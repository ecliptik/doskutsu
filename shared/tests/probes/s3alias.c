/*
 * s3alias.c -- S3-2 Stage-1 MMIO-ALIASED-INTO-LFB probe (task #26, hyp-3).
 *
 * THE go/no-go for hypothesis 3. Static analysis is exhausted: the engine's
 * VRAM-resident backdrop draw crashes deterministically, yet base/pitch/offset
 * are ALL source-confirmed correct (vram_ptr in-aperture, not a bad base). The
 * decisive constraint: NOREP's ONE BULK sequential copy (sysmem->VRAM, ~76800 B
 * contiguous) is CLEAN, but the engine's SCATTERED per-tile backdrop blits to
 * the SAME correct aperture CRASH + corrupt SYSMEM/CODE. A plain "LFB write
 * faults" cannot explain bulk-clean. The only fit:
 *
 * HYPOTHESIS 3: the S3 ViRGE 2D-engine/command registers are ALIASED inside the
 * LFB aperture (the low ~32-64 KB overlays the command regs onto the framebuffer
 * start), so a SCATTERED CPU write (misaligned dword+byte sequence) assembles a
 * GARBAGE 2D command -> the ViRGE bus-masters/BLTs to a wild sysmem/code addr ->
 * corruption. A BULK dword-sequential write writes complete benign dwords + does
 * NOT assemble a triggering command -> clean. (Fits scattered-vs-bulk + sysmem
 * corruption + determinism + S3-specific. new-MMIO at +16MB is NOT the alias;
 * the LOW aperture is -> the PAGE-0 backdrop write is prime suspect, matching
 * the single-buffered CANSB page-0 crash.)
 *
 * Standalone DJGPP; NO SDL/engine. Reuses s3blt(MMIO+CR53+2D-reg defs) +
 * s3wedge/s3vram(LFB map + scattered write). Maps the LFB (writes) + the new-
 * MMIO window (reads the 2D regs + SUBSYS_STAT to detect aliasing/activation).
 *
 * ORACLES (per cell; logged immediately + per-line fsync so a crash IS the proof):
 *   (1) SUBSYS_STAT 2D-engine ACTIVATION -- sampled during the writes; did the
 *       engine go BUSY (run a command it was never explicitly given)? [primary,
 *       readable]
 *   (2) SYSMEM CANARY -- a multi-MB known-pattern region checked post-cell; a
 *       wild bus-master BLT corrupts it. [primary, = the engine's crash signature]
 *   (3) 2D command-register snapshot BEFORE/AFTER -- did the LFB writes CHANGE
 *       the command regs? [corroborating; INFO only -- 2D regs may be write-only,
 *       so "no change" is NOT conclusive, but a change is a direct alias proof]
 *
 * CELLS, each at page 0 (LFB+0, the suspect) AND page 1 (LFB+page_size):
 *   A BULK     -- one rep-movsd (memcpy) sequential write of the page. EXPECT
 *                 clean: no activation, canary intact, regs unchanged (= NOREP).
 *   B SCATTERED-- per-tile, per-row 3-dwords + 4-bytes INTERLEAVED at pitch-
 *                 strided dst (the misaligned dword+byte sequence = VRON backdrop
 *                 blit). EXPECT (if hyp-3): activation / canary corrupt / regs
 *                 change.
 * B-corrupts/activates + A-clean = HYPOTHESIS 3 CONFIRMED.
 *
 * LOW-OFFSET ALIAS SWEEP: write a unique sentinel dword at LFB+{0,0x100,...,
 * 0xA4D4,0xA500,0x8504,...}, re-snapshot the 2D regs after each, report which
 * LFB offset changed which register -> pinpoints the alias mapping.
 *
 * RIDE-ALONGS: DPMI mapped SIZE (full 4 MB?), nearptr DS-limit vs aperture top,
 * ViRGE CR (CR53/CR58/CR59/CR31) MMIO-vs-LFB layout.
 *
 * Per [[dosbox_not_proxy]]: the aliasing is a g2k-with-ViRGE property; DOSBox
 * smoke = correctness/no-crash + log structure only (its LFB is host RAM, no 2D
 * aliasing -> both cells clean under emulation, the EXPECTED non-result).
 *
 * HAZARD: HIGH -- Cell B may trigger a wild BLT that crashes/wedges the machine
 * (operator reboot-first; that crash + the fsync'd Cell-B-BEGIN + BEFORE snapshot
 * IS the confirmation). atexit restores DMA-none/CRTC/nearptr/text. Bounded.
 *
 * Output: S3ALIAS.LOG (CWD fopen-direct; C:\ fallback), per-line fsync.
 * 8.3: S3ALIAS.EXE / S3ALIAS.LOG. Pure DJGPP -march=i486 -mtune=pentium -O2.
 *
 * License: MIT.
 */

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
#include <sys/nearptr.h>
#include <time.h>
#include <unistd.h>

