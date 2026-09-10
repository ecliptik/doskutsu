/*
 * membw.c — memory bandwidth probe for wave-22.5 path-B (tilemap caching) decision.
 *
 * Phase 11 wave 22 task #14 (P4). Standalone DJGPP diagnostic probe sized for
 * the wave-23 path-B feasibility question: if NXEngine-evo gains a 76800-byte
 * INDEX8 tilemap backbuffer, what's the cost to blit it onto the active back
 * surface on no-scroll frames?
 *
 * Pattern reference: tests/probes/l1fill.c (wave 20 cache-curve sweep). MEMBW
 * is l1fill's narrower cousin sized exactly for the path-B decision:
 *
 *   - 76800-byte tests directly map to 320x240 INDEX8 = the engine's back-
 *     buffer size. Path-B's "blit cached tilemap" cost lives at this working-
 *     set size; l1fill's curve-sweep doesn't include 76800 specifically.
 *   - Read / write / memcpy decomposition tells flush-instr whether the path
 *     is read-bound, write-bound, or balanced (informs sub-bracket budget).
 *   - Cache-tier sweep at 8 / 64 / 256 / 512 KB cross-references l1fill's
 *     curve and gives flush-instr's path-B model 4 anchor points (L1 / mid-L2
 *     / L2-edge / main-RAM) without the full 1KB-1MB l1fill sweep.
 *
 * Per perf_predictions_unreliable.md: theoretical cycle counts have been
 * 5-30x wrong for this hardware. The probe MEASURES; the prose below names
 * what the numbers will resolve, not what they will be.
 *
 * Per dosbox_scales_per_code_path.md: DOSBox-X numbers from this probe will
 * NOT match real HW (host memcpy on a modern Linux box vs P54C+L2+30ns DRAM).
 * DOSBox-X smoke = correctness only (probe runs, output parses, no crash).
 *
 * Output: writes to MEMBW.OUT in current dir AND mirrors to stdout, grep-friendly
 * `[membw] ...` format. The fopen-direct path (mirrors l1fill.c / dpmithn.c
 * convention) means MEMBW.OUT lands on disk regardless of how the operator
 * invokes the probe — bare `membw`, `MEMBW`, `MEMBW.EXE`, or `MEMBW.BAT` all
 * produce the same file. Stdout retained so the operator sees output on screen
 * as confirmation. Logback grabs MEMBW.OUT.
 *
 * (Wave-22.5 v1 deviated from convention: stdout-only with BAT-side `>` redirect
 * for capture. Real-HW operator typed bare `membw`, DOS picked MEMBW.EXE before
 * MEMBW.BAT in command-resolution order, redirect never engaged, MEMBW.OUT
 * never landed on disk. Numbers had to be transcribed from operator photos.
 * This v2 fopen-direct rewrite eliminates the failure mode entirely.)
 *
 * Pure DJGPP. No SDL, no engine.
 *
 * LFB-write section (486-class hw-coverage campaign, contract sec.2.2):
 * membw also measures VESA linear-framebuffer write bandwidth -- the metric
 * that most directly predicts doskutsu render fps (the flip is framebuffer
 * -write-bound; the g2k anchor for this number is "--", never measured).
 * It finds an 8bpp LFB-capable VBE mode, sets it with the LFB bit, maps the
 * physical LFB via DPMI + a flat near-pointer, times sysmem->LFB memcpy +
 * raw LFB memset (PIT-timed via uclock; a 486 has no RDTSC), then restores
 * text mode. If no LFB mode exists (a VBE 1.2-only card) it emits
 * LFB_WRITE=UNAVAILABLE -- itself a campaign finding.
 * HAZARD: switches video mode; atexit restores text mode 0x03. Per
 * dosbox_not_proxy.md the mode-set + physical-map path is HW-IO -- DOSBox-X
 * smoke proves parse/no-crash only, NOT the bandwidth number.
 *
 * S3-VIRGE campaign extension (LFB-write SIZE SWEEP + LFB_WRITE_BOUND verdict,
 * plan sec.2.3 P2 + 4.4): on top of the single-point 76800 B LFB write above,
 * membw sweeps the LFB-write transfer size (4K/16K/64K/76800/256K/1M), fits
 * per_call_time = fixed_overhead + n/bandwidth, and emits an explicit
 * LFB_WRITE_BOUND=BANDWIDTH|OVERHEAD|INDETERMINATE verdict -- the Lever-1
 * decider for "does a faster card's raw write bandwidth convert to render fps?"
 * BANDWIDTH-bound => the per-byte bus cost dominates at the engine's working
 * set => faster silicon's bandwidth converts to fps. OVERHEAD-bound => a fixed
 * per-flush cost dominates at 76800 B => raw bandwidth does NOT convert.
 * HARDWARE-AGNOSTIC: pure VBE + DPMI map + flat nearptr, NO chip MMIO, so it
 * runs unchanged on the Cirrus (cell-0 pre-swap anchor) and the S3 ViRGE
 * post-swap. The cross-card LFB-write delta is L1's input. (S3 2D-engine MMIO
 * is a SEPARATE probe -- s3blt.c -- never coupled here.)
 *
 * 8.3 DOS filename: MEMBW.EXE (5+3) — fits per dos_filename_8_3.md.
 *
 * License: MIT.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>      /* fsync */
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <sys/nearptr.h>

