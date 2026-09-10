/*
 * s3wedge.c -- S3-2 Stage-1 concurrent-contention WEDGE probe (task #16).
 *
 * THE decisive 30s go/no-go for whether S3-2 Stage 1 (the VRAM-resident page-
 * flip lever, SDL/0077+0078) is salvageable or NO-GO. Both 0077+0078 HANG at the
 * heavy Mimiga frame; read-trap + real-mode-INT both refuted. Leading hypothesis
 * (S3-2-FINAL sec.3, the named-never-measured cell): the lever's SUSTAINED
 * SCATTERED CPU->VRAM writes (every drawcall composites directly into the VRAM
 * back-page, ~1800/frame) wedge the S3 memory-controller/bus at the heavy ~5.4ms
 * BK_PARALLAX frame -- and that scattered write IS the win (-2.89ms), so if it
 * wedges, win == wedge == Stage-1 NO-GO.
 *
 * This probe replicates 0078's per-frame BUS PATTERN in isolation, always-heavy
 * from frame 0, and detects a hard wedge via IRQ-delivery. Mirrors + reuses:
 *   s3blt/s3vram -- VBE LFB map + nearptr + scattered VRAM writes
 *   s3crtc       -- CLI-bracketed CRTC WORD-unit page flip (operator-confirmed
 *                   WORD: page1 = word 0x9600 = CR0C=0x96/CR0D=0x00/CR69=0x00)
 *   audrq        -- SB16 16-bit auto-init DMA ch5 / IRQ-5 (the concurrent audio
 *                   primitive live at the hang) + the minimal-ISR + atexit panic
 *
 * TWO CELLS (sdl-engine spec):
 *   SCATTERED -- ~1800 tile-writes/frame: per tile, READ 256 B from a sysmem
 *                tileset (rotating) -> WRITE row-by-row (16x of 16 B) at the
 *                back-page pitch-strided dst. The per-row stride + per-tile
 *                sysmem-read interleave is the scatter that stresses the arbiter
 *                (NOT one contiguous burst). ~460 KB/frame scattered.
 *   SUSTAINED -- SAME ~460 KB/frame but CONTIGUOUS (sequential fill of the
 *                back-page ~6x), no per-tile read, no stride. Splits
 *                cadence-vs-bandwidth: scattered wedges + sustained doesn't =>
 *                the CADENCE (not raw bandwidth) is the wedge.
 * Both: per-frame CLI-bracketed WORD page-flip + bounded vblank-after + SB16 DMA
 * running continuously (IRQ-5 firing throughout).
 *
 * WEDGE DETECTOR (the oracle): every 64 frames, sample BOTH the CPU RDTSC (the
 * IRQ-INDEPENDENT clock) and the BIOS timer tick at 0040:006C (the IRQ-0-driven
 * IRQ-DELIVERY proxy -- NOT raw port 0x40). Compute expected_ticks from the
 * RDTSC wall delta (18.2065/s). DIVERGENCE -- RDTSC says >=10 ticks should have
 * elapsed but 0040:006C advanced ZERO -- means IRQ delivery is DEAD = the
 * Ctrl-Alt-Del-dead hard wedge = NO-GO. A finite stall shows SLOW-but-nonzero
 * tick advance. (RDTSC is why this is robust: a flat tick alone could just mean
 * the 64 frames ran fast; RDTSC distinguishes wedge from speed.)
 *
 * READ CONTRACT (per-line fsync so a wedge leaves the partial log + last tick):
 *   (a) CLEAN  -- "PROBE-DONE" + steady tick advance both cells -> salvageable.
 *   (b) SOFT   -- a flatline window detected (verdict emitted) -> NO-GO.
 *   (c) HARD   -- log STOPS mid-cell, NO PROBE-DONE, last PROGRESS tick mid-
 *                 advance (= the game's VRON.LOG hang signature) -> NO-GO.
 *   (b)|(c) = Stage-1 NO-GO; only (a) = salvageable.
 *
 * HAZARD: HIGH -- direct VRAM writes + CRTC + SB16 DMA/IRQ all concurrent; the
 * probe's PURPOSE is to provoke a wedge (which on real HW may require Ctrl-Alt-
 * Del). atexit restores: DMA stop + IRQ unhook + DSP reset + CRTC start zero +
 * nearptr off + text mode. Per [[dosbox_not_proxy]] the wedge is a g2k-with-ViRGE
 * result; DOSBox smoke = correctness/no-crash + log structure only.
 *
 * Output: S3WEDGE.LOG (CWD fopen-direct; C:\ fallback), fsync per line.
 * 8.3: S3WEDGE.EXE / S3WEDGE.LOG. Pure DJGPP. -march=i486 -mtune=pentium -O2.
 *
 * License: MIT.
 */

