/*
 * s3blt.c -- S3 ViRGE/DX 2D BitBLT engine probe (S3-VIRGE campaign P3).
 *
 * v2 (post iter-1): iter-1 returned RED_MMIO_UNVERIFIED -- the new-MMIO window
 * at LFB+0x1000000 read 0xFF everywhere because it was never ENABLED. The VBE
 * BIOS sets up a working 8bpp enhanced mode but leaves the MMIO register window
 * off (a VBE app does not need it). v2 enables it (CR53 |= 0x08, per
 * xf86-video-s3virge) after the mode set + a CRTC re-unlock, with a CR53
 * bits3+4 fallback, then re-verifies chip-ID-thru-MMIO before the kernels and
 * re-emits GATE_16x16 with a real blt_MBps. See enable_new_mmio().
 *
 * v3/v4 (post iter-2/v2): v2 real-HW got further -- MMIO decoded (CR53 worked),
 * the engine was enabled (CR66=0x89) and RESPONDED to kicks (SUBSYS_STAT
 * 0x3000->0x1000->0x3000) -- but every BLT reported "STUCK". ROOT CAUSE: the
 * idle poll waited for FIFO_EMP=0x8 (bit 3), which is NEVER set in the real
 * ViRGE SUBSYS_STAT, so the watchdog always tripped even though the engine
 * completed. v6 carries the FULL sdl-engine source-verified fix set (vs
 * xf86-video-s3virge regs3v.h + s3v_accel.c):
 *   - idle = bare bit13 0x2000 (driver WaitIdle idiom; see s3_wait_idle), plus a
 *     busy-then-idle bracket in the timed path (s3_wait_done) so a post-kick
 *     stale idle cannot read ~0 elapsed -> inflated MB/s (the false-GREEN risk).
 *   - ROP = rop<<17 (bits[24:17]); the v2/v3 rop<<16 decoded to ROP 0x66
 *     (src XOR dest), NOT 0xCC SRCCOPY -- so the engine ran an XOR and the
 *     dst never matched (busy->idle but verify-fail). CMD_SET = 0x07980021.
 *   - RWIDTH_HEIGHT = (w-1)<<16 | RAW h (ViRGE quirk: width-1, height raw); the
 *     v2/v3 h-1 was one row short. width is still laddered as an oracle net.
 * The consult front-loaded these checks (no wasted real-HW round-trip); the
 * dst-readback verify backstops any residual issue (UNVERIF, never a false
 * number). With the fixes the verify should confirm correct DATA + real blt_MBps.
 *
 * The ViRGE analog of blttile.c (Cirrus). Standalone DJGPP probe; NO SDL,
 * NO engine, NO C++. Authored to docs/internal/S3-VIRGE-PERF-CAMPAIGN-PLAN.md
 * sec.2.3 P3 + sec.4.2 L2. NONE of the Cirrus SDL/0060 register code ports --
 * this is a clean re-baseline on different silicon.
 *
 * THE QUESTION (plan sec.4.2 L2): can the ViRGE/DX hardware 2D BitBLT engine
 * copy 16x16 TILE-sized rects VRAM->VRAM fast enough to beat the CPU colorkey-
 * blit path AND clear the ~40 MB/s 2D-lever bar -- the only render lever with a
 * path to the 17 ms sysmem composite and therefore to 50 fps. The Cirrus 2D
 * came back RED at 16x16 (8.3 MB/s NORMAL VRAM-rect; its 21.1 MB/s peak was at
 * 16x256 strip geometry = a TRAP, useless because tiles are 16x16). P3 must
 * clear the bar AT 16x16, not at a strip peak.
 *
 * HIGHEST-RISK PROBE IN THE SUITE -- direct MMIO register programming on
 * never-driven silicon. Runs LAST in the iter (a hang cannot lose the HWINV /
 * MEMBW data; fsync per line). HAZARD class: direct chip I/O + a freshly mapped
 * 4 MB-aperture LFB + a 64 KB MMIO window at LFB+0x1000000. Mitigations
 * (blttile.c harness model): bounded register writes, capped engine-idle
 * watchdog, VERIFY-before-trust on every measured BLT (readback byte-equal or
 * blt_MBps=UNVERIF -- a chip that runs but writes wrong data must NOT yield a
 * fake number), candidate-ladder for the genuinely uncertain register
 * conventions, atexit text-mode restore.
 *
 * Per [[dosbox_not_proxy]]: DOSBox-X does NOT model the ViRGE 2D engine -- the
 * DOSBox-X smoke is correctness-only (runs, exits, writes a parseable log; it
 * will almost certainly report NOT-S3 / skip the kernels under emulation). The
 * MB/s numbers are a g2k-with-ViRGE measurement ONLY.
 *
 * Per [[perf_measurement_discipline]]: predictions on this hardware have been
 * 5-30x wrong. The probe MEASURES; the prose names what the numbers resolve,
 * NOT what they will be. The Cirrus anchors below are plausibility bounds and
 * the lever bar, never asserted as ViRGE predictions.
 *
 * REGISTER PROVENANCE (this probe is the FIRST hardware round-trip; the plan
 * accepts that): MMIO base + 2D register offsets + CMD_SET bit encoding are
 * cross-checked from the 86Box vid_s3_virge.c emulator decode + the
 * xf86-video-s3virge regs3v.h driver. The two genuinely UNCERTAIN conventions
 * -- the engine-idle status bit and the rectangle width/height -1 convention --
 * are handled by forensic raw-register dumps + a candidate-ladder, NOT by a
 * single guess (the SDLPROBE / bltpat-v2-V7 false-close failure mode).
 *
 * REPORTS for sdl-engine's 4 MB-aperture map (plan sec.3.1 #1 risk + sec.3.3):
 * LFB PhysBasePtr, total VRAM, derived MMIO base -- VERIFIED by reading the
 * chip-ID THROUGH the MMIO window vs the direct CRTC read (candidates reported
 * on mismatch).
 *
 * MEASURES: K1 VRAM->VRAM BitBLT throughput (verified); K2 solid rect-fill
 * (verified; also the SDL back-surface clear primitive); K3 CPU colorkey A/B
 * baseline (the ratio the bar is judged on). K4 (transparent colorkey/ROP
 * candidate-ladder) is DEFERRED to iter 2 per team-lead -- not implemented here.
 *
 * Geometry sweep (plan sec.2.3): 16x16 (THE decision size), 16x32, 32x32
 * (overhead curve), 320x240 (backdrop / sdl-engine's blit case), 16x256 (the
 * labelled trap-reference -- the Cirrus strip-peak geometry, NOT a lever bar).
 *
 * Output: S3BLT.LOG (CWD), fallback C:\S3BLT.LOG. fsync per line.
 *
 * DOS constraints: -march=i486 -mtune=pentium, no MMX/SSE; size_t 32-bit on
 * DJGPP -- byte math in uint32_t / double. Pure DJGPP libc + DPMI.
 *
 * 8.3 DOS filename: S3BLT.EXE (5.3) -- fits. Log S3BLT.LOG.
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

/* ============================================================ */
/* Logging -- fopen-direct S3BLT.LOG + stdout mirror, fsync/line */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("S3BLT.LOG", "w");
    if (!g_log) g_log = fopen("C:\\S3BLT.LOG", "w");
}

static void slog(const char *fmt, ...)
{
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
        fsync(fileno(g_log));
    }
}

/* ============================================================ */
/* Timing -- RDTSC calibrated vs uclock (blttile pattern). The   */
/* ViRGE/DX target machine is a Pentium-class g2k -> RDTSC ok.   */
/* ============================================================ */

static double   g_us_per_cycle = 0.0;
static uint32_t g_cpu_mhz      = 0;

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static double cycles_to_us(uint64_t cycles)
{
    if (g_us_per_cycle <= 0.0) return -1.0;
    return (double)cycles * g_us_per_cycle;
}

static double now_secs(void) { return (double)uclock() / (double)UCLOCKS_PER_SEC; }

static void calibrate_rdtsc(void)
{
    double t0 = now_secs();
    uint64_t c0 = rdtsc();
    while (now_secs() - t0 < 0.100) { /* spin */ }
    double t1 = now_secs();
    uint64_t c1 = rdtsc();
    double secs = t1 - t0;
    if (secs <= 0.0) return;
    double cpu_hz = (double)(c1 - c0) / secs;
    g_us_per_cycle = 1e6 / cpu_hz;
    g_cpu_mhz = (uint32_t)(cpu_hz / 1e6);
}

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ============================================================ */
/* VGA / S3 CRTC port helpers                                     */
/* ============================================================ */

