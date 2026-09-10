/*
 * blttile.c -- Cirrus 5434 BitBLT tile-copy card: formally close it.
 *
 * Phase 11 task #30, probe-engineer authors. Standalone DJGPP probe;
 * NO SDL, NO engine, NO C++. Authored to the contract in
 * docs/internal/BLTTILE-PROBE-DESIGN.md (flush-instr, contract owner).
 *
 * THE QUESTION (BLTTILE-PROBE-DESIGN sec.0):
 *   Can the Cirrus CL-GD5434 hardware BitBLT engine copy TILE-sized
 *   rects VRAM->VRAM fast enough to beat the ~18 MB/s CPU sysmem
 *   colorkey-blit path -- and can it skip transparent (colorkey)
 *   pixels? PATTERN_COPY measured 68 MB/s (bltpat-v2) but its source
 *   is a latched register pattern, NOT VRAM traffic. wave-19/wave-50
 *   put real VRAM-rect traffic at ~19 MB/s. NORMAL-mode tile copy IS
 *   VRAM-rect traffic; blttile.c measures that class directly.
 *
 * HW-IO PROBE -- direct Cirrus register programming. Real-HW-only:
 * DOSBox-X's Cirrus emulation does not model BLT-engine timing
 * ([[dosbox_not_proxy]]); the DOSBox-X smoke is correctness-only
 * (runs, exits, writes a parseable log). HAZARD class: direct chip
 * I/O. Mitigations: bounded register writes, capped 200k-spin BLT
 * watchdog, atexit text-mode restore. Modelled on the proven
 * cirrus-blt-variant-probe.c (bltvar) harness + SDL/0060
 * SDL_DOSVesaDirect_BLTCopy NORMAL-mode register sequence.
 *
 * WHAT IT MEASURES (sec.1):
 *   K1 -- VRAM->VRAM NORMAL-mode rect copy throughput, source resident
 *         in OFF-SCREEN VRAM, dst in visible VRAM, for 3 geometries:
 *         16x16 (one tile), 32x32 (4-tile block), 16x256 (tile-column
 *         strip). Each is verified (dst readback == source) BEFORE its
 *         throughput is trusted -- a chip that runs but writes wrong
 *         data (the bltvar BULK_COPY FAIL_FIRST lesson) must not yield
 *         a fake blt_MBps. Unverified -> blt_MBps=UNVERIF.
 *   K2 -- A/B vs the CPU sysmem colorkey blit: a plain CPU per-pixel
 *         `if (px != colorkey)` copy loop (the _blit_indexed path
 *         class), sysmem->sysmem, same payload. Gives cpu_MBps + ratio.
 *   K3 -- transparent-compare (colorkey ROP) feasibility. The Cirrus
 *         5434 colorkey-ROP register sequence is undocumented in
 *         SDL/0060 + every existing probe (all program GR[0x2F]=0x00
 *         "no transparency"). Per the flush-instr STOP-and-ack ruling:
 *         a bltvar-style CANDIDATE-LADDER (3 hypotheses centred on
 *         GR[0x2F]) emitting raw dst readback per candidate -- NOT one
 *         guessed sequence. Verdict is USABLE encoding=Cn or
 *         INCONCLUSIVE (never a bare confident NOT-USABLE -- that would
 *         false-close the card; the bltpat-v2 V7 failure mode).
 *
 * GEOMETRY MODEL NOTE: tiles are programmed PACKED (src_pitch =
 * dst_pitch = w) so the BLT moves w*h contiguous bytes -- a clean
 * bandwidth measurement + trivial verify. A real strided tile blit
 * into a 640-wide framebuffer (dst_pitch=640) would pay additional
 * DRAM-page-crossing cost, so packed is an OPTIMISTIC bound for the
 * chip: if packed VRAM-rect copy does not beat the CPU path, a
 * strided real-FB blit certainly will not. Appropriate for a
 * "does it even beat the CPU" gate.
 *
 * Output: BLTTILE.LOG (CWD), fallback C:\BLTTILE.LOG. fsync per line.
 *
 * DOS constraints: -march=i486 -mtune=pentium, no MMX/SSE (P54C
 * predates them); size_t is 32-bit on DJGPP -- byte math kept in
 * uint32_t / double. Pure DJGPP libc + DPMI.
 *
 * 8.3 DOS filename: BLTTILE.EXE (7.3) -- fits.
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
#include <sys/movedata.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("BLTTILE.LOG", "w");
    if (!g_log) g_log = fopen("C:\\BLTTILE.LOG", "w");
}

static void plog(const char *fmt, ...)
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
/* Timing -- RDTSC, calibrated against uclock (bltvar pattern)   */
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

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

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
/* VGA / Cirrus port helpers (bltvar harness)                    */
/* ============================================================ */