#include <ctype.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#include <time.h>
#include <unistd.h>

#define S3WEDGE_VERSION "v1 (S3-2 #16 Stage-1 contention wedge: scattered vs sustained VRAM-write + WORD flip + SB16 DMA; RDTSC-vs-0040:006C wedge detector)"

/* ---- tunables (sdl-engine spec) ---- */
#define MAX_FRAMES     4000      /* per cell; always-heavy -> a clean full budget is trustworthy */
#define SAMPLE_EVERY   64        /* frames between 0040:006C samples */
#define TILES_X        20        /* 320/16 */
#define TILES_Y        15        /* 240/16 */
#define PASSES         6         /* 20x15x6 = 1800 tile-writes/frame (~460 KB)   */
#define TILE_BYTES     256       /* 16x16 */
#define TILESET_BYTES  65536u    /* sysmem source for the scattered reads        */
#define WEDGE_MIN_EXPECT 10.0    /* >=10 ticks expected in a window to call flatline */
#define WEDGE_CONSEC   2         /* consecutive flatline windows to declare WEDGE  */
#define BIOS_TICK_LIN  0x46Cu    /* 0040:006C linear addr (BIOS timer tick, DWORD) */
#define TICKS_PER_SEC  18.2065

/* ============================================================ */
/* Logging (fsync per line -- a wedge must leave the partial log) */
/* ============================================================ */

static FILE *g_log = NULL;
static void open_log(void)
{
    g_log = fopen("S3WEDGE.LOG", "w");
    if (!g_log) g_log = fopen("C:\\S3WEDGE.LOG", "w");
}
static void wlog(const char *fmt, ...)
{
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); fsync(fileno(g_log)); }
}

/* ============================================================ */
/* RDTSC timing (audrq idiom) -- the IRQ-independent clock        */
/* ============================================================ */

static double g_us_per_cycle = 0.0; static uint32_t g_cpu_mhz = 0;
static inline uint64_t rdtsc(void){ uint32_t lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((uint64_t)hi<<32)|lo; }
static double now_secs(void){ return (double)uclock()/(double)UCLOCKS_PER_SEC; }
static double cyc_to_secs(uint64_t c){ return (g_us_per_cycle>0.0)?((double)c*g_us_per_cycle/1e6):-1.0; }
static void calibrate_rdtsc(void)
{
    double t0=now_secs(); uint64_t c0=rdtsc();
    while (now_secs()-t0 < 0.100) {}
    double secs=now_secs()-t0; uint64_t c1=rdtsc();
    if (secs<=0.0) return;
    double hz=(double)(c1-c0)/secs; g_us_per_cycle=1e6/hz; g_cpu_mhz=(uint32_t)(hz/1e6);
}
/* BIOS timer tick at 0040:006C -- the IRQ-0-driven IRQ-delivery proxy. */
static uint32_t bios_tick(void){ return _farpeekl(_dos_ds, BIOS_TICK_LIN); }

/* ============================================================ */
/* CRTC (S3) -- WORD-unit page flip (s3crtc idiom, CLI-bracketed) */
/* ============================================================ */

#define CRTC_IDX 0x3D4
#define CRTC_DATA 0x3D5
static uint8_t cr_read(uint8_t i){ outportb(CRTC_IDX,i); return inportb(CRTC_DATA); }
static void cr_write(uint8_t i,uint8_t v){ outportb(CRTC_IDX,i); outportb(CRTC_DATA,v); }
static void s3_unlock_crtc(void){ cr_write(0x38,0x48); cr_write(0x39,0xA5); }