/* ============================================================ */
/* Logging — fopen-direct to MEMBW.OUT in cwd + mirror to stdout */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    /* Write to current dir (operator runs from \DOSKUTSU\ on g2k; matches
     * realhw's logback path /media/micheal/DOS/doskutsu/MEMBW.OUT). If that
     * fails (read-only mount edge case) fall back to CF root so we still
     * capture something. Stdout always works regardless. */
    g_log = fopen("MEMBW.OUT", "w");
    if (!g_log) g_log = fopen("C:\\MEMBW.OUT", "w");
}

static void mlog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    /* Stdout side — operator confirmation on screen. */
    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);

    /* File side — fsync per line so partial output survives a crash; matches
     * l1fill.c / dpmithn.c / partial.c convention. */
    if (g_log) {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
        fsync(fileno(g_log));
    }
}

/* ============================================================ */
/* Timing — uclock() for ~838 ns granularity (matches l1fill /  */
/* dpmithn). RDTSC is available on P54C+ but uclock keeps the   */
/* probe portable to 486 hosts the iter MIGHT also run on, and  */
/* batch-of-K timing amortizes the granularity.                 */
/* ============================================================ */

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ============================================================ */
/* Volatile sink — prevents the compiler from eliding read_seq  */
/* (an optimizing compiler with -O2 will hoist a pure summation */
/* loop out of the timing harness without it).                  */
/* ============================================================ */

static volatile uint32_t g_read_sink = 0;

/* ============================================================ */
/* Memory primitives                                            */
/* ============================================================ */

static void op_read_seq(const uint32_t *src, uint32_t n_bytes)
{
    /* Word-at-a-time read; sum into a volatile sink. With -O2, GCC emits
     * roughly `mov eax,[esi]; add edx,eax; add esi,4; cmp; jb` — about
     * 1 cycle/word on Pentium U-pipe under sustained read. Real bandwidth
     * = n_bytes / elapsed (DRAM-side cost dominates outside L1+L2). */
    const uint32_t *end = src + (n_bytes >> 2);
    uint32_t sum = 0;
    while (src < end) {
        sum += *src++;
    }
    g_read_sink = sum;
}

static void op_write_seq(uint8_t *dst, uint32_t n_bytes)
{
    /* memset is the standard "clear back buffer" pattern; -O2 + DJGPP libc
     * emit `rep stos` (or unrolled mov-imm32 for short blocks). This is the
     * NXEngine fillrect / surface-clear primitive we care about. */
    memset(dst, 0xA5, n_bytes);
}

static void op_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n_bytes)
{
    /* DJGPP libc memcpy is `rep movsd` for aligned word-sized ops. This is
     * the path-B "blit cached tilemap to back surface" primitive directly. */
    memcpy(dst, src, n_bytes);
}

/* ============================================================ */
/* One timed test — N samples, each K iters, returns median     */
/* batch-elapsed. flush-instr derives per-call from elapsed/K.  */
/* ============================================================ */

typedef enum { OP_READ, OP_WRITE, OP_COPY } op_kind_t;

static double timed_op(op_kind_t op, uint8_t *dst, const uint8_t *src,
                       uint32_t n_bytes, int K)
{
    double t0 = now_secs();
    switch (op) {
        case OP_READ:
            for (int k = 0; k < K; k++) {
                op_read_seq((const uint32_t *)src, n_bytes);
            }
            break;
        case OP_WRITE:
            for (int k = 0; k < K; k++) {
                op_write_seq(dst, n_bytes);
            }
            break;
        case OP_COPY:
            for (int k = 0; k < K; k++) {
                op_memcpy(dst, src, n_bytes);
            }
            break;
    }
    return now_secs() - t0;
}

/* Run N outer samples to filter scheduler / IRQ-tick noise. Report the
 * median batch elapsed_us + bandwidth derived from the median. Returns the
 * mbps for the summary line. */
static double run_test(const char *name, op_kind_t op,
                       uint8_t *dst, const uint8_t *src,
                       uint32_t n_bytes, int K)
{
    const int N = 10;
    double samples[16];

    /* Warm-up batch (discarded — primes any cache state, demand-page,
     * branch predictor for the inner loop). */
    timed_op(op, dst, src, n_bytes, K > 5 ? 5 : K);

    for (int s = 0; s < N; s++) {
        samples[s] = timed_op(op, dst, src, n_bytes, K);
    }
    qsort(samples, N, sizeof samples[0], dbl_cmp);
    double med = samples[N / 2];
    double elapsed_us = med * 1e6;

    /* Bandwidth: total bytes touched in the K-iter batch / batch elapsed.
     * For OP_COPY, we report the buffer size, not 2x — convention matches
     * l1fill, lets flush-instr derive path-B cost as `76800 / mbps` ms. */
    double mbps = ((double)n_bytes * (double)K) / (med * 1024.0 * 1024.0);

    mlog("[membw] test=%-9s bytes=%-6lu iters=%-5d elapsed_us=%-10.0f mbps=%6.2f",
         name, (unsigned long)n_bytes, K, elapsed_us, mbps);

    return mbps;
}

/* ============================================================ */
/* VESA linear-framebuffer write-bandwidth section               */
/* (486-class campaign contract sec.2.2). PIT-timed via uclock;  */
/* no RDTSC (a 486 has none). Switches video mode -- restores    */
/* text mode 0x03 on the way out + via atexit.                   */
/* ============================================================ */

#define LFB_BYTES 76800u   /* 320x240x8 = doskutsu back-surface payload */