#define SR_INDEX  0x3C4
#define SR_DATA   0x3C5
#define GR_INDEX  0x3CE
#define GR_DATA   0x3CF
#define CRTC_IDX  0x3D4
#define CRTC_DATA 0x3D5

static uint8_t sr_read(uint8_t idx)  { outportb(SR_INDEX, idx); return inportb(SR_DATA); }
static void    sr_write(uint8_t idx, uint8_t v) { outportb(SR_INDEX, idx); outportb(SR_DATA, v); }
static uint8_t gr_read(uint8_t idx)  { outportb(GR_INDEX, idx); return inportb(GR_DATA); }
static void    gr_write(uint8_t idx, uint8_t v) { outportb(GR_INDEX, idx); outportb(GR_DATA, v); }
static uint8_t cr_read(uint8_t idx)  { outportb(CRTC_IDX, idx); return inportb(CRTC_DATA); }

static int detect_cirrus(uint8_t *out_crtc27, const char **out_name)
{
    sr_write(0x06, 0x12);
    uint8_t sr06   = sr_read(0x06);
    uint8_t crtc27 = cr_read(0x27);
    *out_crtc27 = crtc27;
    switch (crtc27) {
        case 0xA0: *out_name = "Cirrus CL-GD5430";                   return 1;
        case 0xA8: *out_name = "Cirrus CL-GD5434 (g2k expected)";     return 1;
        case 0xAC: *out_name = "Cirrus CL-GD5436";                    return 1;
        case 0xB8: *out_name = "Cirrus CL-GD5446";                    return 1;
        default:   break;
    }
    if (sr06 == 0x12) { *out_name = "(Cirrus-extension-responsive, unknown id)"; return 1; }
    *out_name = "(NOT Cirrus or extension lock not Cirrus-style)";
    return 0;
}

/* ============================================================ */
/* VBE mode helpers                                              */
/* ============================================================ */

static int vbe_set_mode(uint16_t mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = mode;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return (r.x.ax == 0x004F) ? 0 : -2;
}

static int vbe_set_bank(int bank)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F05;
    r.x.bx = 0x0000;
    r.x.dx = (uint16_t)bank;
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
/* Geometry + VRAM layout                                        */
/* ============================================================ */

/* 3 tile geometries (BLTTILE-PROBE-DESIGN sec.1). */
#define GEOM_COUNT 3
static const struct { const char *name; int w; int h; } g_geom[GEOM_COUNT] = {
    { "16x16",  16,  16  },   /* one tile        -> 256 B  */
    { "32x32",  32,  32  },   /* 4-tile block    -> 1024 B */
    { "16x256", 16,  256 },   /* tile-col strip  -> 4096 B */
};

/* VRAM offsets. dst in visible VRAM (top-left); src truly off-screen
 * above the 640x480x8 visible region (307200 B). 0x080000 = 512 KB --
 * the probe verifies the src region is RAM-backed via prefill readback;
 * a 5434 with >=1 MB VRAM (the standard floor) covers this. */
#define VRAM_DST_OFF   0x000000UL
#define VRAM_SRC_OFF   0x080000UL

/* CPU colorkey for the K2 A/B copy loop -- the _blit_indexed branch. */
#define CPU_COLORKEY   0xFF

/* Timed-run shape -- ROUNDS interleaved batches, median per round
 * (bandcomp / l1fill precedent: median is robust to drift). */
#define ROUNDS         9
#define BATCH_SECS     0.012       /* per timed batch                 */
#define CALIB_ITERS    32

/* Cross-anchors -- carried forward as named constants
 * ([[probe_authoring_discipline]] cross-anchor reuse). Used for
 * emit-line plausibility bounds only, never asserted as predictions
 * ([[perf_measurement_discipline]]). */
#define ANCHOR_VRAM_RECT_MBS  19.0    /* wave-19/50 VRAM-rect traffic   */
#define ANCHOR_CPU_BLIT_MBS   18.0    /* bandcomp / wave-51 CPU path    */
#define ANCHOR_PATTERN_MBS    68.0    /* bltpat-v2 PATTERN_COPY (latch) */
#define GATE_GREEN_MBS        40.0    /* sec.2 GREEN threshold          */
#define PLAUS_LO_MBS           1.0
#define PLAUS_HI_MBS         500.0

static int g_warn_count = 0;

/* ============================================================ */
/* Banked VRAM put/get (handles 64 KB window crossing)           */
/* ============================================================ */