#define S3ALIAS_VERSION "v1 (S3-2 #26 hyp-3 MMIO-aliased-into-LFB: bulk-vs-scattered LFB write -> 2D-engine activation / canary / reg-change; low-offset alias sweep)"

#define MMIO_OFFSET   0x01000000UL    /* new MMIO = LFB + 16 MB (read the 2D regs) */
#define MMIO_SIZE     0x00010000UL
#define TILES_X       20
#define TILES_Y       15
#define TILE          16
#define CANARY_BYTES  (2u*1024u*1024u)  /* 2 MB sysmem canary for the wild BLT     */
#define CANARY_FILL32 0xCA11AB1Eul       /* "callable" -- distinctive dword pattern  */

/* 2D-engine / command registers (MMIO offsets, from s3blt). */
#define R_SRC_BASE      0xA4D4
#define R_DEST_BASE     0xA4D8
#define R_DEST_SRC_STR  0xA4E4
#define R_CMD_SET       0xA500
#define R_RWIDTH_HEIGHT 0xA504
#define R_RSRC_XY       0xA508
#define R_RDEST_XY      0xA50C
#define R_SUBSYS_STAT   0x8504
#define S3D_IDLE_BIT    0x2000          /* SUBSYS_STAT bit13 set = engine idle      */

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;
static void open_log(void){ g_log=fopen("S3ALIAS.LOG","w"); if(!g_log) g_log=fopen("C:\\S3ALIAS.LOG","w"); }
static void alog(const char *fmt, ...)
{
    char buf[640]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
    fputs(buf,stdout); fputc('\n',stdout); fflush(stdout);
    if(g_log){ fputs(buf,g_log); fputc('\n',g_log); fflush(g_log); fsync(fileno(g_log)); }
}

/* ============================================================ */
/* CRTC + MMIO + LFB                                             */
/* ============================================================ */

static uint8_t cr_read(uint8_t i){ outportb(0x3D4,i); return inportb(0x3D5); }
static void cr_write(uint8_t i,uint8_t v){ outportb(0x3D4,i); outportb(0x3D5,v); }
static void s3_unlock_crtc(void){ cr_write(0x38,0x48); cr_write(0x39,0xA5); }

static volatile uint8_t *g_mmio=NULL, *g_lfb=NULL;
static inline uint32_t mmio_r32(uint32_t o){ return *(volatile uint32_t*)(g_mmio+o); }
static inline void mmio_w32(uint32_t o,uint32_t v){ *(volatile uint32_t*)(g_mmio+o)=v; }

/* ============================================================ */
/* VBE 8bpp LFB mode finder (s3wedge idiom; returns pitch)       */
/* ============================================================ */

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
/* atexit -- restore CRTC start + nearptr + map + text           */
/* ============================================================ */

static __dpmi_meminfo g_lfbmap, g_mmiomap;
static int g_lfb_mapped=0, g_mmio_mapped=0, g_nearptr=0, g_mode=0;
static uint8_t g_cr53_orig=0; static int g_cr53_dirty=0;

static void cleanup(void)
{
    if(g_mode){ cr_write(0x0D,0); cr_write(0x0C,0); }
    if(g_cr53_dirty){ cr_write(0x53,g_cr53_orig); g_cr53_dirty=0; }
    if(g_nearptr){ __djgpp_nearptr_disable(); g_nearptr=0; }
    if(g_mmio_mapped){ __dpmi_free_physical_address_mapping(&g_mmiomap); g_mmio_mapped=0; }
    if(g_lfb_mapped){ __dpmi_free_physical_address_mapping(&g_lfbmap); g_lfb_mapped=0; }
    if(g_mode){ __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x0003; __dpmi_int(0x10,&r); g_mode=0; }
}

/* ============================================================ */
/* 2D-register snapshot                                          */
/* ============================================================ */