/* ---- LFB-write SIZE SWEEP config (S3-VIRGE campaign, plan sec.2.3 P2 + 4.4)
 * The transfer-size sweep is the Lever-1 decider. Sizes per the plan; iters
 * per size sized for ~2-4 MB per batch so uclock 838 ns granularity is
 * amortized at any plausible LFB rate (ASSUMPTION -- the plan fixes the SIZES
 * but not the iter counts; noted per the task brief). Index 3 == 76800 (the
 * engine working set). The sweep is keyed off these arrays everywhere. */
#define N_SWEEP        6
#define SWEEP_WS_IDX   3            /* index of 76800 = engine back-surface */
#define SWEEP_MAP_MAX  1048576u     /* plan's largest sweep size = most we map */
#define SWEEP_MIN_MAP  262144u      /* need >= 256K mapped for a usable asymptote */
#define SWEEP_NSAMP    5            /* median-of-5 per (size,op); odd, noise-robust */
#define BW_FRACTION_THRESHOLD 0.50  /* bw-term majority at 76800 -> BANDWIDTH-bound */

static const uint32_t SWEEP_SIZES[N_SWEEP] = {
    4096u, 16384u, 65536u, 76800u, 262144u, 1048576u
};
static const int SWEEP_ITERS[N_SWEEP] = {
    512,   192,    48,     32,     12,      4
};

/* Cross-anchor constant (plan sec.2.2): Cirrus banked dosmemput->VRAM 76800 B
 * = 19 MB/s. Carried forward so the probe's own output cross-references the
 * prior silicon. The probe does NOT detect which card it runs on (that would
 * couple it to chip ID); flush-instr owns the authoritative pre-swap anchor
 * freeze (same-instrument Cirrus run preferred, this 19 MB/s the fallback). */
#define CIR_LFB_WRITE_ANCHOR_MBPS 19.0

static int g_text_mode_dirty = 0;

/* Restore the 80x25 colour text mode (INT 10h AX=0003). Idempotent;
 * registered via atexit so a crash mid-graphics-mode still recovers. */
static void restore_text_mode(void)
{
    if (g_text_mode_dirty) {
        __dpmi_regs r;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0003;
        __dpmi_int(0x10, &r);
        g_text_mode_dirty = 0;
    }
}

/* Scan the VBE mode list for the smallest 8bpp packed-pixel mode that
 * exposes a linear framebuffer big enough for LFB_BYTES. Returns the VBE
 * mode number (>0) and fills *out_phys with the LFB physical base; 0 if
 * none exists (a VBE 1.2-only card -- LFB is a VBE 2.0+ feature). */
static uint16_t find_lfb_mode(uint32_t *out_phys, uint16_t *out_xres,
                              uint16_t *out_yres, int *out_vbe_major,
                              uint32_t *out_total_vram)
{
    *out_phys = 0; *out_xres = 0; *out_yres = 0; *out_vbe_major = 0;
    *out_total_vram = 0;

    int sel = 0;
    int seg = __dpmi_allocate_dos_memory(32, &sel);   /* 512-byte DOS buffer */
    if (seg < 0) return 0;
    uint32_t buflin = (uint32_t)seg << 4;

    /* AX=4F00 controller info -- 'VBE2' request signature for VBE 2.0 data. */
    _farpokeb(_dos_ds, buflin + 0, 'V');
    _farpokeb(_dos_ds, buflin + 1, 'B');
    _farpokeb(_dos_ds, buflin + 2, 'E');
    _farpokeb(_dos_ds, buflin + 3, '2');
    for (int i = 4; i < 512; i++) _farpokeb(_dos_ds, buflin + i, 0);

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00;
    r.x.es = (uint16_t)seg;
    r.x.di = 0;
    if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) {
        __dpmi_free_dos_memory(sel);
        return 0;
    }
    uint16_t vbe_ver  = _farpeekw(_dos_ds, buflin + 0x04);
    *out_vbe_major    = (vbe_ver >> 8) & 0xFF;
    /* VbeInfoBlock.TotalMemory at 0x12 is in 64 KB units (1 MB Cirrus -> 16;
     * 4 MB S3 ViRGE -> 64). Bounds the size sweep so we never map past the
     * aperture. */
    uint16_t total_blk = _farpeekw(_dos_ds, buflin + 0x12);
    *out_total_vram   = (uint32_t)total_blk * 65536u;
    uint16_t mp_off   = _farpeekw(_dos_ds, buflin + 0x0E);
    uint16_t mp_seg   = _farpeekw(_dos_ds, buflin + 0x10);
    uint32_t mode_lin = ((uint32_t)mp_seg << 4) + mp_off;

    uint16_t chosen = 0;
    uint32_t chosen_phys = 0;
    uint32_t chosen_pixels = 0xFFFFFFFFul;
    uint16_t cx = 0, cy = 0;

    for (int mi = 0; mi < 256; mi++) {
        uint16_t mode = _farpeekw(_dos_ds, mode_lin + mi * 2);
        if (mode == 0xFFFF) break;

        /* AX=4F01 ModeInfo into offset 256 of the same DOS buffer. */
        for (int i = 256; i < 512; i++) _farpokeb(_dos_ds, buflin + i, 0);
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F01;
        r.x.cx = mode;
        r.x.es = (uint16_t)seg;
        r.x.di = 256;
        if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) continue;

        uint32_t mb   = buflin + 256;
        uint16_t attr = _farpeekw(_dos_ds, mb + 0x00);
        uint16_t xres = _farpeekw(_dos_ds, mb + 0x12);
        uint16_t yres = _farpeekw(_dos_ds, mb + 0x14);
        uint8_t  bpp  = _farpeekb(_dos_ds, mb + 0x19);
        uint8_t  model= _farpeekb(_dos_ds, mb + 0x1B);
        uint32_t phys = _farpeekl(_dos_ds, mb + 0x28);

        if (!(attr & 0x0001)) continue;   /* mode not supported by HW */
        if (!(attr & 0x0080)) continue;   /* no linear-framebuffer support */
        if (bpp != 8)         continue;   /* 8bpp = doskutsu's INDEX8 path */
        if (model != 4)       continue;   /* 4 = packed pixel */
        if (phys == 0)        continue;   /* no usable LFB aperture */
        uint32_t pixels = (uint32_t)xres * (uint32_t)yres;
        if (pixels < LFB_BYTES) continue; /* framebuffer too small */
        if (pixels < chosen_pixels) {     /* prefer smallest -> cheap set */
            chosen = mode; chosen_phys = phys;
            chosen_pixels = pixels; cx = xres; cy = yres;
        }
    }
    __dpmi_free_dos_memory(sel);
    if (chosen) { *out_phys = chosen_phys; *out_xres = cx; *out_yres = cy; }
    return chosen;
}