static void vram_put(uint32_t vram_off, const uint8_t *buf, uint32_t n)
{
    uint32_t cur = vram_off, pos = 0, rem = n;
    while (rem > 0) {
        int bank = (int)(cur >> 16);
        uint32_t in_bank = cur & 0xFFFF;
        uint32_t chunk = 0x10000u - in_bank;
        if (chunk > rem) chunk = rem;
        vbe_set_bank(bank);
        dosmemput(buf + pos, chunk, 0xA0000UL + in_bank);
        pos += chunk; rem -= chunk; cur += chunk;
    }
}

static void vram_get(uint32_t vram_off, uint8_t *buf, uint32_t n)
{
    uint32_t cur = vram_off, pos = 0, rem = n;
    while (rem > 0) {
        int bank = (int)(cur >> 16);
        uint32_t in_bank = cur & 0xFFFF;
        uint32_t chunk = 0x10000u - in_bank;
        if (chunk > rem) chunk = rem;
        vbe_set_bank(bank);
        dosmemget(0xA0000UL + in_bank, chunk, buf + pos);
        pos += chunk; rem -= chunk; cur += chunk;
    }
}

/* ============================================================ */
/* Cirrus 5434 NORMAL-mode BLT (SDL/0060 register sequence)      */
/* ============================================================ */

static void blt_clear_s2s_constraints(void)
{
    /* GR[0x0B] bits 1 + 4 -- screen-to-screen BLT errata. */
    uint8_t v = gr_read(0x0B);
    v &= (uint8_t)~((1u << 1) | (1u << 4));
    gr_write(0x0B, v);
}

static inline int blt_busy(void)
{
    outportb(GR_INDEX, 0x31);
    return (inportb(GR_DATA) & 0x08) != 0;
}

/* Spin until idle, capped (~200k iterations ~= 10 ms worst-case on
 * PODP83). Returns 0 idle, -1 stuck. */
static int blt_wait_idle(void)
{
    for (int spin = 0; spin < 200000; spin++) {
        if (!blt_busy()) return 0;
    }
    return -1;
}

/* Program the BLT geometry registers GR[0x20-0x2E] -- width-1,
 * height-1, dst/src pitch, 24-bit dst/src offsets. Packed tile:
 * src_pitch = dst_pitch = w. Shared by NORMAL + transparent paths. */
static void blt_program_geom(uint32_t dst_off, uint32_t src_off,
                             int w, int h)
{
    uint16_t w_m1 = (uint16_t)(w - 1);
    uint16_t h_m1 = (uint16_t)(h - 1);
    uint16_t pitch = (uint16_t)w;          /* packed-tile model        */

    gr_write(0x20, (uint8_t)(w_m1 & 0xFF));
    gr_write(0x21, (uint8_t)((w_m1 >> 8) & 0x0F));
    gr_write(0x22, (uint8_t)(h_m1 & 0xFF));
    gr_write(0x23, (uint8_t)((h_m1 >> 8) & 0x0F));
    gr_write(0x24, (uint8_t)(pitch & 0xFF));
    gr_write(0x25, (uint8_t)((pitch >> 8) & 0xFF));
    gr_write(0x26, (uint8_t)(pitch & 0xFF));
    gr_write(0x27, (uint8_t)((pitch >> 8) & 0xFF));
    gr_write(0x28, (uint8_t)(dst_off & 0xFF));
    gr_write(0x29, (uint8_t)((dst_off >> 8) & 0xFF));
    gr_write(0x2A, (uint8_t)((dst_off >> 16) & 0xFF));
    gr_write(0x2C, (uint8_t)(src_off & 0xFF));
    gr_write(0x2D, (uint8_t)((src_off >> 8) & 0xFF));
    gr_write(0x2E, (uint8_t)((src_off >> 16) & 0xFF));
}

/* Program a NORMAL-mode (GR[0x30]=0x00) screen-to-screen rect copy.
 * SRCCOPY ROP, no transparency. Identical sequence to SDL/0060
 * SDL_DOSVesaDirect_BLTCopy. */
static void blt_program_normal(uint32_t dst_off, uint32_t src_off,
                               int w, int h)
{
    blt_program_geom(dst_off, src_off, w, h);
    gr_write(0x2F, 0x00);   /* no transparency (K1 baseline)            */
    gr_write(0x30, 0x00);   /* NORMAL screen-to-screen mode             */
    gr_write(0x32, 0x0D);   /* SRCCOPY ROP                              */
    gr_write(0x33, 0x00);   /* no extension flags                       */
}