/* Program display start in WORD units (start_word = byte_off>>1), CLI-bracketed
 * (the index/data pair is stateful -- atomic vs IRQ-5/timer; the 0078 safety). */
static void flip_word(uint32_t byte_off)
{
    uint32_t w = byte_off >> 1;
    __asm__ __volatile__("cli");
    uint8_t c69 = cr_read(0x69);
    cr_write(0x0D, (uint8_t)(w & 0xFF));
    cr_write(0x0C, (uint8_t)((w >> 8) & 0xFF));
    cr_write(0x69, (uint8_t)((c69 & 0xE0) | ((w >> 16) & 0x1F)));
    __asm__ __volatile__("sti");
}
static void wait_vbl(void)
{
    int i=0; while ((inportb(0x3DA)&0x08)!=0){ if(++i>200000) return; }
    i=0; while ((inportb(0x3DA)&0x08)==0){ if(++i>200000) return; }
}

/* ============================================================ */
/* SB16 16-bit auto-init DMA + IRQ-5 (audrq idiom)               */
/* ============================================================ */

static int sb_base=-1, sb_irq=-1, sb_dma16=-1;
static int dma_seg=0, dma_sel=0; static uint32_t dma_phys=0; static long dma_bytes=0;
static const uint8_t high_page_ports[8]={0x87,0x83,0x81,0x82,0x8F,0x8B,0x89,0x8A};

static void parse_blaster(void)
{
    const char *e=getenv("BLASTER");
    sb_base=0x220; sb_irq=5; sb_dma16=5;     /* g2k Vibra16S default A220 I5 H5 */
    if (e) { const char *p=e; while(*p){ while(*p==' '||*p=='\t')p++; if(!*p)break;
        char t=(char)toupper((unsigned char)*p); p++; int v=(int)strtol(p,(char**)&p,(t=='A')?16:10);
        if(t=='A')sb_base=v; else if(t=='I')sb_irq=v; else if(t=='H')sb_dma16=v; } }
    wlog("[s3wedge] BLASTER base=0x%X irq=%d dma16=%d (env=%s)", sb_base, sb_irq, sb_dma16, e?e:"none");
}
static int dsp_reset(void)
{
    outportb(sb_base+6,1); for(volatile int i=0;i<1000;i++)(void)inportb(0x80); outportb(sb_base+6,0);
    int to=1000; while(to-->0){ if(inportb(sb_base+0xE)&0x80) return (inportb(sb_base+0xA)==0xAA)?0:-2;
        for(volatile int i=0;i<100;i++)(void)inportb(0x80); }
    return -1;
}
static void dsp_write(uint8_t v){ int to=100000; while(to-->0){ if((inportb(sb_base+0xC)&0x80)==0){ outportb(sb_base+0xC,v); return; } } }
static int dma_alloc(long n)
{
    int para=(int)((n+15)/16); int kept[8],nk=0,res=-1;
    for(int a=0;a<8;a++){ __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x0100; r.x.bx=(uint16_t)para; __dpmi_int(0x31,&r);
        if(r.x.flags&1) { break; }
        int seg=r.x.ax,sel=r.x.dx; uint32_t ph=(uint32_t)seg<<4;
        if((ph & ~0x1FFFFUL)==((ph+(uint32_t)n-1)&~0x1FFFFUL)){ dma_seg=seg; dma_sel=sel; dma_phys=ph; dma_bytes=n; res=0; break; }
        if(nk<8) kept[nk++]=sel; }
    for(int i=0;i<nk;i++){ __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x0101; r.x.dx=(uint16_t)kept[i]; __dpmi_int(0x31,&r); }
    return res;
}
static void dma_free(void){ if(dma_sel){ __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x0101; r.x.dx=(uint16_t)dma_sel; __dpmi_int(0x31,&r);} dma_seg=dma_sel=0; dma_phys=0; dma_bytes=0; }
static void dma_program_16bit(int ch, uint32_t ph, long n)
{
    int words=(int)(n/2)-1, ci=ch-4; uint8_t page=(uint8_t)((ph>>16)&0xFF);
    outportb(0xD4,(uint8_t)(0x04|(ch&3))); outportb(0xD8,0); outportb(0xD6,(uint8_t)(0x58|(ch&3)));
    outportb(high_page_ports[ch],page);
    outportb(0xC0+ci*4,(uint8_t)((ph>>1)&0xFF)); outportb(0xC0+ci*4,(uint8_t)((ph>>9)&0xFF));
    outportb(0xC0+ci*4+2,(uint8_t)(words&0xFF)); outportb(0xC0+ci*4+2,(uint8_t)((words>>8)&0xFF));
    outportb(0xD4,(uint8_t)(ch&3));
}
static void dma_mask_16bit(int ch){ outportb(0xD4,(uint8_t)(0x04|(ch&3))); }