#define CRTC_IDX  0x3D4
#define CRTC_DATA 0x3D5

static uint8_t cr_read(uint8_t idx)  { outportb(CRTC_IDX, idx); return inportb(CRTC_DATA); }
static void    cr_write(uint8_t idx, uint8_t v) { outportb(CRTC_IDX, idx); outportb(CRTC_DATA, v); }

/* S3 chip-ID: unlock the extended CRTC regs (CR38=0x48 "register lock 1",
 * CR39=0xA5 "register lock 2"), then read CR2D (device id high), CR2E (device
 * id low), CR2F (revision). ViRGE/DX => CR2D=0x8A CR2E=0x01 (id 0x8A01). */
static void s3_unlock_crtc(void)
{
    cr_write(0x38, 0x48);
    cr_write(0x39, 0xA5);
}

/* ============================================================ */
/* PCI BIOS detect (INT 1Ah) -- reused from chipid.c pattern     */
/* ============================================================ */

#define S3_PCI_VENDOR  0x5333
#define VIRGE_DX_DEV   0x8A01

/* Find an S3 device by vendor 0x5333. Tries the ViRGE/DX device id first,
 * then a wildcard class scan fallback. Returns 1 + bus/devfunc on success. */
static int pci_find_s3(uint16_t *out_bus_devfunc, uint16_t *out_dev)
{
    __dpmi_regs r;
    /* AX=B102 find-device-by-id, DX=vendor, CX=device, SI=index. */
    static const uint16_t try_dev[] = { VIRGE_DX_DEV, 0x8A10, 0x5631, 0x883D, 0x8A01 };
    for (int i = 0; i < (int)(sizeof try_dev / sizeof try_dev[0]); i++) {
        memset(&r, 0, sizeof r);
        r.x.ax = 0xB102;
        r.x.cx = try_dev[i];
        r.x.dx = S3_PCI_VENDOR;
        r.x.si = 0;
        if (__dpmi_int(0x1A, &r) < 0) continue;
        if (r.h.ah == 0) {
            *out_bus_devfunc = (uint16_t)((r.h.bh << 8) | r.h.bl);
            *out_dev = try_dev[i];
            return 1;
        }
    }
    return 0;
}

static uint32_t pci_read_dword(uint16_t bus_devfunc, uint8_t off)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB10A;            /* read config dword */
    r.x.bx = bus_devfunc;
    r.x.di = off;
    if (__dpmi_int(0x1A, &r) < 0) return 0xFFFFFFFFul;
    return r.d.ecx;
}

/* ============================================================ */
/* VBE 8bpp-LFB mode finder (membw.c pattern) -- phys base +     */
/* total VRAM + chosen geometry. Returns mode (>0) or 0.         */
/* ============================================================ */

static uint16_t find_lfb_mode(uint32_t *out_phys, uint16_t *out_xres,
                              uint16_t *out_yres, uint32_t *out_total_vram,
                              int *out_vbe_major)
{
    *out_phys = 0; *out_xres = 0; *out_yres = 0;
    *out_total_vram = 0; *out_vbe_major = 0;

    int sel = 0;
    int seg = __dpmi_allocate_dos_memory(32, &sel);
    if (seg < 0) return 0;
    uint32_t buflin = (uint32_t)seg << 4;

    _farpokeb(_dos_ds, buflin + 0, 'V'); _farpokeb(_dos_ds, buflin + 1, 'B');
    _farpokeb(_dos_ds, buflin + 2, 'E'); _farpokeb(_dos_ds, buflin + 3, '2');
    for (int i = 4; i < 512; i++) _farpokeb(_dos_ds, buflin + i, 0);

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00; r.x.es = (uint16_t)seg; r.x.di = 0;
    if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) {
        __dpmi_free_dos_memory(sel); return 0;
    }
    uint16_t vbe_ver = _farpeekw(_dos_ds, buflin + 0x04);
    *out_vbe_major   = (vbe_ver >> 8) & 0xFF;
    uint16_t totblk  = _farpeekw(_dos_ds, buflin + 0x12);
    *out_total_vram  = (uint32_t)totblk * 65536u;
    uint16_t mp_off  = _farpeekw(_dos_ds, buflin + 0x0E);
    uint16_t mp_seg  = _farpeekw(_dos_ds, buflin + 0x10);
    uint32_t mode_lin = ((uint32_t)mp_seg << 4) + mp_off;

    uint16_t chosen = 0; uint32_t chosen_phys = 0;
    uint32_t chosen_pixels = 0xFFFFFFFFul; uint16_t cx = 0, cy = 0;

    for (int mi = 0; mi < 256; mi++) {
        uint16_t mode = _farpeekw(_dos_ds, mode_lin + mi * 2);
        if (mode == 0xFFFF) break;
        for (int i = 256; i < 512; i++) _farpokeb(_dos_ds, buflin + i, 0);
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F01; r.x.cx = mode; r.x.es = (uint16_t)seg; r.x.di = 256;
        if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) continue;
        uint32_t mb = buflin + 256;
        uint16_t attr = _farpeekw(_dos_ds, mb + 0x00);
        uint16_t xres = _farpeekw(_dos_ds, mb + 0x12);
        uint16_t yres = _farpeekw(_dos_ds, mb + 0x14);
        uint8_t  bpp  = _farpeekb(_dos_ds, mb + 0x19);
        uint8_t  model= _farpeekb(_dos_ds, mb + 0x1B);
        uint32_t phys = _farpeekl(_dos_ds, mb + 0x28);
        if (!(attr & 0x0001)) continue;   /* not supported */
        if (!(attr & 0x0080)) continue;   /* no LFB        */
        if (bpp != 8)         continue;
        if (model != 4)       continue;   /* packed pixel  */
        if (phys == 0)        continue;
        uint32_t pixels = (uint32_t)xres * (uint32_t)yres;
        if (pixels < 76800u)  continue;   /* must hold 320x240 back-surface */
        if (pixels < chosen_pixels) {
            chosen = mode; chosen_phys = phys; chosen_pixels = pixels;
            cx = xres; cy = yres;
        }
    }
    __dpmi_free_dos_memory(sel);
    if (chosen) { *out_phys = chosen_phys; *out_xres = cx; *out_yres = cy; }
    return chosen;
}

static int vbe_set_mode_lfb(uint16_t mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02; r.x.bx = mode | 0x4000;   /* LFB bit 14 */
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return (r.x.ax == 0x004F) ? 0 : -2;
}

static int g_text_restored = 0;
static void text_mode_restore(void)
{
    if (g_text_restored) return;
    g_text_restored = 1;
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
}

/* ============================================================ */
/* S3 ViRGE 2D BitBLT engine -- MMIO register map                */
/* (see /tmp/s3blt-regs-notes.txt provenance; CONFIDENT offsets) */
/* ============================================================ */

#define MMIO_OFFSET     0x01000000UL   /* new MMIO = LFB_base + 16 MB        */
#define MMIO_OLD_OFFSET 0x00800000UL   /* old MMIO = LFB_base + 8 MB (fallback) */
#define MMIO_SIZE       0x00010000UL   /* 64 KB register window              */

/* v2 ROOT-CAUSE FIX: iter S3-1 came back RED_MMIO_UNVERIFIED -- the new-MMIO
 * window at LFB+0x1000000 read 0xFFFFFFFF and every BLT was a no-op, because
 * this probe USED the window but never ENABLED it. On the S3 ViRGE you must
 * enable "new MMIO" via CR53 ("Extended Memory Control 1") AFTER unlocking the
 * extended CRTC regs (CR38=0x48 / CR39=0xA5). The enable is CR53 bit 3 (0x08)
 * per the S3 ViRGE datasheet + the common DOS S3 new-MMIO sequence -- but the
 * EXACT bit is treated as UNCERTAIN and resolved by a candidate ladder that is
 * gated on chip-ID-through-MMIO == the direct chip-ID (verify-before-trust:
 * a wrong CR53/base just fails to verify, never yields a false number; the
 * SDLPROBE / bltpat-v2-V7 false-close lesson). CR53 is OR'd onto its original
 * value (preserve the chip's other config bits) and restored at teardown. */