/* One full programmed NORMAL BLT. Returns 0 ok, -1 chip stuck. */
static int blt_one_normal(uint32_t dst_off, uint32_t src_off, int w, int h)
{
    gr_write(0x31, 0x04);               /* BLT_RESET -- clear stale     */
    if (blt_wait_idle() != 0) return -1;
    blt_program_normal(dst_off, src_off, w, h);
    gr_write(0x31, 0x02);               /* kick                         */
    if (blt_wait_idle() != 0) {
        gr_write(0x31, 0x04);           /* emergency reset for next call */
        return -1;
    }
    return 0;
}

/* ============================================================ */
/* K1 -- per-geometry VRAM->VRAM throughput + verify             */
/* ============================================================ */

static void plaus_check(const char *what, double mbs)
{
    if (mbs < PLAUS_LO_MBS || mbs > PLAUS_HI_MBS) {
        g_warn_count++;
        plog("BLTTILE-WARN %s = %.2f MB/s OUTSIDE plausible window "
             "[%.1f, %.1f] -- emit suspect (anchors: VRAM-rect ~%.0f, "
             "CPU ~%.0f, PATTERN ~%.0f MB/s)",
             what, mbs, PLAUS_LO_MBS, PLAUS_HI_MBS,
             ANCHOR_VRAM_RECT_MBS, ANCHOR_CPU_BLIT_MBS, ANCHOR_PATTERN_MBS);
    }
}

/* Verify: one BLT, read dst back, compare to the known source pattern
 * (gradient byte = offset&0xFF). Guards against a chip that runs but
 * writes wrong data (bltvar BULK_COPY FAIL_FIRST). Returns 1 ok. */
static int k1_verify(int gi)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    uint32_t bytes = (uint32_t)w * (uint32_t)h;
    uint8_t *dst = (uint8_t *)malloc(bytes);
    if (!dst) { plog("BLTTILE-WARN k1_verify malloc failed"); return 0; }

    /* sentinel-clear dst so "untouched" is distinguishable from "wrong". */
    {
        uint8_t *sent = (uint8_t *)malloc(bytes);
        if (sent) { memset(sent, 0xCC, bytes); vram_put(VRAM_DST_OFF, sent, bytes); free(sent); }
    }

    int rc = blt_one_normal(VRAM_DST_OFF, VRAM_SRC_OFF, w, h);
    if (rc != 0) {
        plog("  verify geom=%s: BLT STUCK (chip did not complete)", g_geom[gi].name);
        free(dst);
        return 0;
    }
    vram_get(VRAM_DST_OFF, dst, bytes);

    int untouched = 1, exact = 1;
    for (uint32_t i = 0; i < bytes; i++) {
        if (dst[i] != 0xCC)              untouched = 0;
        if (dst[i] != (uint8_t)(i & 0xFF)) exact = 0;
    }
    if (exact) {
        plog("  verify geom=%s: OK (dst readback == source gradient, %lu B)",
             g_geom[gi].name, (unsigned long)bytes);
    } else if (untouched) {
        plog("  verify geom=%s: FAIL -- dst all-0xCC, BLT wrote nothing",
             g_geom[gi].name);
    } else {
        plog("  verify geom=%s: FAIL -- dst differs from source "
             "(dst[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X; "
             "chip ran but wrote wrong data)",
             g_geom[gi].name, dst[0], dst[1], dst[2], dst[3],
             dst[4], dst[5], dst[6], dst[7]);
    }
    free(dst);
    return exact;
}

/* Calibrate iters/batch for ~BATCH_SECS, then time ROUNDS batches;
 * return median MB/s into *med, min into *lo, max into *hi, iters *itp. */
static void k1_throughput(int gi, double *med, double *lo, double *hi,
                           long *itp)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    double bytes = (double)w * (double)h;
    double samp[ROUNDS];
    long N;
    int r;

    /* calibrate. */
    {
        uint64_t c0 = rdtsc();
        for (int i = 0; i < CALIB_ITERS; i++)
            blt_one_normal(VRAM_DST_OFF, VRAM_SRC_OFF, w, h);
        double us = cycles_to_us(rdtsc() - c0);
        double per = us / (double)CALIB_ITERS;
        if (per <= 0.0) per = 1.0;
        N = (long)((BATCH_SECS * 1e6) / per) + 1;
        if (N < 8) N = 8;
    }
    *itp = N;

    for (r = 0; r < ROUNDS; r++) {
        uint64_t c0 = rdtsc();
        for (long i = 0; i < N; i++)
            blt_one_normal(VRAM_DST_OFF, VRAM_SRC_OFF, w, h);
        double secs = cycles_to_us(rdtsc() - c0) / 1e6;
        samp[r] = (secs > 0.0) ? ((double)N * bytes) / (secs * 1048576.0)
                               : 0.0;
    }
    qsort(samp, ROUNDS, sizeof samp[0], dbl_cmp);
    *med = samp[ROUNDS / 2];
    *lo  = samp[0];
    *hi  = samp[ROUNDS - 1];
}