static const uint32_t REGS[] = { R_SRC_BASE,R_DEST_BASE,R_DEST_SRC_STR,R_CMD_SET,R_RWIDTH_HEIGHT,R_RSRC_XY,R_RDEST_XY };
static const char *REGN[]    = { "SRC_BASE","DEST_BASE","STRIDE","CMD_SET","RWH","RSRC_XY","RDEST_XY" };
#define NREGS ((int)(sizeof REGS/sizeof REGS[0]))

static void snap_regs(uint32_t *out){ for(int i=0;i<NREGS;i++) out[i]=mmio_r32(REGS[i]); }
static int regs_changed(const uint32_t *a,const uint32_t *b,char *desc,int dlen)
{
    int n=0; desc[0]=0;
    for(int i=0;i<NREGS;i++) if(a[i]!=b[i]){
        char t[64]; snprintf(t,sizeof t,"%s%s:0x%08lX->0x%08lX", n?",":"", REGN[i],(unsigned long)a[i],(unsigned long)b[i]);
        if((int)strlen(desc)+(int)strlen(t)<dlen-1) strcat(desc,t);
        n++;
    }
    return n;
}

/* ============================================================ */
/* sysmem canary                                                 */
/* ============================================================ */

static uint32_t *g_canary=NULL;
static void canary_fill(void){ for(uint32_t i=0;i<CANARY_BYTES/4;i++) g_canary[i]=CANARY_FILL32 ^ i; }
static long canary_check(void){ for(uint32_t i=0;i<CANARY_BYTES/4;i++) if(g_canary[i]!=(CANARY_FILL32^i)) return (long)i*4; return -1; }

/* ============================================================ */
/* write kernels + activation sampling                           */
/* ============================================================ */

/* sample SUBSYS_STAT; return 1 if the engine is BUSY (idle bit clear). */
static int engine_busy(void){ return (mmio_r32(R_SUBSYS_STAT) & S3D_IDLE_BIT) == 0; }

/* BULK: one sequential memcpy of `n` bytes from sysmem src to LFB+base. */
static void cell_bulk(uint32_t base, const uint8_t *src, uint32_t n){ memcpy((void*)(g_lfb+base), src, n); }

/* SCATTERED: per-tile, per-row 3 dwords + 4 bytes interleaved at pitch-strided
 * dst (the misaligned dword+byte VRON backdrop blit). Samples engine-busy every
 * tile (returns 1 if the engine EVER went busy = activated by the writes). */