/* minimal IRQ-5 ISR: ack SB16 (8+16 bit) + EOI + count + deadman self-mask. */
static volatile uint32_t isr_count=0; static volatile int isr_stormed=0;
static int irq_int_num=0, irq_mask_port=0, irq_installed=0, irq_is_slave=0; static uint8_t irq_mask_bit=0;
static _go32_dpmi_seginfo old_vec,new_vec;
#define ISR_DEADMAN 2000000u   /* >>legit IRQ count over a 30s run; storm self-mask */
static void irq_handler(void)
{
    (void)inportb(sb_base+0xE); (void)inportb(sb_base+0xF);   /* ack 8+16-bit */
    if (irq_is_slave) { outportb(0xA0,0x20); } outportb(0x20,0x20);
    isr_count++;
    if (isr_count==ISR_DEADMAN && !isr_stormed){ isr_stormed=1; outportb(irq_mask_port,(uint8_t)(inportb(irq_mask_port)|irq_mask_bit)); }
}
static void irq_handler_end(void){}
static int irq_install(int irq)
{
    if(irq<8){ irq_int_num=0x08+irq; irq_mask_port=0x21; irq_mask_bit=(uint8_t)(1u<<irq); irq_is_slave=0; }
    else { irq_int_num=0x70+(irq-8); irq_mask_port=0xA1; irq_mask_bit=(uint8_t)(1u<<(irq-8)); irq_is_slave=1; }
    _go32_dpmi_lock_code((void*)irq_handler,(unsigned long)((char*)irq_handler_end-(char*)irq_handler));
    _go32_dpmi_lock_data((void*)&isr_count,sizeof isr_count);
    _go32_dpmi_lock_data((void*)&isr_stormed,sizeof isr_stormed);
    _go32_dpmi_lock_data((void*)&sb_base,sizeof sb_base);
    _go32_dpmi_lock_data((void*)&irq_is_slave,sizeof irq_is_slave);
    _go32_dpmi_lock_data((void*)&irq_mask_port,sizeof irq_mask_port);
    _go32_dpmi_lock_data((void*)&irq_mask_bit,sizeof irq_mask_bit);
    if(_go32_dpmi_get_protected_mode_interrupt_vector(irq_int_num,&old_vec)!=0) return -1;
    new_vec.pm_offset=(long)irq_handler; new_vec.pm_selector=_go32_my_cs();
    if(_go32_dpmi_allocate_iret_wrapper(&new_vec)!=0) return -1;
    if(_go32_dpmi_set_protected_mode_interrupt_vector(irq_int_num,&new_vec)!=0){ _go32_dpmi_free_iret_wrapper(&new_vec); return -1; }
    outportb(irq_mask_port,(uint8_t)(inportb(irq_mask_port)|irq_mask_bit));   /* masked at install */
    if(irq_is_slave) outportb(0x21,(uint8_t)(inportb(0x21)&~0x04));           /* unmask cascade IRQ2 */
    irq_installed=1; return 0;
}
static void irq_unmask(void){ if(irq_installed) outportb(irq_mask_port,(uint8_t)(inportb(irq_mask_port)&~irq_mask_bit)); }
static void irq_mask(void){ if(irq_installed) outportb(irq_mask_port,(uint8_t)(inportb(irq_mask_port)|irq_mask_bit)); }
static void irq_uninstall(void){ if(!irq_installed) return; irq_mask(); _go32_dpmi_set_protected_mode_interrupt_vector(irq_int_num,&old_vec); _go32_dpmi_free_iret_wrapper(&new_vec); irq_installed=0; }
static void dsp_start_autoinit(int freq, long buf_bytes)
{
    dsp_write(0xD1);                                   /* speaker on */
    double t=now_secs(); while(now_secs()-t<0.115)(void)inportb(0x80);
    dsp_write(0x41); dsp_write((uint8_t)((freq>>8)&0xFF)); dsp_write((uint8_t)(freq&0xFF));
    int block=((int)(buf_bytes/2)/2)-1;               /* 16-bit stereo half-buf samples */
    dsp_write(0xB6); dsp_write(0x30); dsp_write((uint8_t)(block&0xFF)); dsp_write((uint8_t)((block>>8)&0xFF));
}
/* dsp_stop removed: cleanup() hard-resets the DSP (stops DMA) */
static int g_audio_on=0;