/* ============================================================ */
/* K2 -- CPU sysmem colorkey blit A/B (the _blit_indexed class)  */
/* ============================================================ */

/* one tile worth of branchy per-pixel colorkey copy, sysmem->sysmem. */
static void cpu_colorkey_tile(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t px = src[i];
        if (px != CPU_COLORKEY) dst[i] = px;
    }
}

static double k2_cpu_throughput(int gi, uint8_t *src, uint8_t *dst)
{
    int w = g_geom[gi].w, h = g_geom[gi].h;
    double bytes = (double)w * (double)h;
    uint32_t n = (uint32_t)w * (uint32_t)h;
    double samp[ROUNDS];
    long N;
    int r;

    cpu_colorkey_tile(dst, src, n);     /* warm-up */
    {
        uint64_t c0 = rdtsc();
        for (int i = 0; i < CALIB_ITERS; i++) cpu_colorkey_tile(dst, src, n);
        double us = cycles_to_us(rdtsc() - c0);
        double per = us / (double)CALIB_ITERS;
        if (per <= 0.0) per = 1.0;
        N = (long)((BATCH_SECS * 1e6) / per) + 1;
        if (N < 8) N = 8;
    }
    for (r = 0; r < ROUNDS; r++) {
        uint64_t c0 = rdtsc();
        for (long i = 0; i < N; i++) cpu_colorkey_tile(dst, src, n);
        double secs = cycles_to_us(rdtsc() - c0) / 1e6;
        samp[r] = (secs > 0.0) ? ((double)N * bytes) / (secs * 1048576.0)
                               : 0.0;
    }
    qsort(samp, ROUNDS, sizeof samp[0], dbl_cmp);
    return samp[ROUNDS / 2];
}

/* ============================================================ */
/* K3 -- transparent-compare feasibility (candidate-ladder)      */
/* ============================================================ */
/*
 * The Cirrus 5434 colorkey-ROP register sequence is undocumented in
 * SDL/0060 (which programs GR[0x2F]=0x00 "no transparency") + every
 * existing Cirrus probe. Per the flush-instr STOP-and-ack ruling
 * (contract owner): do NOT guess one authoritative sequence -- a wrong
 * single program emits a confident-but-false NOT-USABLE -> false RED
 * gate (the bltpat-v2 V7 / SDLPROBE post-mortem failure mode). Instead
 * run a bltvar-style CANDIDATE-LADDER: try a few plausible encodings,
 * emit raw dst-byte readback per candidate, let the LOG data show
 * which (if any) produces correct colorkey behaviour.
 *
 * Ladder centred on GR[0x2F] -- SDL/0060's "no transparency" comment
 * makes it the prime transparency-ENABLE suspect (flush-instr point 2).
 * The candidates are HYPOTHESES to disambiguate by readback, NOT
 * authoritative register sequences.
 *
 * Test rig (flush-instr point 4): dst pre-filled with sentinel TSENT;
 * src tile has BOTH key (even pixels = TKEY) and non-key (odd pixels =
 * TNKVAL) at known positions; BLT; read back. USABLE iff every non-key
 * src pixel landed in dst AND every key-pixel dst position still holds
 * TSENT (the chip skipped it).
 *
 * Verdict (flush-instr point 5): "USABLE encoding=Cn" if one works;
 * else "INCONCLUSIVE" (NOT a bare confident "NOT-USABLE" -- that would
 * let the gate false-close the card; INCONCLUSIVE = transparent-BLT
 * unproven, may need the datasheet). Every candidate's readback is
 * logged regardless.
 */

#define TKEY    0x00    /* transparent (colorkey) palette index        */
#define TNKVAL  0xAA    /* non-key pixel value                         */
#define TSENT   0xCC    /* dst sentinel -- skipped pixels keep this     */
#define T_W     16
#define T_H     16
#define T_N     (T_W * T_H)

typedef struct {
    const char *name;
    uint8_t gr_2f;      /* transparency enable / control                */
    uint8_t gr_30;      /* BLT mode (0x00 NORMAL; 0x08 = transp-as-mode) */
    uint8_t gr_34, gr_35;  /* transparent color (lo, hi)                */
    uint8_t gr_38, gr_39;  /* transparent color mask (lo, hi)           */
    const char *desc;
} tcand_t;