/* Median per-call time (us) for one op at one size: batched K iters x NSAMP
 * samples, return median-batch / K. NO logging -- the caller invokes this
 * mid-graphics-mode (stdout would corrupt the screen); results are emitted
 * after text mode is restored. Returns -1 on a non-positive timer reading. */
static double timed_percall_us(op_kind_t op, uint8_t *dst, const uint8_t *src,
                               uint32_t n, int K)
{
    double s[16];
    int ns = SWEEP_NSAMP;
    if (ns > (int)(sizeof s / sizeof s[0])) ns = (int)(sizeof s / sizeof s[0]);
    timed_op(op, dst, src, n, K > 5 ? 5 : K);          /* warm-up (discarded) */
    for (int i = 0; i < ns; i++) s[i] = timed_op(op, dst, src, n, K);
    qsort(s, ns, sizeof s[0], dbl_cmp);
    double med = s[ns / 2];
    return (med > 0.0) ? (med / (double)K) * 1e6 : -1.0;
}

/* Emit the LFB-write size-sweep curve + the LFB_WRITE_BOUND verdict. Called
 * AFTER text mode is restored (does mlog). Inputs are per-call us per size for
 * memset (write) and memcpy (copy); n_active is how many swept sizes ran.
 *
 * Verdict model (uncached VRAM -- no cache term, unlike sysmem memcpy):
 *   per_call_us(n) = fixed_overhead_us + n / bandwidth
 * Least-squares fit over the MEMSET points (the pure write path -- the side a
 * card swap actually changes; a copy's sysmem read is card-independent) gives
 * fixed_overhead + asymptotic bandwidth. At 76800 B the bandwidth-term fraction
 * = (76800/bw) / (fixed_overhead + 76800/bw): >= threshold => BANDWIDTH-bound. */