#define CR_MEM_CTL1     0x53           /* S3 Extended Memory Control 1        */
#define CR53_NEWMMIO    0x08           /* bit3: new-MMIO enable (lead candidate) */

#define R_SRC_BASE      0xA4D4
#define R_DEST_BASE     0xA4D8
#define R_CLIP_L_R      0xA4DC
#define R_CLIP_T_B      0xA4E0
#define R_DEST_SRC_STR  0xA4E4
#define R_MONO_PAT_0    0xA4E8
#define R_MONO_PAT_1    0xA4EC
#define R_PAT_BG_CLR    0xA4F0
#define R_PAT_FG_CLR    0xA4F4
#define R_SRC_BG_CLR    0xA4F8
#define R_SRC_FG_CLR    0xA4FC
#define R_CMD_SET       0xA500
#define R_RWIDTH_HEIGHT 0xA504
#define R_RSRC_XY       0xA508
#define R_RDEST_XY      0xA50C
#define R_SUBSYS_STAT   0x8504
#define R_MMIO_CRTC_IDX 0x83D4         /* VGA-thru-MMIO: CRTC index          */
#define R_MMIO_CRTC_DAT 0x83D5         /* VGA-thru-MMIO: CRTC data           */

/* CMD_SET bit encoding. */
#define CMD_AE          0x00000001UL   /* autoexecute (kick on RDEST_XY)     */
#define CMD_FORMAT_8    0x00000000UL   /* dest 8bpp (bits 2-4 = 0)           */
#define CMD_DRAW        0x00000020UL   /* draw enable (bit 5)                */
#define CMD_XP          0x02000000UL   /* x positive  (bit 25)               */
#define CMD_YP          0x04000000UL   /* y positive  (bit 26)               */
#define CMD_BITBLT      0x00000000UL   /* command bits 27-30 = 0             */
#define CMD_RECT        0x10000100UL   /* (0x2<<27) | 0x100 mono-pattern     */
#define ROP_SRCCOPY     0xCC
#define ROP_PATCOPY     0xF0
#define ROP_SHIFT       17             /* ROP = bits[24:17] (rop<<17). iter-4 fix:
                                        * v2/v3 used <<16 (wrong ROP alignment) --
                                        * sdl-engine consult caught it vs s3v_accel.
                                        * 0xCC<<17 = 0x01980000 (was 0x00CC0000). */

/* SUBSYS_STAT (0x8504) S3D 2D-engine idle predicate. iter-3 found the idle bit
 * EMPIRICALLY on the g2k ViRGE/DX (v2 real-HW: idle SUBSYS_STAT=0x3000,
 * busy=0x1000, kick transition 0x3000->0x1000->0x3000). The v1/v2 guess
 * FIFO_EMP=0x8 (bit 3) was WRONG -- bit 3 is never set in the real ViRGE
 * SUBSYS_STAT, so every wait spun to the watchdog and falsely reported "STUCK".
 * iter-5/v6 uses the DRIVER IDIOM, source-verified by sdl-engine vs
 * xf86-video-s3virge regs3v.h: WaitIdle() = `while(!(SUBSYS_STAT & 0x2000))` --
 * bare bit13 set = engine idle. (The masked (& 0x3f00)==0x3000 form is NOT the
 * driver idiom -- dropped; the 0x20002000 bit13+bit29 form is TRIO_3D, not
 * ViRGE.) For the TIMED loop, a bare idle poll RIGHT AFTER the kick can read a
 * STALE idle=1 before the engine asserts busy -> ~0 elapsed -> inflated MB/s
 * (a false-GREEN risk). s3_wait_done() brackets each timed BLT busy-THEN-idle
 * so the timer captures the true duration. */
#define S3D_IDLE_BIT    0x2000         /* SUBSYS_STAT bit13: 1 = S3D engine idle */
#define BUSY_OBSERVE_CAP 256L          /* small cap to observe the busy edge;    */
                                       /* falls through (sub-us BLT) -- the g2k  */
                                       /* v2 forensics show busy at post-kick r0 */
#define WATCHDOG_SPINS  500000L        /* idle-poll cap: ~60x margin vs a    */
                                       /* 320x240 BLT @10 MB/s on PODP83;    */
                                       /* bounds a genuinely-stuck engine    */

/* 8bpp SRCCOPY screen-to-screen BitBLT command word. */
#define CMD_BLT_8BPP \
    (CMD_BITBLT | CMD_AE | CMD_DRAW | CMD_FORMAT_8 | CMD_XP | CMD_YP | \
     ((uint32_t)ROP_SRCCOPY << ROP_SHIFT))

/* 8bpp solid rectangle-fill command word (mono pattern all-ones, PATCOPY). */
#define CMD_FILL_8BPP \
    (CMD_RECT | CMD_AE | CMD_DRAW | CMD_FORMAT_8 | CMD_XP | CMD_YP | \
     ((uint32_t)ROP_PATCOPY << ROP_SHIFT))

static volatile uint8_t *g_mmio = NULL;    /* nearptr to MMIO window         */
static volatile uint8_t *g_lfb  = NULL;    /* nearptr to LFB (VRAM)          */

static inline void mmio_w32(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(g_mmio + off) = v;
}
static inline uint32_t mmio_r32(uint32_t off)
{
    return *(volatile uint32_t *)(g_mmio + off);
}
static inline void mmio_w8(uint32_t off, uint8_t v)
{
    *(volatile uint8_t *)(g_mmio + off) = v;
}
static inline uint8_t mmio_r8(uint32_t off)
{
    return *(volatile uint8_t *)(g_mmio + off);
}

/* LFB byte access for src prefill + dst readback (the verify oracle). */
static inline void lfb_wb(uint32_t off, uint8_t v) { g_lfb[off] = v; }
static inline uint8_t lfb_rb(uint32_t off)         { return g_lfb[off]; }

static void lfb_fill(uint32_t off, uint8_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) g_lfb[off + i] = v;
}
static void lfb_gradient(uint32_t off, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) g_lfb[off + i] = (uint8_t)(i & 0xFF);
}

/* Wait for the S3D 2D engine idle (bare bit13 set -- driver WaitIdle idiom),
 * capped. Returns 0 idle, -1 stuck. Used for pre-program serialization. */
static int s3_wait_idle(void)
{
    for (long spin = 0; spin < WATCHDOG_SPINS; spin++) {
        if (mmio_r32(R_SUBSYS_STAT) & S3D_IDLE_BIT) return 0;
    }
    return -1;
}

/* Race-proof completion wait for the TIMED path: after a kick, first observe
 * the engine BUSY (bit13 clear), THEN wait for idle, so a post-kick STALE idle
 * can't read ~0 elapsed (the false-GREEN / inflated-MB/s risk team-lead + sdl-
 * engine flagged). The busy-observe is small-capped + falls through: the g2k v2
 * forensics show busy at the first post-kick read, so it is observable; the cap
 * just bounds a sub-microsecond BLT that finishes before we catch the edge.
 * Returns 0 ok, -1 idle never reasserted (genuinely stuck). */
static int s3_wait_done(void)
{
    long s;
    for (s = 0; s < BUSY_OBSERVE_CAP; s++)                 /* wait until busy   */
        if ((mmio_r32(R_SUBSYS_STAT) & S3D_IDLE_BIT) == 0) break;
    for (s = 0; s < WATCHDOG_SPINS; s++)                   /* wait until idle   */
        if (mmio_r32(R_SUBSYS_STAT) & S3D_IDLE_BIT) return 0;
    return -1;
}

/* Program + kick one screen-to-screen BitBLT. RWIDTH_HEIGHT on the ViRGE is
 * width-1 in the high half but HEIGHT RAW in the low half (the S3 quirk
 * sdl-engine flagged for iter-4; v2/v3 wrongly used h-1). w_m1 ladders only the
 * WIDTH convention (1 => w-1, 0 => w) as an oracle safety net; height is always
 * raw h. stride=packed=w. Returns 0 ok, -1 engine stuck. */