/* flush-instr ladder (point 3) -- hypotheses, disambiguated by readback. */
static const tcand_t g_tcands[] = {
    { "C1", 0x01, 0x00, TKEY, 0x00, 0xFF, 0x00,
      "GR[0x2F]=0x01 enable; color GR[0x34/0x35]; mask GR[0x38/0x39]" },
    { "C2", 0x04, 0x00, TKEY, 0x00, 0xFF, 0x00,
      "GR[0x2F]=0x04 (higher enable bit); else as C1" },
    { "C3", 0x00, 0x08, TKEY, 0x00, 0xFF, 0x00,
      "transparency as GR[0x30] bit 3 (0x08); GR[0x2F]=0x00" },
};
#define N_TCAND ((int)(sizeof(g_tcands) / sizeof(g_tcands[0])))

/* One programmed transparent BLT for candidate C. 0 ok, -1 stuck. */
static int blt_one_transparent(const tcand_t *C, uint32_t dst_off,
                               uint32_t src_off, int w, int h)
{
    gr_write(0x31, 0x04);               /* BLT_RESET                    */
    if (blt_wait_idle() != 0) return -1;
    blt_program_geom(dst_off, src_off, w, h);
    gr_write(0x2F, C->gr_2f);
    gr_write(0x30, C->gr_30);
    gr_write(0x32, 0x0D);               /* SRCCOPY ROP                  */
    gr_write(0x33, 0x00);
    gr_write(0x34, C->gr_34);           /* transparent color            */
    gr_write(0x35, C->gr_35);
    gr_write(0x36, 0x00);
    gr_write(0x37, 0x00);               /* mask hi-bytes cleared        */
    gr_write(0x38, C->gr_38);           /* transparent color mask       */
    gr_write(0x39, C->gr_39);
    gr_write(0x31, 0x02);               /* kick                         */
    if (blt_wait_idle() != 0) {
        gr_write(0x31, 0x04);           /* emergency reset               */
        return -1;
    }
    return 0;
}