/* ============================================================ */
/* LFB map (s3vram idiom) + globals for atexit                   */
/* ============================================================ */

static __dpmi_meminfo g_info; static int g_mapped=0, g_nearptr=0, g_mode_active=0;
static uint8_t *g_lfb=NULL;

static uint16_t find_lfb(uint32_t *phys,uint16_t *xr,uint16_t *yr,uint32_t *vram,uint16_t *pitch,int *vbemaj)
{
    *phys=0;*xr=0;*yr=0;*vram=0;*pitch=0;*vbemaj=0;
    int sel=0,seg=__dpmi_allocate_dos_memory(32,&sel); if(seg<0) return 0;
    uint32_t b=(uint32_t)seg<<4;
    _farpokeb(_dos_ds,b+0,'V');_farpokeb(_dos_ds,b+1,'B');_farpokeb(_dos_ds,b+2,'E');_farpokeb(_dos_ds,b+3,'2');
    for(int i=4;i<512;i++)_farpokeb(_dos_ds,b+i,0);
    __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x4F00; r.x.es=(uint16_t)seg; r.x.di=0;
    if(__dpmi_int(0x10,&r)<0||r.x.ax!=0x004F){ __dpmi_free_dos_memory(sel); return 0; }
    *vbemaj=(_farpeekw(_dos_ds,b+0x04)>>8)&0xFF; *vram=(uint32_t)_farpeekw(_dos_ds,b+0x12)*65536u;
    uint32_t ml=((uint32_t)_farpeekw(_dos_ds,b+0x10)<<4)+_farpeekw(_dos_ds,b+0x0E);
    uint16_t chosen=0,cx=0,cy=0,cp=0; uint32_t cph=0,cpx=0xFFFFFFFFul;
    for(int mi=0;mi<256;mi++){ uint16_t m=_farpeekw(_dos_ds,ml+mi*2); if(m==0xFFFF)break;
        for(int i=256;i<512;i++)_farpokeb(_dos_ds,b+i,0);
        memset(&r,0,sizeof r); r.x.ax=0x4F01; r.x.cx=m; r.x.es=(uint16_t)seg; r.x.di=256;
        if(__dpmi_int(0x10,&r)<0||r.x.ax!=0x004F)continue;
        uint32_t mb=b+256; uint16_t at=_farpeekw(_dos_ds,mb+0x00),pt=_farpeekw(_dos_ds,mb+0x10);
        uint16_t x=_farpeekw(_dos_ds,mb+0x12),y=_farpeekw(_dos_ds,mb+0x14);
        uint8_t bp=_farpeekb(_dos_ds,mb+0x19),mo=_farpeekb(_dos_ds,mb+0x1B); uint32_t ph=_farpeekl(_dos_ds,mb+0x28);
        if(!(at&1)||!(at&0x80)||bp!=8||mo!=4||ph==0)continue;
        uint32_t px=(uint32_t)x*y; if(px<76800u)continue;
        if(px<cpx){ chosen=m; cph=ph; cpx=px; cx=x; cy=y; cp=pt; } }
    __dpmi_free_dos_memory(sel);
    if(chosen){ *phys=cph; *xr=cx; *yr=cy; *pitch=cp; }
    return chosen;
}