static int s3_blt_copy(uint32_t dst_off, uint32_t src_off, int w, int h,
                       int w_m1)
{
    if (s3_wait_idle() != 0) return -1;
    uint16_t ww = (uint16_t)(w_m1 ? (w - 1) : w);
    uint16_t hh = (uint16_t)h;                  /* RAW height (ViRGE quirk)   */
    uint32_t stride = (uint32_t)w;             /* packed-tile model          */

    mmio_w32(R_SRC_BASE,      src_off);
    mmio_w32(R_DEST_BASE,     dst_off);
    mmio_w32(R_CLIP_L_R,      (0u << 16) | 0x07FFu);
    mmio_w32(R_CLIP_T_B,      (0u << 16) | 0x07FFu);
    mmio_w32(R_DEST_SRC_STR,  (stride << 16) | stride);
    mmio_w32(R_CMD_SET,       CMD_BLT_8BPP);
    mmio_w32(R_RWIDTH_HEIGHT, ((uint32_t)ww << 16) | hh);
    mmio_w32(R_RSRC_XY,       0u);
    mmio_w32(R_RDEST_XY,      0u);             /* last write -> autoexecute   */
    if (s3_wait_done() != 0) return -1;        /* busy-then-idle (timed-safe) */
    return 0;
}

/* Program + kick one solid rectangle fill at dst_off, color val. Same
 * RWIDTH_HEIGHT convention as the BLT: width per w_m1, height RAW h. */
static int s3_rect_fill(uint32_t dst_off, int w, int h, uint8_t val, int w_m1)
{
    if (s3_wait_idle() != 0) return -1;
    uint16_t ww = (uint16_t)(w_m1 ? (w - 1) : w);
    uint16_t hh = (uint16_t)h;                  /* RAW height (ViRGE quirk)   */
    uint32_t stride = (uint32_t)w;
    uint32_t color = (uint32_t)val;
    color |= color << 8; color |= color << 16;  /* replicate to all bytes    */

    mmio_w32(R_DEST_BASE,     dst_off);
    mmio_w32(R_CLIP_L_R,      (0u << 16) | 0x07FFu);
    mmio_w32(R_CLIP_T_B,      (0u << 16) | 0x07FFu);
    mmio_w32(R_DEST_SRC_STR,  (stride << 16) | stride);
    mmio_w32(R_MONO_PAT_0,    0xFFFFFFFFul);   /* all-ones -> solid fill      */
    mmio_w32(R_MONO_PAT_1,    0xFFFFFFFFul);
    mmio_w32(R_PAT_FG_CLR,    color);
    mmio_w32(R_PAT_BG_CLR,    0);
    mmio_w32(R_CMD_SET,       CMD_FILL_8BPP);
    mmio_w32(R_RWIDTH_HEIGHT, ((uint32_t)ww << 16) | hh);
    mmio_w32(R_RDEST_XY,      0u);             /* kick                        */
    if (s3_wait_done() != 0) return -1;        /* busy-then-idle (timed-safe) */
    return 0;
}

/* ============================================================ */
/* Geometry sweep + VRAM layout + anchors                        */
/* ============================================================ */

#define GEOM_COUNT 5
static const struct { const char *name; int w; int h; int is_trap; } g_geom[GEOM_COUNT] = {
    { "16x16",   16,  16,  0 },   /* THE decision size  -> 256 B    */
    { "16x32",   16,  32,  0 },   /* overhead curve     -> 512 B    */
    { "32x32",   32,  32,  0 },   /* overhead curve     -> 1024 B   */
    { "320x240", 320, 240, 0 },   /* backdrop blit case -> 76800 B  */
    { "16x256",  16,  256, 1 },   /* TRAP-reference strip -> 4096 B */
};

#define VRAM_DST_OFF   0x00000000UL    /* visible region (top-left)          */
#define VRAM_SRC_OFF   0x00100000UL    /* off-screen src (1 MB into VRAM)     */
#define CPU_COLORKEY   0xFF

#define ROUNDS         9
#define BATCH_SECS     0.012
#define CALIB_ITERS    32

/* Cross-anchors (plan sec.2.2) -- plausibility bounds + the lever bar.
 * NEVER asserted as ViRGE predictions ([[perf_measurement_discipline]]). */
#define CIR_CPU_BLT_16x16     15.7     /* CPU colorkey @16x16 (the bar base) */
#define CIR_BLT_16x256_PEAK   21.1     /* Cirrus strip peak = the TRAP        */
#define CIR_2D_LEVER_BAR      40.0     /* GREEN threshold @16x16              */
#define PLAUS_LO_MBS           0.5
#define PLAUS_HI_MBS        2000.0     /* ViRGE EDO+PCI may exceed Cirrus     */

static int g_warn = 0;

static void plaus_check(const char *what, const char *geom, double mbs)
{
    if (mbs < PLAUS_LO_MBS || mbs > PLAUS_HI_MBS) {
        g_warn++;
        slog("S3BLT-WARN %s geom=%s = %.2f MB/s OUTSIDE plausible window "
             "[%.1f,%.1f] -- emit suspect", what, geom, mbs,
             PLAUS_LO_MBS, PLAUS_HI_MBS);
    }
}

/* ============================================================ */
/* K1 -- VRAM->VRAM BitBLT verify + throughput                   */
/* ============================================================ */

/* Verify one geometry: prefill src gradient, sentinel-clear dst, BLT, read
 * dst back. Height is always RAW h (ViRGE quirk); the ladder tries the WIDTH
 * convention (1 => w-1, 0 => w). Returns the width convention that produced a
 * byte-correct copy (1 or 0), or -1 if neither verified. */
static int k1_verify(int gi)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    uint32_t bytes = (uint32_t)w * (uint32_t)h;

    lfb_gradient(VRAM_SRC_OFF, bytes);
    /* self-check the off-screen src is RAM-backed before trusting any BLT. */
    for (uint32_t i = 0; i < bytes; i += (bytes > 64 ? bytes / 64 : 1)) {
        if (lfb_rb(VRAM_SRC_OFF + i) != (uint8_t)(i & 0xFF)) {
            g_warn++;
            slog("  verify geom=%s: src VRAM @0x%06lX NOT readback-intact "
                 "(off 0x%lX got 0x%02X want 0x%02X) -- VRAM too small/banking?",
                 g_geom[gi].name, (unsigned long)VRAM_SRC_OFF,
                 (unsigned long)i, lfb_rb(VRAM_SRC_OFF + i), (uint8_t)(i & 0xFF));
            return -1;
        }
    }

    for (int conv = 1; conv >= 0; conv--) {     /* width: try (w-1,h) then (w,h) */
        lfb_fill(VRAM_DST_OFF, 0xCC, bytes);    /* sentinel                   */
        int rc = s3_blt_copy(VRAM_DST_OFF, VRAM_SRC_OFF, w, h, conv);
        if (rc != 0) {
            slog("  verify geom=%s conv=%s: BLT STUCK (engine watchdog tripped)",
                 g_geom[gi].name, conv ? "w-1,h" : "w,h");
            continue;
        }
        int exact = 1, untouched = 1;
        for (uint32_t i = 0; i < bytes; i++) {
            uint8_t b = lfb_rb(VRAM_DST_OFF + i);
            if (b != 0xCC)              untouched = 0;
            if (b != (uint8_t)(i & 0xFF)) exact = 0;
        }
        if (exact) {
            slog("  verify geom=%s: OK conv=%s (dst==src gradient, %lu B)",
                 g_geom[gi].name, conv ? "w-1,h(raw)" : "w,h(raw)",
                 (unsigned long)bytes);
            return conv;
        }
        slog("  verify geom=%s conv=%s: %s (dst[0..7]=%02X %02X %02X %02X "
             "%02X %02X %02X %02X)", g_geom[gi].name, conv ? "w-1,h" : "w,h",
             untouched ? "NO-OP wrote nothing" : "WRONG data",
             lfb_rb(VRAM_DST_OFF+0), lfb_rb(VRAM_DST_OFF+1),
             lfb_rb(VRAM_DST_OFF+2), lfb_rb(VRAM_DST_OFF+3),
             lfb_rb(VRAM_DST_OFF+4), lfb_rb(VRAM_DST_OFF+5),
             lfb_rb(VRAM_DST_OFF+6), lfb_rb(VRAM_DST_OFF+7));
    }
    return -1;
}