static void k3_transparent_compare(void)
{
    uint8_t src[T_N], dst[T_N], sent[T_N];
    int i, c, usable_idx = -1;

    plog("---- K3: transparent-compare feasibility (candidate-ladder) ----");

    /* src tile: even pixels = key, odd pixels = non-key (known rig). */
    for (i = 0; i < T_N; i++) src[i] = (i & 1) ? (uint8_t)TNKVAL
                                               : (uint8_t)TKEY;
    vram_put(VRAM_SRC_OFF, src, T_N);
    for (i = 0; i < T_N; i++) sent[i] = (uint8_t)TSENT;
    plog("  rig: %dx%d tile; src even=key 0x%02X odd=non-key 0x%02X; "
         "dst sentinel 0x%02X", T_W, T_H, TKEY, TNKVAL, TSENT);

    for (c = 0; c < N_TCAND; c++) {
        const tcand_t *C = &g_tcands[c];

        vram_put(VRAM_DST_OFF, sent, T_N);   /* re-sentinel dst         */
        int rc = blt_one_transparent(C, VRAM_DST_OFF, VRAM_SRC_OFF,
                                     T_W, T_H);
        if (rc != 0) {
            g_warn_count++;
            plog("  %s (%s): BLT STUCK -- chip did not complete; skipped",
                 C->name, C->desc);
            continue;
        }
        vram_get(VRAM_DST_OFF, dst, T_N);

        /* classify: skip_ok = every key position untouched (TSENT);
         *           copy_ok = every non-key position got TNKVAL. */
        int skip_ok = 1, copy_ok = 1, all_sent = 1;
        for (i = 0; i < T_N; i++) {
            if (dst[i] != (uint8_t)TSENT) all_sent = 0;
            if (i & 1) { if (dst[i] != (uint8_t)TNKVAL) copy_ok = 0; }
            else       { if (dst[i] != (uint8_t)TSENT)  skip_ok = 0; }
        }
        const char *status;
        if (skip_ok && copy_ok)      status = "USABLE";
        else if (all_sent)           status = "NO-OP (BLT wrote nothing)";
        else if (copy_ok && !skip_ok)
            status = "OPAQUE-COPY (key NOT skipped -- transparency off)";
        else                         status = "WRONG (garbled readback)";

        plog("  %s: %s", C->name, C->desc);
        plog("    dst[0..15]: %02X %02X %02X %02X %02X %02X %02X %02X "
             "%02X %02X %02X %02X %02X %02X %02X %02X",
             dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6],
             dst[7], dst[8], dst[9], dst[10], dst[11], dst[12], dst[13],
             dst[14], dst[15]);
        plog("    -> %s", status);
        if (skip_ok && copy_ok && usable_idx < 0) usable_idx = c;
    }

    /* verdict -- USABLE encoding=Cn, or INCONCLUSIVE (never bare
     * "NOT-USABLE" -- flush-instr point 5). */
    if (usable_idx >= 0) {
        plog("[blttile transparent_compare=USABLE encoding=%s "
             "notes=src-key-pixels-skipped-non-key-copied]",
             g_tcands[usable_idx].name);
    } else {
        plog("[blttile transparent_compare=INCONCLUSIVE "
             "notes=tried %d encodings {C1,C2,C3}, none skipped the key "
             "pixel; readbacks above -- transparent-BLT unproven, may "
             "need the CL-GD5434 datasheet]", N_TCAND);
    }
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== BLTTILE -- Cirrus 5434 BitBLT tile-copy card probe ===");
    plog("BLTTILE-BEGIN");
    plog("contract: docs/internal/BLTTILE-PROBE-DESIGN.md (flush-instr)");
    plog("build: DJGPP -march=i486 -mtune=pentium -O2, no MMX/SSE");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);

    atexit(text_mode_restore);          /* hazard discipline            */

    /* ---- RDTSC calibration ---- */
    calibrate_rdtsc();
    plog("rdtsc: cpu_mhz=%u us_per_cycle=%.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    if (g_us_per_cycle <= 0.0) {
        plog("FATAL: RDTSC calibration failed -- cannot time; aborting.");
        plog("[blttile SUITE_DONE verdict=ABORT_NO_TIMER]");
        plog("BLTTILE-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }

    /* ---- chip detect ---- */
    uint8_t crtc27 = 0;
    const char *chip = NULL;
    int is_cirrus = detect_cirrus(&crtc27, &chip);
    plog("chip: CRTC[0x27]=0x%02X -> %s", (unsigned)crtc27, chip);
    if (!is_cirrus || crtc27 != 0xA8) {
        plog("BLTTILE-WARN chip is not a Cirrus 5434 (CRTC[0x27]!=0xA8); "
             "the BLT register layout differs across the family -- "
             "skipping all kernels (chip-specific probe).");
        plog("[blttile SUITE_DONE verdict=SKIP_NOT_5434]");
        plog("BLTTILE-DONE");
        if (g_log) fclose(g_log);
        return 0;
    }

    /* ---- VBE 8bpp mode ---- */
    int vrc = vbe_set_mode(0x0101);     /* 640x480x8 */
    const char *modename = "0x0101 (640x480x8)";
    if (vrc != 0) {
        vrc = vbe_set_mode(0x0100);     /* 640x400x8 fallback */
        modename = "0x0100 (640x400x8)";
    }
    if (vrc != 0) {
        plog("FATAL: VESA 8bpp graphics mode set failed (rc=%d)", vrc);
        plog("[blttile SUITE_DONE verdict=ABORT_VBE_FAILED]");
        plog("BLTTILE-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("vbe mode set: %s", modename);

    /* re-unlock + clear errata after mode set. */
    sr_write(0x06, 0x12);
    blt_clear_s2s_constraints();
    plog("Cirrus unlocked (SR[0x06]=0x12); GR[0x0B] s2s constraints cleared");

    /* ---- prefill source tile in off-screen VRAM ---- */
    /* gradient byte = offset&0xFF; large enough for the biggest tile. */
    {
        uint32_t maxb = 0;
        for (int gi = 0; gi < GEOM_COUNT; gi++) {
            uint32_t b = (uint32_t)g_geom[gi].w * (uint32_t)g_geom[gi].h;
            if (b > maxb) maxb = b;
        }
        uint8_t *src = (uint8_t *)malloc(maxb);
        uint8_t *chk = (uint8_t *)malloc(maxb);
        if (!src || !chk) {
            plog("FATAL: prefill malloc failed");
            plog("[blttile SUITE_DONE verdict=ABORT_MALLOC]");
            free(src); free(chk);
            if (g_log) fclose(g_log);
            return 2;
        }
        for (uint32_t i = 0; i < maxb; i++) src[i] = (uint8_t)(i & 0xFF);
        vram_put(VRAM_SRC_OFF, src, maxb);
        /* self-check: src VRAM region must be RAM-backed + readable. */
        vram_get(VRAM_SRC_OFF, chk, maxb);
        if (memcmp(src, chk, maxb) != 0) {
            g_warn_count++;
            plog("BLTTILE-WARN off-screen src VRAM @0x%06lX did not read "
                 "back intact -- VRAM may be < 0x%06lX or banking issue; "
                 "K1 results suspect.",
                 (unsigned long)VRAM_SRC_OFF,
                 (unsigned long)(VRAM_SRC_OFF + maxb));
        } else {
            plog("prefill: src gradient @0x%06lX verified RAM-backed (%lu B)",
                 (unsigned long)VRAM_SRC_OFF, (unsigned long)maxb);
        }
        free(src); free(chk);
    }
    plog("");

    /* ---- K1 + K2 per geometry ---- */
    plog("[blttile SUITE_BEGIN chip=%s mode=%s]", chip, modename);
    for (int gi = 0; gi < GEOM_COUNT; gi++) {
        int w = g_geom[gi].w, h = g_geom[gi].h;
        uint32_t bytes = (uint32_t)w * (uint32_t)h;
        plog("");
        plog("---- geometry %s (%dx%d, %lu B/tile) ----",
             g_geom[gi].name, w, h, (unsigned long)bytes);

        /* K1: verify, then time. */
        int verified = k1_verify(gi);

        double blt_med = 0.0, blt_lo = 0.0, blt_hi = 0.0;
        long blt_iters = 0;
        k1_throughput(gi, &blt_med, &blt_lo, &blt_hi, &blt_iters);
        plog("BLTTILE-RAW geom=%-6s blt verified=%-3s iters=%-6ld "
             "MB/s med=%7.2f min=%7.2f max=%7.2f",
             g_geom[gi].name, verified ? "yes" : "NO", blt_iters,
             blt_med, blt_lo, blt_hi);
        if (verified) plaus_check("blt", blt_med);

        /* K2: CPU sysmem colorkey A/B, same payload. */
        double cpu_med = 0.0;
        {
            uint8_t *csrc = (uint8_t *)malloc(bytes);
            uint8_t *cdst = (uint8_t *)malloc(bytes);
            if (csrc && cdst) {
                for (uint32_t i = 0; i < bytes; i++) {
                    csrc[i] = (uint8_t)(i & 0xFF);
                    cdst[i] = 0;
                }
                cpu_med = k2_cpu_throughput(gi, csrc, cdst);
                plaus_check("cpu", cpu_med);
                plog("BLTTILE-RAW geom=%-6s cpu colorkey-blit "
                     "MB/s med=%7.2f", g_geom[gi].name, cpu_med);
            } else {
                g_warn_count++;
                plog("BLTTILE-WARN geom=%s cpu A/B malloc failed",
                     g_geom[gi].name);
            }
            free(csrc); free(cdst);
        }

        /* contract STAT line -- flush-instr keys the sec.2 gate off it. */
        if (verified && cpu_med > 0.0) {
            double ratio = blt_med / cpu_med;
            plog("[blttile geom=%-6s blt_MBps=%.1f cpu_MBps=%.1f "
                 "ratio=%.2f]", g_geom[gi].name, blt_med, cpu_med, ratio);
        } else if (!verified) {
            /* BLT unverified -> blt_MBps is meaningless; do not emit a
             * trustable number. The measured-but-untrusted rate is in
             * the BLTTILE-RAW line above for flush-instr's reference. */
            g_warn_count++;
            plog("[blttile geom=%-6s blt_MBps=UNVERIF cpu_MBps=%.1f "
                 "ratio=UNVERIF]", g_geom[gi].name, cpu_med);
        } else {
            plog("[blttile geom=%-6s blt_MBps=%.1f cpu_MBps=UNAVAIL "
                 "ratio=UNAVAIL]", g_geom[gi].name, blt_med);
        }
    }

    /* ---- K3: transparent-compare (PENDING flush-instr) ---- */
    plog("");
    k3_transparent_compare();

    /* ---- self-test summary ---- */
    plog("");
    plog("BLTTILE-SELFTEST warnings=%d", g_warn_count);
    if (g_warn_count == 0)
        plog("BLTTILE-SELFTEST OK -- all emit lines plausibility-bound");
    else
        plog("BLTTILE-SELFTEST FLAGGED -- review BLTTILE-WARN lines");

    plog("");
    plog("Gate (BLTTILE-PROBE-DESIGN sec.2, all 3 geometries + K3):");
    plog("  GREEN: blt_MBps >= ~%.0f (>=2x CPU) AND transparent USABLE",
         GATE_GREEN_MBS);
    plog("  RED:   blt_MBps ~%.0f (wave-19 redux) OR transparent NOT-USABLE",
         ANCHOR_VRAM_RECT_MBS);
    plog("  flush-instr applies the gate from the [blttile ...] lines.");

    text_mode_restore();
    plog("[blttile SUITE_DONE]");
    plog("BLTTILE-DONE");
    if (g_log) fclose(g_log);
    return 0;
}