/* ============================================================ */
/* atexit -- restore EVERYTHING (audio + video). Idempotent.     */
/* ============================================================ */

static void cleanup(void)
{
    if (g_audio_on) { irq_mask(); if(sb_dma16>=4&&sb_dma16<=7) dma_mask_16bit(sb_dma16);
        if(sb_base>0){ outportb(sb_base+6,1); for(volatile int i=0;i<1000;i++)(void)inportb(0x80); outportb(sb_base+6,0); } }
    irq_uninstall();
    dma_free();
    g_audio_on=0;
    if (g_mode_active) { cr_write(0x0D,0); cr_write(0x0C,0); uint8_t c=cr_read(0x69); cr_write(0x69,(uint8_t)(c&0xE0)); }
    if (g_nearptr) { __djgpp_nearptr_disable(); g_nearptr=0; }
    if (g_mapped) { __dpmi_free_physical_address_mapping(&g_info); g_mapped=0; }
    if (g_mode_active) { __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x0003; __dpmi_int(0x10,&r); g_mode_active=0; }
}

/* ============================================================ */
/* The two contention cells + the wedge detector                 */
/* ============================================================ */

static uint8_t *g_tileset=NULL;   /* sysmem source for scattered reads */

/* SCATTERED: ~1800 tile-writes into the back page at write_base. Per tile:
 * read 256 B from a rotating tileset offset -> write row-by-row, pitch-strided. */
static void cell_scattered(uint32_t write_base, uint32_t pitch)
{
    uint32_t ts_off = 0;
    for (int p = 0; p < PASSES; p++)
        for (int ty = 0; ty < TILES_Y; ty++)
            for (int tx = 0; tx < TILES_X; tx++) {
                const uint8_t *src = g_tileset + ts_off;
                uint32_t dst = write_base + (uint32_t)(ty*16)*pitch + (uint32_t)(tx*16);
                for (int row = 0; row < 16; row++) {
                    const uint8_t *s = src + row*16;
                    uint8_t *d = g_lfb + dst + (uint32_t)row*pitch;
                    for (int c = 0; c < 16; c++) d[c] = s[c];   /* read sysmem -> write VRAM */
                }
                ts_off += TILE_BYTES;       /* rotate through the sysmem tileset */
                if (ts_off + TILE_BYTES > TILESET_BYTES) ts_off = 0;
            }
}

/* SUSTAINED: same ~460 KB volume but CONTIGUOUS -- sequential fill of the
 * page_size region PASSES times. No per-tile sysmem read, no stride. */
static void cell_sustained(uint32_t write_base, uint32_t page_size)
{
    for (int p = 0; p < PASSES; p++)
        memset(g_lfb + write_base, (uint8_t)(0x20 + p), page_size);
}

typedef enum { CELL_SCATTERED, CELL_SUSTAINED } cell_t;

/* Run one cell up to MAX_FRAMES with the RDTSC-vs-0040:006C wedge detector.
 * Returns: 0 = CLEAN (full budget, ticks steady), 1 = SOFT WEDGE (flatline). A
 * HARD wedge does not return (CPU/bus dead) -> truncated log = the (c) signature. */