static void k1_throughput(int gi, int conv, double *med, double *lo, double *hi,
                          long *itp)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    double bytes = (double)w * (double)h;
    double samp[ROUNDS];
    long N;

    {
        uint64_t c0 = rdtsc();
        for (int i = 0; i < CALIB_ITERS; i++)
            s3_blt_copy(VRAM_DST_OFF, VRAM_SRC_OFF, w, h, conv);
        double us = cycles_to_us(rdtsc() - c0);
        double per = us / (double)CALIB_ITERS;
        if (per <= 0.0) per = 1.0;
        N = (long)((BATCH_SECS * 1e6) / per) + 1;
        if (N < 8) N = 8;
    }
    *itp = N;
    for (int r = 0; r < ROUNDS; r++) {
        uint64_t c0 = rdtsc();
        for (long i = 0; i < N; i++)
            s3_blt_copy(VRAM_DST_OFF, VRAM_SRC_OFF, w, h, conv);
        double secs = cycles_to_us(rdtsc() - c0) / 1e6;
        samp[r] = (secs > 0.0) ? ((double)N * bytes) / (secs * 1048576.0) : 0.0;
    }
    qsort(samp, ROUNDS, sizeof samp[0], dbl_cmp);
    *med = samp[ROUNDS / 2]; *lo = samp[0]; *hi = samp[ROUNDS - 1];
}

/* ============================================================ */
/* K2 -- solid rect-fill verify + throughput (SDL clear prim)    */
/* ============================================================ */

static int k2_fill_verify(int gi, int conv)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    uint32_t bytes = (uint32_t)w * (uint32_t)h;
    lfb_fill(VRAM_DST_OFF, 0xCC, bytes);
    int rc = s3_rect_fill(VRAM_DST_OFF, w, h, 0x5A, conv);
    if (rc != 0) { slog("  fill verify geom=%s: STUCK", g_geom[gi].name); return 0; }
    int ok = 1;
    for (uint32_t i = 0; i < bytes; i++)
        if (lfb_rb(VRAM_DST_OFF + i) != 0x5A) { ok = 0; break; }
    slog("  fill verify geom=%s: %s (want 0x5A, dst[0..3]=%02X %02X %02X %02X)",
         g_geom[gi].name, ok ? "OK" : "FAIL",
         lfb_rb(VRAM_DST_OFF+0), lfb_rb(VRAM_DST_OFF+1),
         lfb_rb(VRAM_DST_OFF+2), lfb_rb(VRAM_DST_OFF+3));
    return ok;
}

static void k2_fill_throughput(int gi, int conv, double *med)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    double bytes = (double)w * (double)h;
    double samp[ROUNDS];
    long N;
    {
        uint64_t c0 = rdtsc();
        for (int i = 0; i < CALIB_ITERS; i++)
            s3_rect_fill(VRAM_DST_OFF, w, h, 0x5A, conv);
        double us = cycles_to_us(rdtsc() - c0);
        double per = us / (double)CALIB_ITERS;
        if (per <= 0.0) per = 1.0;
        N = (long)((BATCH_SECS * 1e6) / per) + 1;
        if (N < 8) N = 8;
    }
    for (int r = 0; r < ROUNDS; r++) {
        uint64_t c0 = rdtsc();
        for (long i = 0; i < N; i++) s3_rect_fill(VRAM_DST_OFF, w, h, 0x5A, conv);
        double secs = cycles_to_us(rdtsc() - c0) / 1e6;
        samp[r] = (secs > 0.0) ? ((double)N * bytes) / (secs * 1048576.0) : 0.0;
    }
    qsort(samp, ROUNDS, sizeof samp[0], dbl_cmp);
    *med = samp[ROUNDS / 2];
}

/* ============================================================ */
/* K3 -- CPU sysmem colorkey blit A/B (the _blit_indexed class)  */
/* ============================================================ */

static void cpu_colorkey_tile(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t px = src[i];
        if (px != CPU_COLORKEY) dst[i] = px;
    }
}

static double k3_cpu_throughput(int gi)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    uint32_t n = (uint32_t)w * (uint32_t)h;
    double bytes = (double)w * (double)h;
    double samp[ROUNDS];
    long N;
    uint8_t *src = (uint8_t *)malloc(n);
    uint8_t *dst = (uint8_t *)malloc(n);
    if (!src || !dst) { free(src); free(dst); g_warn++; return -1.0; }
    for (uint32_t i = 0; i < n; i++) { src[i] = (uint8_t)(i & 0xFF); dst[i] = 0; }

    cpu_colorkey_tile(dst, src, n);
    {
        uint64_t c0 = rdtsc();
        for (int i = 0; i < CALIB_ITERS; i++) cpu_colorkey_tile(dst, src, n);
        double us = cycles_to_us(rdtsc() - c0);
        double per = us / (double)CALIB_ITERS;
        if (per <= 0.0) per = 1.0;
        N = (long)((BATCH_SECS * 1e6) / per) + 1;
        if (N < 8) N = 8;
    }
    for (int r = 0; r < ROUNDS; r++) {
        uint64_t c0 = rdtsc();
        for (long i = 0; i < N; i++) cpu_colorkey_tile(dst, src, n);
        double secs = cycles_to_us(rdtsc() - c0) / 1e6;
        samp[r] = (secs > 0.0) ? ((double)N * bytes) / (secs * 1048576.0) : 0.0;
    }
    qsort(samp, ROUNDS, sizeof samp[0], dbl_cmp);
    free(src); free(dst);
    return samp[ROUNDS / 2];
}

/* ============================================================ */
/* ViRGE new-MMIO enable (v2 fix) + MMIO verification            */
/* ============================================================ */

/* Enable the ViRGE "new MMIO" register window at LFB+0x1000000.
 *
 * iter-1 came back RED_MMIO_UNVERIFIED: the window read 0xFF everywhere
 * (chip-ID-thru-MMIO=0xFFFF, SUBSYS_STAT=0xFFFFFFFF, all BLTs no-op). ROOT
 * CAUSE: the VBE BIOS brings up a functional 8bpp enhanced mode (so the 2D
 * engine is set up) but a VBE app has no reason to expose the MMIO REGISTER
 * window, so CR53 bit 3 stays clear and the window does not decode. Per
 * xf86-video-s3virge (s3v_driver.c): `CR53 = 0x08` enables MMIO. We re-unlock
 * the extended CRTC regs (the 4F02 mode set may have re-locked them), RMW CR53
 * |= 0x08 (preserving BIOS bits), and dump the engine-state regs CR66/CR31/CR40
 * before the write -- so IF the engine itself is disabled (CR66) and BLTs still
 * no-op after the window decodes, iter-3 is data-driven, not speculative. We do
 * NOT speculatively overwrite CR66/CR31 here: the VBE enhanced mode is already
 * functional, so clobbering its engine regs risks breaking the working mode.
 * Returns the CR53 readback after the write. */
static uint8_t enable_new_mmio(void)
{
    s3_unlock_crtc();                          /* CR38=0x48, CR39=0xA5 re-unlock */
    uint8_t cr53b = cr_read(0x53);
    uint8_t cr66  = cr_read(0x66);
    uint8_t cr31  = cr_read(0x31);
    uint8_t cr40  = cr_read(0x40);
    cr_write(0x53, (uint8_t)(cr53b | 0x08));   /* new-MMIO enable = CR53 bit 3   */
    uint8_t cr53a = cr_read(0x53);
    slog("[s3blt] MMIO_ENABLE CR53 0x%02X->0x%02X (RMW |0x08 new-MMIO bit3) "
         "engine-state CR66=0x%02X CR31=0x%02X CR40=0x%02X (BIOS-set; if the "
         "window decodes but BLTs no-op, CR66 engine-enable is the iter-3 suspect)",
         cr53b, cr53a, cr66, cr31, cr40);
    return cr53a;
}

/* (re-verify uses the externally-scaffolded mmio_chipid_thru() + map_mmio_window()
 * helpers defined below, wired into main's fallback ladder.) */

/* MMIO diagnostics (NOT a hard gate -- both checks can false-negative on real
 * HW, so neither blocks the kernels; the BLT readback-verify is the sole oracle):
 *   (a) scratch-register write/readback. INFORMATIONAL only -- many 2D-engine
 *       registers are WRITE-ONLY on real silicon, so a 0xFF readback does NOT
 *       prove the window is dead (gating on this would false-skip a working
 *       engine -- the trap the DOSBox-X forced run exposed).
 *   (b) chip-ID THROUGH the MMIO VGA window vs the direct CRTC port read. A
 *       match confirms the derived MMIO base (LFB+0x1000000) is correct. This
 *       is the better base-correctness signal, but VGA-thru-MMIO may need a
 *       config bit on some boards, so a NO is also not conclusive.
 * Returns the chip-ID-thru-MMIO match (base_verified) -- used ONLY to make the
 * final RED verdict more actionable (wrong-base vs slow-engine), never to gate. */