static int cell_scattered(uint32_t base, uint32_t pitch, const uint8_t *src)
{
    int activated=0; uint32_t s=0;
    for(int ty=0;ty<TILES_Y;ty++) for(int tx=0;tx<TILES_X;tx++){
        uint32_t dst = base + (uint32_t)(ty*TILE)*pitch + (uint32_t)(tx*TILE);
        for(int row=0;row<TILE;row++){
            volatile uint8_t *d = g_lfb + dst + (uint32_t)row*pitch;
            const uint8_t *sp = src + s; s = (s + TILE) & (65536u-1u);
            /* 3 dwords (12 B) then 4 single bytes (4 B) = 16 B, MISALIGNED mix. */
            *(volatile uint32_t*)(d+0) = *(const uint32_t*)(sp+0);
            *(volatile uint32_t*)(d+4) = *(const uint32_t*)(sp+4);
            *(volatile uint32_t*)(d+8) = *(const uint32_t*)(sp+8);
            d[12]=sp[12]; d[13]=sp[13]; d[14]=sp[14]; d[15]=sp[15];
        }
        if (engine_busy()) activated=1;     /* did the LFB writes start the engine? */
    }
    return activated;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    atexit(cleanup);
    open_log();
    alog("=== S3ALIAS -- S3-2 Stage-1 MMIO-aliased-into-LFB probe (task #26 hyp-3) ===");
    alog("S3ALIAS-BEGIN");
    alog("version: %s", S3ALIAS_VERSION);

    /* sysmem source (64 KB) + the canary. */
    uint8_t *src = (uint8_t*)malloc(65536); g_canary=(uint32_t*)malloc(CANARY_BYTES);
    if(!src||!g_canary){ alog("[s3alias GATE verdict=ABORT_NOMEM]"); alog("S3ALIAS-DONE"); if(g_log)fclose(g_log); return 2; }
    for(int i=0;i<65536;i++) src[i]=(uint8_t)(i*7);
    canary_fill();
    alog("[s3alias] canary @%p size=%lu pattern=0x%08lX^idx", (void*)g_canary,(unsigned long)CANARY_BYTES,(unsigned long)CANARY_FILL32);

    /* LFB mode + map. */
    uint32_t phys=0,vram=0; uint16_t xres=0,yres=0,pitch=0; int vbemaj=0;
    uint16_t mode=find_lfb(&phys,&xres,&yres,&vram,&pitch,&vbemaj);
    if(!mode){ alog("[s3alias GATE verdict=ABORT_NO_LFB]"); alog("S3ALIAS-DONE"); if(g_log)fclose(g_log); return 2; }
    if(pitch==0) pitch=xres;
    uint32_t page_size=(uint32_t)pitch*yres;
    alog("[s3alias] LFB mode=0x%04X %ux%u pitch=%u page_size=%lu phys=0x%08lX vram=%lu",
         mode,xres,yres,pitch,(unsigned long)page_size,(unsigned long)phys,(unsigned long)vram);

    __dpmi_regs r; memset(&r,0,sizeof r); r.x.ax=0x4F02; r.x.bx=mode|0x4000; __dpmi_int(0x10,&r); g_mode=1;
    uint32_t lfb_span = 2*page_size; if(vram && lfb_span>vram) lfb_span=vram;
    memset(&g_lfbmap,0,sizeof g_lfbmap); g_lfbmap.address=phys; g_lfbmap.size=lfb_span;
    memset(&g_mmiomap,0,sizeof g_mmiomap); g_mmiomap.address=phys+MMIO_OFFSET; g_mmiomap.size=MMIO_SIZE;
    int lo=(__dpmi_physical_address_mapping(&g_lfbmap)==0); if(lo)g_lfb_mapped=1;
    int mo=lo&&(__dpmi_physical_address_mapping(&g_mmiomap)==0); if(mo)g_mmio_mapped=1;
    int np=mo&&__djgpp_nearptr_enable(); if(np)g_nearptr=1;
    /* RIDE-ALONG (1)(2): mapped size + nearptr DS-limit vs aperture top. */
    alog("[s3alias] MAP lfb=%d(span=%lu) mmio=%d nearptr=%d ds_limit=0x%08lX aperture_top=0x%08lX full4MB=%s",
         lo,(unsigned long)lfb_span,mo,np,(unsigned long)__dpmi_get_segment_limit(_my_ds()),
         (unsigned long)(phys+0x400000), (vram>=0x400000)?"yes":"no");
    if(!np){ alog("[s3alias GATE verdict=ABORT_MAP_FAILED lfb=%d mmio=%d nearptr=%d]",lo,mo,np); alog("S3ALIAS-DONE"); cleanup(); if(g_log)fclose(g_log); return 2; }
    g_lfb=(volatile uint8_t*)(g_lfbmap.address+__djgpp_conventional_base);
    g_mmio=(volatile uint8_t*)(g_mmiomap.address+__djgpp_conventional_base);

    /* CR53 new-MMIO enable (read the 2D regs) + RIDE-ALONG (3) CR config dump. */
    s3_unlock_crtc();
    g_cr53_orig=cr_read(0x53); cr_write(0x53,(uint8_t)(g_cr53_orig|0x08)); g_cr53_dirty=1;
    alog("[s3alias] CR config: CR53 0x%02X->0x%02X(|=0x08 new-MMIO) CR58=0x%02X CR59=0x%02X CR31=0x%02X CR67=0x%02X",
         g_cr53_orig, cr_read(0x53), cr_read(0x58), cr_read(0x59), cr_read(0x31), cr_read(0x67));
    alog("[s3alias] 2D-reg readback sanity (via new-MMIO): SUBSYS_STAT=0x%08lX (idle_bit set=%d)",
         (unsigned long)mmio_r32(R_SUBSYS_STAT), (mmio_r32(R_SUBSYS_STAT)&S3D_IDLE_BIT)?1:0);

    alog("[s3alias SUITE_BEGIN page_size=%lu pitch=%u]", (unsigned long)page_size, pitch);

    /* ---- cells: A(bulk) + B(scattered), each at page 0 then page 1 ---- */
    uint32_t pages[2] = { 0u, page_size };
    int any_alias = 0;
    for (int pg=0; pg<2; pg++) {
        uint32_t base = pages[pg];
        if (base + page_size > lfb_span) { alog("[s3alias] page %d @0x%lX skipped (beyond map)",pg,(unsigned long)base); continue; }

        /* Cell A BULK */
        canary_fill();
        uint32_t before[NREGS],after[NREGS]; snap_regs(before);
        alog("[s3alias CELL_BEGIN A_BULK page=%d base=0x%05lX]", pg, (unsigned long)base);
        cell_bulk(base, src, page_size);
        snap_regs(after);
        char d[400]; int rc=regs_changed(before,after,d,sizeof d); long can=canary_check(); int busy=engine_busy();
        alog("[s3alias CELL_DONE A_BULK page=%d regs_changed=%d{%s} canary_corrupt_off=%ld engine_busy=%d]",
             pg, rc, d, can, busy);

        /* Cell B SCATTERED (the suspect). Log BEGIN + BEFORE so a crash mid-cell is attributable. */
        canary_fill();
        snap_regs(before);
        alog("[s3alias CELL_BEGIN B_SCATTERED page=%d base=0x%05lX subsys_before=0x%08lX -- if the log STOPS here, the scattered write wedged/crashed = hyp-3 CONFIRMED]",
             pg, (unsigned long)base, (unsigned long)mmio_r32(R_SUBSYS_STAT));
        int activated = cell_scattered(base, pitch, src);
        snap_regs(after);
        rc=regs_changed(before,after,d,sizeof d); can=canary_check(); busy=engine_busy();
        int alias = (rc>0) || activated || (can>=0) || busy;
        if (alias) any_alias=1;
        alog("[s3alias CELL_DONE B_SCATTERED page=%d regs_changed=%d{%s} engine_activated=%d canary_corrupt_off=%ld engine_busy_after=%d -> %s]",
             pg, rc, d, activated, can, busy, alias?"ALIAS-SIGNAL":"clean");
    }

    /* ---- low-offset alias sweep: which LFB offset hits which 2D reg? ---- */
    alog("[s3alias SWEEP_BEGIN -- write a sentinel dword at LFB+off, re-snapshot 2D regs]");
    static const uint32_t sweep[] = {0,0x100,0x1000,0x4000,0x8000,0x8504,0xA000,0xA4D4,0xA500,0xC000,0x10000};
    for (int i=0;i<(int)(sizeof sweep/sizeof sweep[0]);i++){
        uint32_t off=sweep[i]; if(off+4>lfb_span) continue;
        uint32_t before[NREGS],after[NREGS]; snap_regs(before);
        uint32_t sentinel = 0x5A000000ul | off;
        *(volatile uint32_t*)(g_lfb+off) = sentinel;
        snap_regs(after);
        char d[400]; int rc=regs_changed(before,after,d,sizeof d);
        alog("[s3alias SWEEP off=0x%05lX wrote=0x%08lX regs_changed=%d{%s}]",
             (unsigned long)off,(unsigned long)sentinel,rc,d);
        if (rc>0) any_alias=1;
    }
    alog("[s3alias SWEEP_DONE]");

    /* ---- teardown + verdict ---- */
    long final_canary = canary_check();
    cleanup();
    const char *verdict = any_alias ? "ALIAS_CONFIRMED_HYP3"
                        : (final_canary>=0) ? "ALIAS_CANARY_ONLY"
                        : "NO_ALIAS_HYP3_REFUTED";
    alog("");
    alog("[s3alias GATE any_alias=%d final_canary_off=%ld verdict=%s]", any_alias, final_canary, verdict);
    alog("  ALIAS_CONFIRMED_HYP3 -> scattered LFB-aperture writes reach the 2D engine "
         "(reg-change/activation/canary) -> the engine assembles a garbage command + "
         "bus-masters wild = the Stage-1 crash. Stage-1 NO-GO unless the back-page is "
         "moved CLEAR of the alias offset (see SWEEP) or the engine is gated.");
    alog("  NO_ALIAS_HYP3_REFUTED -> bulk+scattered both clean, no reg-change/activation "
         "-> hyp-3 refuted; the crash is elsewhere (back to the arithmetic/pitch branch).");
    alog("  (HARD: if this log STOPS at a B_SCATTERED CELL_BEGIN with no CELL_DONE, the "
         "scattered write crashed the machine = hyp-3 CONFIRMED the hard way.)");
    alog("S3ALIAS-DONE");
    free(src); free(g_canary);
    if(g_log) fclose(g_log);
    return 0;
}