static int run_cell(cell_t cell, uint32_t page_size, uint32_t pitch, int max_frames)
{
    const char *name = (cell==CELL_SCATTERED)?"SCATTERED":"SUSTAINED";
    wlog("[s3wedge CELL_BEGIN %s frames<=%d sample_every=%d expect>=%.0f ticks/window]",
         name, max_frames, SAMPLE_EVERY, WEDGE_MIN_EXPECT);
    int consec_flat = 0;
    uint64_t win_tsc = rdtsc(); uint32_t win_tick = bios_tick();
    for (int f = 0; f < max_frames; f++) {
        uint32_t back = (uint32_t)(f & 1);
        uint32_t wbase = back * page_size;
        if (cell==CELL_SCATTERED) cell_scattered(wbase, pitch);
        else                      cell_sustained(wbase, page_size);
        flip_word(wbase);          /* WORD-unit CRTC page flip (CLI-bracketed) */
        wait_vbl();
        if (((f+1) % SAMPLE_EVERY) == 0) {
            uint64_t tsc = rdtsc(); uint32_t tick = bios_tick();
            double wall = cyc_to_secs(tsc - win_tsc);
            uint32_t dticks = tick - win_tick;       /* no 24h wrap in a 30s run */
            double expect = wall * TICKS_PER_SEC;
            wlog("[s3wedge PROGRESS %s frame=%d tick=0x%08lX dticks=%lu wall=%.2fs expect=%.1f irq=%lu]",
                 name, f+1, (unsigned long)tick, (unsigned long)dticks, wall, expect,
                 (unsigned long)isr_count);
            if (expect >= WEDGE_MIN_EXPECT && dticks == 0) {
                consec_flat++;
                wlog("[s3wedge FLATLINE %s window#%d expect=%.1f dticks=0 (IRQ-delivery dead?)]",
                     name, consec_flat, expect);
                if (consec_flat >= WEDGE_CONSEC) {
                    wlog("[s3wedge CELL_DONE %s verdict=SOFT_WEDGE frame=%d -- 0040:006C flatline "
                         "while RDTSC advanced -> IRQ delivery dead = HARD-FREEZE class = NO-GO]",
                         name, f+1);
                    return 1;
                }
            } else if (dticks > 0) {
                consec_flat = 0;     /* tick alive -> reset (finite stall is not a wedge) */
            }
            win_tsc = tsc; win_tick = tick;
        }
    }
    wlog("[s3wedge CELL_DONE %s verdict=CLEAN frames=%d -- ran the full budget with steady "
         "0040:006C advance (no wedge)]", name, max_frames);
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    int max_frames = MAX_FRAMES;
    if (argc > 1 && (strcmp(argv[1],"SMOKE")==0 || strcmp(argv[1],"smoke")==0))
        max_frames = 128;     /* DOSBox correctness smoke: 2 sample windows, fast */
    atexit(cleanup);
    open_log();
    wlog("=== S3WEDGE -- S3-2 Stage-1 concurrent-contention wedge probe (task #16) ===");
    wlog("S3WEDGE-BEGIN");
    wlog("version: %s", S3WEDGE_VERSION);

    calibrate_rdtsc();
    wlog("[s3wedge] rdtsc cpu_mhz=%u us_per_cycle=%.6f", (unsigned)g_cpu_mhz, g_us_per_cycle);
    if (g_us_per_cycle <= 0.0) {
        wlog("[s3wedge GATE verdict=ABORT_NO_RDTSC]"); wlog("S3WEDGE-DONE");
        if (g_log) { fclose(g_log); } return 2;
    }

    /* ---- LFB mode + map ---- */
    uint32_t phys=0,vram=0; uint16_t xres=0,yres=0,pitch=0; int vbemaj=0;
    uint16_t mode = find_lfb(&phys,&xres,&yres,&vram,&pitch,&vbemaj);
    if (!mode) { wlog("[s3wedge GATE verdict=ABORT_NO_LFB]"); wlog("S3WEDGE-DONE"); if(g_log){fclose(g_log);} return 2; }
    if (pitch==0) pitch=xres;
    uint32_t page_size = (uint32_t)pitch*yres;
    wlog("[s3wedge] LFB mode=0x%04X %ux%u pitch=%u page_size=%lu phys=0x%08lX vram=%lu",
         mode,xres,yres,pitch,(unsigned long)page_size,(unsigned long)phys,(unsigned long)vram);

    __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x4F02; r.x.bx=mode|0x4000; __dpmi_int(0x10,&r);
    g_mode_active=1;
    uint32_t map_size = 2*page_size;            /* page 0 + page 1 */
    if (vram && map_size > vram) map_size = vram;
    memset(&g_info,0,sizeof g_info); g_info.address=phys; g_info.size=map_size;
    if (__dpmi_physical_address_mapping(&g_info)!=0 || !__djgpp_nearptr_enable()) {
        wlog("[s3wedge GATE verdict=ABORT_MAP_FAILED]"); wlog("S3WEDGE-DONE"); cleanup(); if(g_log){fclose(g_log);} return 2;
    }
    g_mapped=1; g_nearptr=1;
    g_lfb = (uint8_t *)(g_info.address + __djgpp_conventional_base);

    /* sysmem tileset for the scattered reads + clear both pages. */
    g_tileset = (uint8_t *)malloc(TILESET_BYTES);
    if (!g_tileset) { wlog("[s3wedge GATE verdict=ABORT_NOMEM]"); wlog("S3WEDGE-DONE"); cleanup(); if(g_log){fclose(g_log);} return 2; }
    for (uint32_t i=0;i<TILESET_BYTES;i++) g_tileset[i]=(uint8_t)(i&0xFF);
    memset(g_lfb, 0x10, (map_size<page_size)?map_size:page_size);

    s3_unlock_crtc();
    uint8_t cr67=cr_read(0x67);
    wlog("[s3wedge] CR67=0x%02X streams=%s (expect non on g2k ViRGE)", cr67, ((cr67&0x0C)==0x0C)?"YES":"no");

    /* ---- SB16 DMA + IRQ-5 (the concurrent audio primitive) ---- */
    parse_blaster();
    if (dsp_reset()!=0) {
        wlog("[s3wedge] WARN SB16 DSP reset failed at 0x%X -- running WITHOUT audio "
             "(contention test weaker; flag in verdict)", sb_base);
    } else {
        long buf = 16384;
        if (dma_alloc(buf)==0 && irq_install(sb_irq)==0) {
            for (long i=0;i<dma_bytes;i++) _farpokeb(_dos_ds, ((uint32_t)dma_seg<<4)+i, (uint8_t)((i*7)&0xFF));
            dma_program_16bit(sb_dma16, dma_phys, buf);
            dsp_start_autoinit(22050, buf);
            irq_unmask();
            g_audio_on=1;
            wlog("[s3wedge] SB16 DMA auto-init 22050/16-bit/stereo ch%d IRQ%d RUNNING (concurrent)", sb_dma16, sb_irq);
        } else {
            wlog("[s3wedge] WARN SB16 DMA/IRQ setup failed -- running WITHOUT audio");
            dma_free();
        }
    }
    wlog("[s3wedge] audio_concurrent=%s", g_audio_on?"YES":"NO");

    /* ---- run both cells (scattered = the suspect, first) ---- */
    wlog("[s3wedge SUITE_BEGIN audio=%d page_size=%lu pitch=%u max_frames=%d%s]",
         g_audio_on, (unsigned long)page_size, pitch, max_frames,
         (max_frames < MAX_FRAMES) ? " (SMOKE)" : "");
    int sc = run_cell(CELL_SCATTERED, page_size, pitch, max_frames);
    int su = run_cell(CELL_SUSTAINED, page_size, pitch, max_frames);

    /* ---- teardown + verdict ---- */
    cleanup();
    const char *verdict;
    if (sc==1 && su==1)      verdict = "NOGO_BOTH_WEDGE";              /* bandwidth-class */
    else if (sc==1 && su==0) verdict = "NOGO_SCATTERED_WEDGE_CADENCE"; /* cadence is the wedge */
    else if (sc==0 && su==1) verdict = "ANOMALOUS_SUSTAINED_ONLY";
    else                     verdict = "CLEAN_SALVAGEABLE";           /* both ran full budget */
    wlog("");
    wlog("[s3wedge GATE scattered=%s sustained=%s audio=%d verdict=%s]",
         sc?"WEDGE":"CLEAN", su?"WEDGE":"CLEAN", g_audio_on, verdict);
    wlog("  CLEAN_SALVAGEABLE -> the scattered VRAM-write pattern does NOT wedge the S3 bus");
    wlog("    even heavy+concurrent -> Stage-1 hang is something else; lever salvageable.");
    wlog("  NOGO_* -> the lever's own write pattern wedges (win==wedge) -> Stage-1 NO-GO.");
    wlog("  (HARD wedge = this log STOPS mid-cell with no GATE/PROBE-DONE -> also NO-GO.)");
    wlog("PROBE-DONE frames_scattered=%d frames_sustained=%d", sc?-1:max_frames, su?-1:max_frames);
    wlog("S3WEDGE-DONE");
    free(g_tileset);
    if (g_log) fclose(g_log);
    return 0;
}