static int mmio_verify(uint8_t crtc_2d, uint8_t crtc_2e)
{
    slog("---- MMIO diagnostics (base = LFB + 0x%08lX; NOT a gate) ----",
         (unsigned long)MMIO_OFFSET);

    /* (a) scratch reg: SRC_FG_CLR is unused by our BLT path. Informational. */
    uint32_t sentinel = 0xA5C30F12ul;
    mmio_w32(R_SRC_FG_CLR, sentinel);
    uint32_t rb = mmio_r32(R_SRC_FG_CLR);
    slog("  MMIO_SCRATCH readback=0x%08lX (wrote 0x%08lX) -- %s (INFO ONLY: 2D "
         "regs may be write-only, so a mismatch does NOT prove a dead window)",
         (unsigned long)rb, (unsigned long)sentinel,
         (rb == sentinel) ? "matched (readable)" : "mismatch (write-only or no-decode)");

    /* (b) chip-ID through MMIO VGA window -- the base-correctness signal. */
    mmio_w8(R_MMIO_CRTC_IDX, 0x2D);
    uint8_t m2d = mmio_r8(R_MMIO_CRTC_DAT);
    mmio_w8(R_MMIO_CRTC_IDX, 0x2E);
    uint8_t m2e = mmio_r8(R_MMIO_CRTC_DAT);
    int base_verified = (m2d == crtc_2d && m2e == crtc_2e);
    if (base_verified) {
        slog("  MMIO_BASE_VERIFIED=YES (chip-ID via MMIO 0x83D4/5 = 0x%02X%02X "
             "== direct CRTC 0x%02X%02X -> base LFB+0x1000000 confirmed)",
             m2d, m2e, crtc_2d, crtc_2e);
    } else {
        slog("  MMIO_BASE_VERIFIED=NO (chip-ID via MMIO = 0x%02X%02X vs direct "
             "CRTC 0x%02X%02X). VGA-thru-MMIO may be config-disabled, or the base "
             "is wrong. sdl-engine candidates: LFB+0x1000000 (used), LFB+0x800000 "
             "(old-MMIO). Kernels still run -- BLT readback-verify is the oracle.",
             m2d, m2e, crtc_2d, crtc_2e);
    }
    return base_verified;
}

/* v2 -- map a MMIO_SIZE window at physical `base`, set g_mmio. 0 ok / -1 fail.
 * nearptr must already be enabled (it is -- LFB map enables it once). */
static int map_mmio_window(uint32_t base, __dpmi_meminfo *out_map)
{
    memset(out_map, 0, sizeof *out_map);
    out_map->address = base;
    out_map->size    = MMIO_SIZE;
    if (__dpmi_physical_address_mapping(out_map) != 0) return -1;
    g_mmio = (volatile uint8_t *)(out_map->address + __djgpp_conventional_base);
    return 0;
}

/* v2 -- read the chip-ID (CR2D/CR2E) THROUGH the current g_mmio VGA-thru-MMIO
 * window. A match vs the direct-port chip-ID is the base+CR53-correct oracle. */
static void mmio_chipid_thru(uint8_t *out_2d, uint8_t *out_2e)
{
    mmio_w8(R_MMIO_CRTC_IDX, 0x2D); *out_2d = mmio_r8(R_MMIO_CRTC_DAT);
    mmio_w8(R_MMIO_CRTC_IDX, 0x2E); *out_2e = mmio_r8(R_MMIO_CRTC_DAT);
}

/* Forensic dump of SUBSYS_STAT raw across a kick -- confirms the idle bit
 * (stat & 0x2000) toggles each run. v2 real-HW showed before=0x3000 (idle),
 * postkick=0x1000 (busy, bit13 clear), after=0x3000 (idle) -- the basis of the
 * bare-bit13 idle fix. Also sanity-checks the bit reasserts after the kick. */