static void emit_lfb_sweep(const double *ms_us, const double *cp_us, int n_active)
{
    mlog("[membw] ---- LFB-write SIZE SWEEP (S3 campaign Lever-1 decider) ----");
    for (int i = 0; i < n_active; i++) {
        double w_mbps = (ms_us[i] > 0.0)
            ? ((double)SWEEP_SIZES[i] / ms_us[i]) * 1e6 / (1024.0 * 1024.0) : -1.0;
        double c_mbps = (cp_us[i] > 0.0)
            ? ((double)SWEEP_SIZES[i] / cp_us[i]) * 1e6 / (1024.0 * 1024.0) : -1.0;
        mlog("[membw] LFBSW size=%-8lu memset_us=%-9.2f memset_mbps=%-7.2f "
             "memcpy_us=%-9.2f memcpy_mbps=%-7.2f",
             (unsigned long)SWEEP_SIZES[i], ms_us[i], w_mbps, cp_us[i], c_mbps);
    }

    /* Least-squares fit per_call_us = a + slope*n over valid memset points. */
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
    for (int i = 0; i < n_active; i++) {
        if (ms_us[i] <= 0.0) continue;
        double x = (double)SWEEP_SIZES[i], y = ms_us[i];
        sx += x; sy += y; sxx += x * x; sxy += x * y; m++;
    }
    if (m < 3) {
        mlog("[membw] LFB_WRITE_BOUND=INDETERMINATE (%d valid memset points; need "
             ">=3 for a fit)", m);
        return;
    }
    double denom = (double)m * sxx - sx * sx;
    double slope = (denom != 0.0) ? ((double)m * sxy - sx * sy) / denom : 0.0; /* us/byte */
    double intercept = (sy - slope * sx) / (double)m;                          /* us */

    /* R^2 -- flags a noisy curve (an uncached-VRAM sweep should fit cleanly). */
    double ybar = sy / (double)m, ss_tot = 0, ss_res = 0;
    for (int i = 0; i < n_active; i++) {
        if (ms_us[i] <= 0.0) continue;
        double yhat = intercept + slope * (double)SWEEP_SIZES[i];
        ss_res += (ms_us[i] - yhat) * (ms_us[i] - yhat);
        ss_tot += (ms_us[i] - ybar) * (ms_us[i] - ybar);
    }
    double r2 = (ss_tot > 0.0) ? 1.0 - ss_res / ss_tot : -1.0;

    if (slope <= 0.0) {
        mlog("[membw] LFB_SWEEP_FIT slope=%.6e us/byte (NON-POSITIVE) "
             "fixed_overhead_us=%.2f r2=%.3f", slope, intercept, r2);
        mlog("[membw] LFB_WRITE_BOUND=INDETERMINATE (throughput did not rise with "
             "size -- timer noise or a host-memory LFB saturating (DOSBox-X))");
        return;
    }

    double bw_mbps = (1e6 / slope) / (1024.0 * 1024.0);
    double a_us = intercept < 0.0 ? 0.0 : intercept;     /* clamp tiny-negative noise */
    double bw_term_us = 76800.0 * slope;
    double total_us = a_us + bw_term_us;
    double frac = (total_us > 0.0) ? bw_term_us / total_us : -1.0;

    /* Empirical saturation cross-check: mbps(76800) / mbps(largest swept). */
    double mbps_ws = (SWEEP_WS_IDX < n_active && ms_us[SWEEP_WS_IDX] > 0.0)
        ? ((double)SWEEP_SIZES[SWEEP_WS_IDX] / ms_us[SWEEP_WS_IDX]) * 1e6 / (1024.0 * 1024.0)
        : -1.0;
    int last = n_active - 1;
    double mbps_max = (ms_us[last] > 0.0)
        ? ((double)SWEEP_SIZES[last] / ms_us[last]) * 1e6 / (1024.0 * 1024.0) : -1.0;
    double saturation = (mbps_max > 0.0 && mbps_ws > 0.0) ? mbps_ws / mbps_max : -1.0;

    mlog("[membw] LFB_SWEEP_FIT asymptotic_mbps=%.2f fixed_overhead_us=%.2f r2=%.3f "
         "(linear fit per_call_us=a+n/bw over %d pts)", bw_mbps, intercept, r2, m);
    mlog("[membw] LFB_WORKSET bw_fraction=%.2f saturation=%.2f mbps76800=%.2f "
         "mbps_max=%.2f (fraction = bandwidth-term share of the 76800 B per-flush time)",
         frac, saturation, mbps_ws, mbps_max);

    const char *verdict = (frac >= BW_FRACTION_THRESHOLD) ? "BANDWIDTH" : "OVERHEAD";
    mlog("[membw] LFB_WRITE_BOUND=%s (threshold bw_fraction>=%.2f; BANDWIDTH => a "
         "faster card's write bandwidth converts to fps; OVERHEAD => a fixed per-flush "
         "cost dominates at 76800 B, raw bandwidth does NOT convert)",
         verdict, BW_FRACTION_THRESHOLD);

    /* Cross-anchor vs the Cirrus banked-dosmemput reference (plan sec.2.2). */
    if (mbps_ws > 0.0) {
        mlog("[membw] LFB_WRITE_VS_CIR_ANCHOR ratio=%.2f (LFB memset@76800 %.2f MB/s / "
             "Cirrus banked anchor %.1f MB/s; on a Cirrus run = self-consistency, on the "
             "S3 = the cross-card L1 delta; flush-instr owns the authoritative freeze)",
             mbps_ws / CIR_LFB_WRITE_ANCHOR_MBPS, mbps_ws, CIR_LFB_WRITE_ANCHOR_MBPS);
    }

    /* Plausibility bounds (probe_authoring_discipline -- flag, do not assert). */
    if (bw_mbps > 100000.0) {
        mlog("[membw] LFB_IMPLAUSIBLE: asymptotic_mbps > 100 GB/s -- emulator host-speed "
             "artifact (dosbox_not_proxy), NOT real-HW LFB bandwidth; verdict meaningless");
    }
    if (r2 >= 0.0 && r2 < 0.90) {
        mlog("[membw] LFB_FIT_NOISY: r2=%.3f < 0.90 -- size curve noisy; treat the "
             "BANDWIDTH/OVERHEAD verdict as low-confidence, eyeball the LFBSW lines", r2);
    }
    if (saturation > 0.0 && frac > 0.0 &&
        (saturation - frac > 0.20 || frac - saturation > 0.20)) {
        mlog("[membw] LFB_XCHECK_DIVERGE: fit bw_fraction=%.2f vs empirical saturation=%.2f "
             "differ >0.20 -- fit may be noise-biased; prefer the LFBSW per-size lines",
             frac, saturation);
    }
}

/* Measure LFB write bandwidth: raw memset (pure VRAM write) + sysmem->LFB
 * memcpy (the render-path primitive). Emits MB/s + a sentinel readback that
 * confirms the writes physically landed (a wrong mapping would still time
 * "fast" while writing nowhere -- the sentinel catches that). Then runs the
 * transfer-size sweep + LFB_WRITE_BOUND verdict (S3-VIRGE campaign Lever-1).
 *
 * sysmem_write_mbps (the prior sysmem write_seq @ 76800) is passed in so the
 * probe can emit the LFB-vs-sysmem-write ceiling ratio -- the sharper L1
 * discriminator: the size sweep says whether cost is per-byte vs per-call, but
 * a per-byte cost can be CPU-copy-loop-bound (card-independent) OR VRAM-bus-
 * bound (card-dependent). LFB_write << sysmem_write => VRAM-bus-bound => a
 * faster card has headroom; LFB_write ~= sysmem_write => CPU-bound => it does
 * not. (podp83 anchor: Cirrus sysmem write_seq 40.9 vs LFB 19 MB/s.) */