static void s3_status_forensics(void)
{
    slog("---- SUBSYS_STAT forensics (confirm idle bit 0x2000 toggles) ----");
    uint32_t before = mmio_r32(R_SUBSYS_STAT);
    /* kick a 320x240 BLT (long enough that we may catch a busy state). */
    uint16_t w = 320, h = 240, stride = 320;
    mmio_w32(R_SRC_BASE, VRAM_SRC_OFF);
    mmio_w32(R_DEST_BASE, VRAM_DST_OFF);
    mmio_w32(R_CLIP_L_R, (0u << 16) | 0x07FFu);
    mmio_w32(R_CLIP_T_B, (0u << 16) | 0x07FFu);
    mmio_w32(R_DEST_SRC_STR, ((uint32_t)stride << 16) | stride);
    mmio_w32(R_CMD_SET, CMD_BLT_8BPP);
    mmio_w32(R_RWIDTH_HEIGHT, ((uint32_t)(w - 1) << 16) | (h - 1));
    mmio_w32(R_RSRC_XY, 0u);
    mmio_w32(R_RDEST_XY, 0u);              /* kick */
    uint32_t s0 = mmio_r32(R_SUBSYS_STAT);
    uint32_t s1 = mmio_r32(R_SUBSYS_STAT);
    uint32_t s2 = mmio_r32(R_SUBSYS_STAT);
    long settle = 0;
    while ((mmio_r32(R_SUBSYS_STAT) & S3D_IDLE_BIT) == 0 && settle < WATCHDOG_SPINS)
        settle++;
    uint32_t after = mmio_r32(R_SUBSYS_STAT);
    slog("  SUBSYS_STAT before=0x%08lX postkick=[0x%08lX 0x%08lX 0x%08lX] "
         "after=0x%08lX settle_spins=%ld IDLE_pred=(stat&0x%X)!=0",
         (unsigned long)before, (unsigned long)s0, (unsigned long)s1,
         (unsigned long)s2, (unsigned long)after, settle, S3D_IDLE_BIT);
    if ((after & S3D_IDLE_BIT) == 0) {
        g_warn++;
        slog("  S3BLT-WARN engine did NOT return to idle (bit 0x2000) within the "
             "watchdog after a kick -- if BLTs also STUCK, the idle bit or the "
             "command sequence is wrong; cross-check the raw values above.");
    } else if (s0 == before && s1 == before && s2 == before && settle == 0) {
        g_warn++;
        slog("  S3BLT-WARN SUBSYS_STAT never showed busy across a kick -- the "
             "engine may not have run; throughput timings suspect. flush-instr: "
             "cross-check vs MEMBW LFB rate.");
    } else {
        slog("  S3BLT-OK idle bit toggled correctly across the kick "
             "(idle->busy->idle); s3_wait_idle will gate on completion.");
    }
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

/* FORCE: bypass the S3-ViRGE detection gate. Smoke/diagnostic ONLY -- lets the
 * DOSBox-X correctness smoke exercise the high-risk mapping + MMIO-decode-gate
 * + teardown path (which the detection gate would otherwise skip on non-ViRGE
 * silicon). The real g2k+ViRGE iter runs S3BLT.EXE with NO args (auto-detect).
 * Any numbers from a forced run on non-ViRGE silicon are NOT a real measurement. */
static int arg_is_force(const char *a)
{
    return a && (!strcmp(a, "force") || !strcmp(a, "FORCE") ||
                 !strcmp(a, "-f") || !strcmp(a, "-F") || !strcmp(a, "--force"));
}

int main(int argc, char **argv)
{
    int g_force = 0;
    for (int i = 1; i < argc; i++) if (arg_is_force(argv[i])) g_force = 1;

    open_log();
    slog("=== S3BLT -- S3 ViRGE/DX 2D BitBLT engine probe (campaign P3) ===");
    slog("S3BLT-BEGIN");
    slog("version: v6 (sdl-engine source-verified: (a) idle = bare bit13 0x2000 "
         "[driver WaitIdle idiom] + busy-then-idle timed bracket [anti false-GREEN]; "
         "(b) ROP <<17 [bits24:17] = CMD_SET 0x07980021, was 0x06CC0021/ROP-0x66-XOR; "
         "(c) RWIDTH_HEIGHT = (w-1)<<16 | RAW h, was h-1. v2 CR53|=0x08 new-MMIO "
         "enable + fallbacks retained. dst-readback oracle backstops all.)");
    slog("plan: docs/internal/S3-VIRGE-PERF-CAMPAIGN-PLAN.md sec.2.3 P3 + 4.2 L2");
    slog("build: DJGPP -march=i486 -mtune=pentium -O2, no MMX/SSE");
    slog("HAZARD: direct MMIO on the ViRGE 2D engine; verify-before-trust +");
    slog("        capped watchdog + atexit text restore. DOSBox-X cannot model");
    slog("        this engine -- emulator run = correctness/no-crash only.");
    slog("UCLOCKS_PER_SEC=%lu", (unsigned long)UCLOCKS_PER_SEC);

    atexit(text_mode_restore);

    calibrate_rdtsc();
    slog("rdtsc: cpu_mhz=%u us_per_cycle=%.6f", (unsigned)g_cpu_mhz, g_us_per_cycle);
    if (g_us_per_cycle <= 0.0) {
        slog("FATAL: RDTSC calibration failed -- cannot time.");
        slog("[s3blt SUITE_DONE verdict=ABORT_NO_TIMER]");
        slog("S3BLT-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }

    /* ---- PCI + CRTC detect (report raw both ways) ---- */
    uint16_t bdf = 0, pci_dev = 0;
    int have_pci = pci_find_s3(&bdf, &pci_dev);
    uint32_t vendev = have_pci ? pci_read_dword(bdf, 0x00) : 0xFFFFFFFFul;
    uint32_t bar0   = have_pci ? pci_read_dword(bdf, 0x10) : 0xFFFFFFFFul;
    if (have_pci) {
        slog("PCI: found S3 at bus_devfunc=0x%04X vendor:device=0x%04lX:0x%04lX "
             "BAR0=0x%08lX", bdf,
             (unsigned long)(vendev & 0xFFFF),
             (unsigned long)((vendev >> 16) & 0xFFFF),
             (unsigned long)bar0);
    } else {
        slog("PCI: no S3 (vendor 0x5333) device found via PCI BIOS scan");
    }

    s3_unlock_crtc();
    uint8_t c2d = cr_read(0x2D), c2e = cr_read(0x2E), c2f = cr_read(0x2F);
    slog("CRTC chip-ID: CR2D=0x%02X CR2E=0x%02X (id=0x%02X%02X) CR2F(rev)=0x%02X",
         c2d, c2e, c2d, c2e, c2f);
    int is_virge = (have_pci && (vendev & 0xFFFF) == S3_PCI_VENDOR) ||
                   (c2d == 0x8A) /* ViRGE/DX/GX id high */;
    slog("force=%d is_virge=%d", g_force, is_virge);
    if (!is_virge && !g_force) {
        slog("S3BLT-WARN no S3 ViRGE signature (PCI vendor != 0x5333 AND "
             "CR2D != 0x8A) -- skipping all 2D kernels (chip-specific probe; "
             "expected under DOSBox-X). Pass FORCE to exercise the mapping path.");
        slog("[s3blt SUITE_DONE verdict=SKIP_NOT_S3_VIRGE]");
        slog("S3BLT-DONE");
        if (g_log) fclose(g_log);
        return 0;
    }
    if (!is_virge && g_force) {
        slog("S3BLT-FORCE proceeding past detection (CR2D=0x%02X != 0x8A) for "
             "mapping-path smoke coverage -- any numbers below are NOT a real "
             "ViRGE measurement.", c2d);
    }

    /* ---- VBE 8bpp LFB mode ---- */
    uint32_t phys = 0, total_vram = 0; uint16_t xres = 0, yres = 0; int vbemaj = 0;
    uint16_t mode = find_lfb_mode(&phys, &xres, &yres, &total_vram, &vbemaj);
    if (mode == 0) {
        slog("FATAL: no 8bpp linear-framebuffer VBE mode (vbe_major=%d)", vbemaj);
        slog("[s3blt SUITE_DONE verdict=ABORT_NO_LFB_MODE]");
        slog("S3BLT-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }
    slog("LFB mode=0x%04X %ux%u 8bpp phys=0x%08lX total_vram=%lu vbe_major=%d",
         mode, xres, yres, (unsigned long)phys, (unsigned long)total_vram, vbemaj);
    slog("REPORT(sdl-engine): LFB_PHYS=0x%08lX TOTAL_VRAM=%lu MMIO_BASE=0x%08lX",
         (unsigned long)phys, (unsigned long)total_vram,
         (unsigned long)(phys + MMIO_OFFSET));

    /* src must fit in VRAM. */
    uint32_t max_bytes = 320u * 240u;
    if (VRAM_SRC_OFF + max_bytes > total_vram) {
        slog("FATAL: src @0x%06lX + %lu B exceeds total_vram %lu",
             (unsigned long)VRAM_SRC_OFF, (unsigned long)max_bytes,
             (unsigned long)total_vram);
        slog("[s3blt SUITE_DONE verdict=ABORT_VRAM_TOO_SMALL]");
        slog("S3BLT-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }

    /* ---- set mode + map LFB + MMIO ---- */
    if (vbe_set_mode_lfb(mode) != 0) {
        slog("FATAL: VESA set-mode (LFB) failed for 0x%04X", mode);
        slog("[s3blt SUITE_DONE verdict=ABORT_SETMODE]");
        slog("S3BLT-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }

    __dpmi_meminfo lfb_map, mmio_map;
    uint32_t lfb_span = VRAM_SRC_OFF + max_bytes;     /* cover dst + src      */
    if (lfb_span > total_vram) lfb_span = total_vram;
    memset(&lfb_map, 0, sizeof lfb_map);
    lfb_map.address = phys; lfb_map.size = lfb_span;
    memset(&mmio_map, 0, sizeof mmio_map);
    mmio_map.address = phys + MMIO_OFFSET; mmio_map.size = MMIO_SIZE;

    int map_ok = (__dpmi_physical_address_mapping(&lfb_map) == 0);
    int mmio_ok = map_ok && (__dpmi_physical_address_mapping(&mmio_map) == 0);
    int near_ok = mmio_ok && __djgpp_nearptr_enable();
    if (!near_ok) {
        if (mmio_ok) __dpmi_free_physical_address_mapping(&mmio_map);
        if (map_ok)  __dpmi_free_physical_address_mapping(&lfb_map);
        text_mode_restore();
        slog("FATAL: mapping/nearptr failed (lfb=%d mmio=%d near=%d) -- the "
             "4 MB-aperture map is sdl-engine's #1 risk; this is the empirical "
             "pre-flight and it FAILED.", map_ok, mmio_ok, near_ok ? 1 : 0);
        slog("[s3blt SUITE_DONE verdict=ABORT_MAP_FAILED]");
        slog("S3BLT-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }
    g_lfb  = (volatile uint8_t *)(lfb_map.address  + __djgpp_conventional_base);
    g_mmio = (volatile uint8_t *)(mmio_map.address + __djgpp_conventional_base);

    /* ===== from here we are in graphics mode + nearptr; slog still writes the
     * LOG file correctly (stdout may corrupt the screen, harmless). ===== */
    slog("MAP: LFB mapped %lu B + MMIO 64 KB mapped; 4 MB-aperture pre-flight OK",
         (unsigned long)lfb_span);

    /* v2 FIX: enable the ViRGE new-MMIO window (CR53 bit 3) -- the iter-1
     * RED_MMIO_UNVERIFIED root cause. Done AFTER the VBE mode set (which may
     * re-lock the extended CRTC regs); enable_new_mmio re-unlocks first. */
    enable_new_mmio();

    int base_verified = mmio_verify(c2d, c2e);

    if (!base_verified) {
        /* Fallback A (cheap, same base): some ViRGE steppings gate new-MMIO on
         * CR53 bits [4:3], not bit 3 alone. Set bit 4 too + re-read chip-ID. */
        s3_unlock_crtc();
        uint8_t fb_b = cr_read(0x53);
        cr_write(0x53, (uint8_t)(fb_b | 0x18));        /* bits 3+4 */
        uint8_t fb_a = cr_read(0x53);
        uint8_t am2d = 0, am2e = 0;
        mmio_chipid_thru(&am2d, &am2e);
        base_verified = (am2d == c2d && am2e == c2e);
        slog("[s3blt] MMIO_FALLBACK_A CR53 0x%02X->0x%02X (RMW |0x18 bits3+4) "
             "chipid_thru=0x%02X%02X re-verify=%s", fb_b, fb_a, am2d, am2e,
             base_verified ? "YES" : "NO");
    }
    if (!base_verified) {
        /* Fallback B (alternate base): remap g_mmio to LFB+0x800000 (a flagged
         * alt-MMIO candidate) + re-check chip-ID-thru-MMIO. On NO, revert g_mmio
         * to the documented new-MMIO base so the kernels use it (verify-oracle
         * still reports UNVERIF -- no false numbers). */
        __dpmi_free_physical_address_mapping(&mmio_map);
        if (map_mmio_window(phys + 0x00800000UL, &mmio_map) == 0) {
            uint8_t bm2d = 0, bm2e = 0;
            mmio_chipid_thru(&bm2d, &bm2e);
            base_verified = (bm2d == c2d && bm2e == c2e);
            slog("[s3blt] MMIO_FALLBACK_B base=LFB+0x800000 chipid_thru=0x%02X%02X "
                 "re-verify=%s", bm2d, bm2e, base_verified ? "YES" : "NO");
            if (!base_verified) {
                __dpmi_free_physical_address_mapping(&mmio_map);
                map_mmio_window(phys + MMIO_OFFSET, &mmio_map);   /* revert */
                slog("[s3blt] MMIO_FALLBACK_B NO -- reverted g_mmio to "
                     "LFB+0x%08lX. iter-3: legacy PACKED MMIO at phys 0xA0000 "
                     "(DIFFERENT register offsets -- separate impl, not coded).",
                     (unsigned long)MMIO_OFFSET);
            }
        } else {
            map_mmio_window(phys + MMIO_OFFSET, &mmio_map);       /* re-map primary */
            slog("[s3blt] MMIO_FALLBACK_B map at LFB+0x800000 FAILED; kept the "
                 "primary new-MMIO base.");
        }
    }

    double cpu16 = -1.0, blt16 = -1.0; int blt16_ok = 0;

    /* Kernels ALWAYS run (gated only on the real prerequisites: mapping +
     * nearptr, already satisfied). The BLT readback-verify is the oracle --
     * a wrong MMIO base just yields UNVERIF, never a false number. The
     * watchdog (WATCHDOG_SPINS) bounds a non-responding engine. */
    s3_status_forensics();

    /* ---- K1 + K2 + K3 per geometry ---- */
    slog("[s3blt SUITE_BEGIN chip=0x%02X%02X mode=0x%04X %ux%u]",
         c2d, c2e, mode, xres, yres);

    for (int gi = 0; gi < GEOM_COUNT; gi++) {
        int w = g_geom[gi].w, h = g_geom[gi].h;
        uint32_t bytes = (uint32_t)w * (uint32_t)h;
        slog("");
        slog("---- geom %s (%dx%d, %lu B)%s ----", g_geom[gi].name, w, h,
             (unsigned long)bytes, g_geom[gi].is_trap ? "  [TRAP-REFERENCE]" : "");

        /* K1 verify (also resolves the width convention; height always raw). */
        int conv = k1_verify(gi);
        double blt_med = 0, blt_lo = 0, blt_hi = 0; long blt_it = 0;
        if (conv >= 0) {
            k1_throughput(gi, conv, &blt_med, &blt_lo, &blt_hi, &blt_it);
            slog("S3BLT-RAW geom=%-7s blt conv=%-7s iters=%-6ld MB/s "
                 "med=%7.2f min=%7.2f max=%7.2f", g_geom[gi].name,
                 conv ? "w-1,h" : "w,h", blt_it, blt_med, blt_lo, blt_hi);
            plaus_check("blt", g_geom[gi].name, blt_med);
        }

        /* K3 CPU baseline. */
        double cpu_med = k3_cpu_throughput(gi);
        if (cpu_med > 0.0) {
            slog("S3BLT-RAW geom=%-7s cpu colorkey-blit MB/s med=%7.2f",
                 g_geom[gi].name, cpu_med);
            plaus_check("cpu", g_geom[gi].name, cpu_med);
        }

        /* K2 fill (secondary; same dims convention K1 found, else w-1). */
        int fconv = (conv >= 0) ? conv : 1;
        int fill_ok = k2_fill_verify(gi, fconv);
        double fill_med = 0;
        if (fill_ok) {
            k2_fill_throughput(gi, fconv, &fill_med);
            slog("S3BLT-RAW geom=%-7s fill MB/s med=%7.2f",
                 g_geom[gi].name, fill_med);
            plaus_check("fill", g_geom[gi].name, fill_med);
        }

        /* contract STAT line -- flush-instr keys the gate off it. */
        if (conv >= 0 && cpu_med > 0.0) {
            slog("[s3blt geom=%-7s blt_MBps=%.1f cpu_MBps=%.1f ratio=%.2f "
                 "fill_MBps=%s%.1f%s]", g_geom[gi].name, blt_med, cpu_med,
                 blt_med / cpu_med, fill_ok ? "" : "UNVERIF(", fill_med,
                 fill_ok ? "" : ")");
        } else if (conv < 0) {
            g_warn++;
            slog("[s3blt geom=%-7s blt_MBps=UNVERIF cpu_MBps=%.1f ratio=UNVERIF "
                 "fill_MBps=%s]", g_geom[gi].name,
                 cpu_med > 0.0 ? cpu_med : -1.0,
                 fill_ok ? "(see RAW)" : "UNVERIF");
        } else {
            slog("[s3blt geom=%-7s blt_MBps=%.1f cpu_MBps=UNAVAIL ratio=UNAVAIL]",
                 g_geom[gi].name, blt_med);
        }

        if (g_geom[gi].w == 16 && g_geom[gi].h == 16) {
            cpu16 = cpu_med; blt16 = blt_med; blt16_ok = (conv >= 0);
        }
    }

    /* ---- tear down graphics state BEFORE the verdict prose ---- */
    __djgpp_nearptr_disable();
    text_mode_restore();
    __dpmi_free_physical_address_mapping(&mmio_map);
    __dpmi_free_physical_address_mapping(&lfb_map);
    g_lfb = NULL; g_mmio = NULL;

    /* ---- self-test + GATE ---- */
    slog("");
    slog("S3BLT-SELFTEST warnings=%d", g_warn);

    slog("");
    slog("GATE (plan sec.2.3 P3 -- judged AT 16x16, the tile size):");
    slog("  HARD PREREQ: K1 verify OK (else false-close).");
    slog("  GREEN  iff blt16_verified AND blt_MBps(16x16) > cpu_MBps(16x16) "
         "AND blt_MBps(16x16) >= %.0f (CIR_2D_LEVER_BAR).", CIR_2D_LEVER_BAR);
    slog("  RED    iff verify-fail OR loses-to-CPU@16x16 OR < %.0f MB/s.",
         CIR_2D_LEVER_BAR);
    slog("  NOTE: 16x256 is the TRAP-reference (Cirrus strip-peak ~%.0f MB/s) "
         "-- NOT the lever bar; ignore its rate for the gate.", CIR_BLT_16x256_PEAK);

    const char *verdict;
    if (blt16_ok && cpu16 > 0.0 && blt16 > cpu16 && blt16 >= CIR_2D_LEVER_BAR) {
        verdict = "GREEN";
    } else if (blt16_ok && cpu16 > 0.0 && blt16 <= cpu16) {
        verdict = "RED_LOSES_TO_CPU_16x16";   /* engine works but too slow      */
    } else if (blt16_ok && blt16 < CIR_2D_LEVER_BAR) {
        verdict = "RED_BELOW_40MBPS";          /* engine works but under the bar */
    } else if (!blt16_ok && !base_verified) {
        verdict = "RED_MMIO_UNVERIFIED";       /* likely wrong base -> iter-2 try candidates */
    } else if (!blt16_ok && base_verified) {
        verdict = "RED_BLT_VERIFY_FAIL";       /* base ok, BLT register seq wrong -> iter-2 fix seq */
    } else {
        verdict = "RED_INDETERMINATE";
    }
    slog("[s3blt GATE_16x16 blt_MBps=%.1f cpu_MBps=%.1f bar=%.0f verdict=%s]",
         blt16, cpu16, CIR_2D_LEVER_BAR, verdict);
    slog("  GREEN -> green-light the S3 _blit_indexed offload (Lever 2; "
         "sdl-engine + nx-engine). RED -> 2D engine RED, render stays "
         "bandwidth-bound (look to MEMBW L1 instead).");
    slog("  flush-instr applies the final gate; K4 transparent-colorkey ladder "
         "is DEFERRED to iter 2.");

    slog("");
    slog("[s3blt SUITE_DONE]");
    slog("S3BLT-DONE");
    if (g_log) fclose(g_log);
    return 0;
}