static void lfb_write_test(const uint8_t *src, double sysmem_write_mbps)
{
    mlog("[membw] ---- LFB write bandwidth (sysmem->VESA linear framebuffer) ----");

    uint32_t phys = 0, total_vram = 0; uint16_t xres = 0, yres = 0; int vbe_major = 0;
    uint16_t mode = find_lfb_mode(&phys, &xres, &yres, &vbe_major, &total_vram);
    if (mode == 0) {
        mlog("[membw] LFB_WRITE=UNAVAILABLE (no 8bpp linear-framebuffer VBE mode; "
             "vbe_major=%d; VBE 1.2 cards have no LFB -- this is a campaign finding)",
             vbe_major);
        return;
    }
    mlog("[membw] LFB_MODE=0x%04X %ux%u 8bpp phys=0x%08lX vbe_major=%d total_vram=%lu",
         mode, xres, yres, (unsigned long)phys, vbe_major, (unsigned long)total_vram);

    /* Map budget: cap at the plan's 1 MB sweep max AND at total VRAM so we never
     * map past the aperture. Cirrus (1 MB) -> 1 MB; S3 ViRGE (4 MB) -> 1 MB. If
     * the controller reported no VRAM, fall back to the 76800 B anchor map (the
     * sweep then reports INDETERMINATE -- a clean signal, not a crash). */
    uint32_t budget = (total_vram >= LFB_BYTES) ? total_vram : LFB_BYTES;
    if (budget > SWEEP_MAP_MAX) budget = SWEEP_MAP_MAX;
    uint32_t map_size = LFB_BYTES;
    int n_active = 0;
    for (int i = 0; i < N_SWEEP; i++) {
        if (SWEEP_SIZES[i] <= budget) {
            n_active = i + 1;
            if (SWEEP_SIZES[i] > map_size) map_size = SWEEP_SIZES[i];
        }
    }

    /* Set the mode WITH the linear-framebuffer bit (BX bit 14) before mapping
     * -- on many cards the LFB aperture only decodes once the mode is set. */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = mode | 0x4000;
    __dpmi_int(0x10, &r);
    g_text_mode_dirty = 1;
    int set_ok = (r.x.ax == 0x004F);

    __dpmi_meminfo info;
    memset(&info, 0, sizeof info);
    info.address = phys;
    info.size = map_size;
    if (__dpmi_physical_address_mapping(&info) != 0) {
        /* Retry with just the anchor size -- a 1 MB map can fail where 76800
         * succeeds on a tight-aperture card; the anchor numbers still land,
         * the sweep then reports INDETERMINATE. */
        memset(&info, 0, sizeof info);
        info.address = phys;
        info.size = LFB_BYTES;
        if (__dpmi_physical_address_mapping(&info) != 0) {
            restore_text_mode();
            mlog("[membw] LFB_WRITE=UNAVAILABLE (DPMI physical_address_mapping failed "
                 "for phys=0x%08lX)", (unsigned long)phys);
            return;
        }
        map_size = LFB_BYTES;
        n_active = 0;
        for (int i = 0; i < N_SWEEP; i++)
            if (SWEEP_SIZES[i] <= LFB_BYTES) n_active = i + 1;
    }
    if (!__djgpp_nearptr_enable()) {
        __dpmi_free_physical_address_mapping(&info);
        restore_text_mode();
        mlog("[membw] LFB_WRITE=UNAVAILABLE (nearptr_enable failed; DPMI host "
             "enforces memory protection -- cannot get a flat LFB pointer)");
        return;
    }
    uint8_t *lfb = (uint8_t *)(info.address + __djgpp_conventional_base);

    /* ===== all timing runs WHILE in graphics mode; NO mlog here (mlog still
     * writes MEMBW.OUT correctly, but stdout would corrupt the screen) --
     * collect into arrays, restore text mode, then emit. ===== */

    /* (1) Anchor block at 76800 B -- keeps the established LFB_WRITE_MBPS /
     * LFB_COPY_MBPS / LFB_sentinel line names that flush-instr's anchor grep
     * and the smoke gate depend on. */
    const int N = 10;
    const int K = 32;
    double ws[16], cs[16];
    timed_op(OP_WRITE, lfb, src, LFB_BYTES, 5);                /* warm-up */
    for (int s = 0; s < N; s++) ws[s] = timed_op(OP_WRITE, lfb, src, LFB_BYTES, K);
    timed_op(OP_COPY, lfb, src, LFB_BYTES, 5);                 /* warm-up */
    for (int s = 0; s < N; s++) cs[s] = timed_op(OP_COPY, lfb, src, LFB_BYTES, K);

    /* Sentinel: a wrong physical mapping still times fast but writes nowhere. */
    memset(lfb, 0xA5, 16);
    volatile uint8_t sent_set = lfb[0];
    lfb[1] = 0x5A;
    volatile uint8_t sent_byte = lfb[1];

    /* (2) Size sweep -- the Lever-1 decider. Collect per-call us per size into
     * arrays; emit after restore. Only runs with >= 256K mapped (need points
     * above the working set for a usable asymptote). */
    double sweep_ms_us[N_SWEEP], sweep_cp_us[N_SWEEP];
    for (int i = 0; i < N_SWEEP; i++) { sweep_ms_us[i] = -1.0; sweep_cp_us[i] = -1.0; }
    int run_sweep = (map_size >= SWEEP_MIN_MAP && n_active >= 3);
    if (run_sweep) {
        for (int i = 0; i < n_active; i++) {
            sweep_ms_us[i] = timed_percall_us(OP_WRITE, lfb, src,
                                              SWEEP_SIZES[i], SWEEP_ITERS[i]);
            sweep_cp_us[i] = timed_percall_us(OP_COPY, lfb, src,
                                              SWEEP_SIZES[i], SWEEP_ITERS[i]);
        }
    }

    __djgpp_nearptr_disable();
    restore_text_mode();
    __dpmi_free_physical_address_mapping(&info);

    /* ===== back in text mode -- safe to mlog ===== */
    qsort(ws, N, sizeof ws[0], dbl_cmp);
    qsort(cs, N, sizeof cs[0], dbl_cmp);
    double w_med = ws[N / 2], c_med = cs[N / 2];
    double w_mbps = (w_med > 0.0)
        ? ((double)LFB_BYTES * (double)K) / (w_med * 1024.0 * 1024.0) : -1.0;
    double c_mbps = (c_med > 0.0)
        ? ((double)LFB_BYTES * (double)K) / (c_med * 1024.0 * 1024.0) : -1.0;

    if (!set_ok) {
        mlog("[membw] LFB_WARN: AX=4F02 set-mode returned AX!=0x004F -- numbers suspect");
    }
    mlog("[membw] LFB_MAP_SIZE=%lu n_sweep_sizes=%d (budget from total_vram, capped 1MB)",
         (unsigned long)map_size, run_sweep ? n_active : 0);
    mlog("[membw] test=LFB_memset bytes=%-6lu iters=%-5d elapsed_us=%-10.0f mbps=%6.2f",
         (unsigned long)LFB_BYTES, K, w_med * 1e6, w_mbps);
    mlog("[membw] test=LFB_copy   bytes=%-6lu iters=%-5d elapsed_us=%-10.0f mbps=%6.2f",
         (unsigned long)LFB_BYTES, K, c_med * 1e6, c_mbps);
    mlog("[membw] LFB_WRITE_MBPS=%.2f LFB_COPY_MBPS=%.2f "
         "(LFB_COPY = sysmem->LFB = the render-fps predictor)", w_mbps, c_mbps);
    mlog("[membw] LFB_sentinel=0x%02X/0x%02X (expect 0xA5/0x5A; any other value "
         "=> bad mapping, LFB mbps meaningless)", sent_set, sent_byte);

    /* L1 ceiling discriminator: LFB write vs the CPU's sysmem write rate. The
     * size sweep tells per-byte-vs-per-call; THIS tells whether a per-byte LFB
     * cost is VRAM-bus-bound (a faster card has headroom, ratio<<1) or CPU-copy-
     * loop-bound (a faster card does NOT help, ratio~=1). */
    if (w_mbps > 0.0 && sysmem_write_mbps > 0.0) {
        mlog("[membw] LFB_VS_SYSMEM_WRITE ratio=%.2f (LFB write %.2f / sysmem write "
             "%.2f MB/s). ratio<<1 => VRAM-bus-bound: a faster card can lift LFB write "
             "toward the sysmem ceiling (= the L1 headroom). ratio~=1 => CPU-copy-loop-"
             "bound: a faster card does NOT help (the CPU is the wall, not the VRAM).",
             w_mbps / sysmem_write_mbps, w_mbps, sysmem_write_mbps);
    }

    /* Plausibility bound (probe_authoring_discipline): flag, do not assert --
     * the operator still gets the raw numbers to judge. */
    if (w_mbps <= 0.0 || c_mbps <= 0.0) {
        mlog("[membw] LFB_IMPLAUSIBLE: non-positive mbps -- timer granularity or "
             "set-mode failure; treat LFB numbers as invalid");
    } else if (w_mbps > 100000.0 || c_mbps > 100000.0) {
        mlog("[membw] LFB_IMPLAUSIBLE: mbps > 100 GB/s -- emulator host-speed "
             "artifact (dosbox_not_proxy), not real-HW LFB bandwidth");
    }
    if (sent_set != 0xA5 || sent_byte != 0x5A) {
        mlog("[membw] LFB_SENTINEL_FAIL: readback mismatch -- LFB writes did NOT "
             "land; the LFB mbps numbers above are meaningless");
    }

    /* (3) Size-sweep curve + LFB_WRITE_BOUND verdict (the Lever-1 deliverable). */
    if (run_sweep) {
        emit_lfb_sweep(sweep_ms_us, sweep_cp_us, n_active);
    } else {
        mlog("[membw] LFB_WRITE_BOUND=INDETERMINATE (size sweep skipped: mapped %lu B "
             "< %lu B min; controller VRAM report = %lu B)",
             (unsigned long)map_size, (unsigned long)SWEEP_MIN_MAP,
             (unsigned long)total_vram);
    }
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    atexit(restore_text_mode);   /* recover text mode if LFB test crashes */
    mlog("[membw] platform=x86 timing=uclock uclocks_per_sec=%lu",
         (unsigned long)UCLOCKS_PER_SEC);
    mlog("[membw] purpose=phase11-wave-22.5-path-B-decision + 486-class-campaign-LFB-write");
    mlog("[membw] output=MEMBW.OUT (cwd-relative; fopen-direct, no shell redirect needed)");

    /* Allocate two 1 MB buffers on the protected-mode heap (DJGPP malloc
     * serves from XMS via CWSDPMI). Per brief: not stack — DPMI default
     * stack is ~256 KB, smaller than the 512 KB sweep size. */
    const uint32_t MAX = 1024u * 1024u;  /* 1 MB each — covers 512 KB tier */
    uint8_t *src = (uint8_t *)malloc(MAX);
    uint8_t *dst = (uint8_t *)malloc(MAX);
    if (!src || !dst) {
        mlog("[membw] FATAL: malloc(2x1MB) failed (XMS exhausted?)");
        free(src); free(dst);
        if (g_log) fclose(g_log);
        return 2;
    }

    /* Touch every 4 KB page so the first batch's demand-paging cost doesn't
     * bias the warm-up. Initialize src with non-trivial pattern so reads
     * accumulate to a non-zero sink (catches "the loop got optimized away"). */
    for (uint32_t i = 0; i < MAX; i += 4096) {
        src[i] = (uint8_t)(i & 0xFF);
        dst[i] = 0;
    }
    for (uint32_t i = 0; i < MAX; i += 16) {
        src[i] = (uint8_t)((i >> 4) & 0xFF);
    }

    mlog("[membw] buffers src=%p dst=%p alloc_size=%lu_each (page-touched)",
         src, dst, (unsigned long)MAX);

    /* ======== Path-B-sized tests at 76800 B ======== */
    /* 76800 B = 320 x 240 INDEX8 = engine back-surface size. iters=1000
     * targets ~1-3 sec per test. Read / write / copy decomposed to identify
     * which side dominates real-HW bandwidth. */
    mlog("[membw] ---- 76800 B (320x240 INDEX8 = engine back-surface size) ----");
    double mbps_read   = run_test("read_seq",  OP_READ,  dst, src, 76800, 1000);
    double mbps_write  = run_test("write_seq", OP_WRITE, dst, src, 76800, 1000);
    double mbps_memcpy = run_test("memcpy",    OP_COPY,  dst, src, 76800, 1000);

    /* ======== Cache-tier sweep (memcpy) ======== */
    /* Anchors flush-instr's path-B model with 4 cache-tier points. Iter
     * counts target ~50-500 ms per batch given expected per-tier MB/s. */
    mlog("[membw] ---- cache-tier sweep (memcpy: read+write same tier) ----");
    double mbps_L1     = run_test("L1_swp",    OP_COPY, dst, src,   8192, 10000);
    double mbps_L2     = run_test("L2_swp",    OP_COPY, dst, src,  65536,  1000);
    double mbps_L2max  = run_test("L2_full",   OP_COPY, dst, src, 262144,   100);
    double mbps_main   = run_test("main_ram",  OP_COPY, dst, src, 524288,    10);

    /* ======== LFB write bandwidth (486-class campaign contract sec.2.2) ======== */
    /* The render-path-decisive metric: MB/s writing into the VESA linear
     * framebuffer. Switches video mode internally; restores text mode before
     * returning so the summary below renders correctly. mbps_write (sysmem
     * write_seq @ 76800) is passed so the LFB section can emit the LFB-vs-
     * sysmem write ceiling ratio (the L1 headroom discriminator). */
    lfb_write_test(src, mbps_write);

    /* ======== Summary ======== */
    mlog("[membw] summary L1=%.0f L2=%.0f L2_full=%.0f main=%.0f memcpy76k=%.1f read76k=%.1f write76k=%.1f (all MB/s)",
         mbps_L1, mbps_L2, mbps_L2max, mbps_main,
         mbps_memcpy, mbps_read, mbps_write);

    /* Catch the "compiler elided the read loop" footgun: if the sink is 0
     * after thousands of reads of a non-zero buffer, the loop got optimized
     * out. Print a sanity check so flush-instr can spot a regression. */
    mlog("[membw] read_sink=0x%08lX (non-zero confirms read_seq loop ran)",
         (unsigned long)g_read_sink);

    free(src);
    free(dst);

    mlog("[membw] done");
    mlog("");
    mlog("Reading the output for path-B (wave-22.5 / wave-23):");
    mlog("  memcpy76k MB/s -> path-B blit-cached-tilemap cost = 0.073 / mbps sec");
    mlog("    e.g. 50 MB/s -> 1.5 ms per cached blit; 10 MB/s -> 7.3 ms");
    mlog("  read76k vs write76k -> tells whether path-B bottleneck is read or write");
    mlog("  L1 vs L2 vs main -> validates cache hierarchy assumptions in flush-instr's");
    mlog("    model (76800 B fits in PODP L2 = 256 KB; bandwidth should track L2_swp).");
    mlog("  LFB_COPY_MBPS -> the render-fps predictor: per-flip cost ~= 0.073 / mbps sec.");
    mlog("    LFB_WRITE=UNAVAILABLE means a VBE 1.2-only card (no linear framebuffer)");
    mlog("    -- itself a campaign finding for that machine's render path.");
    mlog("  LFB_WRITE_BOUND=BANDWIDTH|OVERHEAD (S3 campaign Lever-1 decider):");
    mlog("    BANDWIDTH -> the LFB write path is per-byte bus-limited at 76800 B; a faster");
    mlog("      card (S3 EDO+PCI) raises the ceiling -> raw bandwidth converts to fps.");
    mlog("    OVERHEAD  -> a fixed per-flush cost dominates at 76800 B; faster VRAM");
    mlog("      bandwidth does NOT convert (a dumb-flush rebaseline buys ~0 fps).");
    mlog("    Compare the LFBSW per-size lines across cards (Cirrus cell-0 vs S3) for L1.");
    mlog("  Note: on a 486 the L1_swp/L2_swp labels are nominal (8-16 KB L1, optional");
    mlog("    motherboard L2); the per-working-set bandwidth numbers stay valid.");

    if (g_log) fclose(g_log);
    return 0;
}
