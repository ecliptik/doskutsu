/*
 * mode13h-probe.c -- Mode 13h packed-pixel bandwidth probe
 * (Phase 11 wave-36 ceiling-bust probe B).
 *
 * GOAL: surface whether the dosmemput bandwidth bottleneck is BANK-SWITCH
 * overhead, PER-BYTE CPU PIO, or both. Mode 13h (320x200x8 packed-pixel)
 * fits the entire 64000-byte framebuffer in a single 64 KB segment at
 * 0xA000:0 -- no bank-switch required. Compare dosmemput throughput in
 * Mode 13h vs banked-mode anchor (BLTFILL Scenario B = 19.6 MB/s for 76800
 * bytes = 3.738 ms; pro-rated to 64000 bytes = ~3.11 ms baseline).
 *
 * SCENARIOS:
 *   A. dosmemput sysmem -> Mode 13h 0xA000:0 (no bank-switch; 64000 bytes)
 *   B. memcpy via nearptr -> Mode 13h 0xA000:0 (no DPMI thunk per call)
 *   C. memset via nearptr 0xAA -> Mode 13h 0xA000:0 (fill ceiling)
 *
 * Per-test: 100 reps, RDTSC-timed, MB/s + ms median emit. Verify byte-match
 * on dst spot-check (first 16 + last 16) for B + C.
 *
 * Decision gates (team-lead task):
 *   A (dosmemput Mode 13h) > 1.3x BLTFILL-anchor banked dosmemput pro-rated
 *     -> bank-switch overhead is the bottleneck; Mode 13h is a wave-37
 *        candidate (lose 40 px vertical but gain bandwidth)
 *   A approx anchor (within 10%) -> per-byte CPU PIO IS the bottleneck;
 *     bank-switch was free; Mode 13h doesn't help bandwidth
 *   B (nearptr memcpy) faster than A by significant margin -> per-byte DPMI
 *     thunk through dosmemput is the bottleneck (independent of bank-switch);
 *     wave-37 candidate is "nearptr memcpy" not "Mode 13h"
 *   C confirms chip-write ceiling under Mode 13h vs LFB conditions
 *
 * Cross-anchor against LFBNEAR-probe (sibling): if LFB nearptr matches Mode 13h
 * nearptr, the ceiling is sysmem->VRAM CPU bandwidth, not bank-switch.
 *
 * Plausibility bounds (generous; aim is to surface "this can't be right"
 * numbers, not gate on tight expected real-HW range):
 *   - cpu_mhz in [50, 5000]
 *   - all *_mbs in [5, 300]
 *
 * Verdict gates on speedup ratios, not absolute MB/s. DOSBox-X emulates
 * Mode 13h VRAM as host RAM; numbers won't match real HW. Real-HW iter
 * is the data gate.
 *
 * 8.3 DOS filenames:
 *   Source:   tests/probes/mode13h-probe.c
 *   Binary:   MODE13H.EXE (7+3)
 *   Log:      MODE13H.LOG (7+3)
 *   BAT:      MODE13H.BAT (7+3)
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
#include <sys/nearptr.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("MODE13H.LOG", "w");
    if (!g_log) g_log = fopen("C:\\MODE13H.LOG", "w");
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
/* RDTSC + cpu_mhz calibration                                   */
/* ============================================================ */

static double g_us_per_cycle = 0.0;
static uint32_t g_cpu_mhz = 0;

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

/* ============================================================ */
/* INT 0x10 mode helpers                                         */
/* ============================================================ */

/* Set mode 13h via INT 0x10 AX=0x0013. */
static int int10_set_mode_13h(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0013;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return 0;
}

/* Restore text mode via INT 0x10 AX=0x0003. */
static void text_mode_restore(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
}

/* ============================================================ */
/* Geometry                                                      */
/* ============================================================ */

#define MODE13H_BYTES   64000L  /* 320 x 200 */

/* ============================================================ */
/* Stat helper                                                   */
/* ============================================================ */

#define N_REPS 100

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

typedef struct { double min, med, p95, max, mean; } stats_t;

static void compute_stats(double *samples, int n, stats_t *out)
{
    qsort(samples, n, sizeof samples[0], dbl_cmp);
    out->min  = samples[0];
    out->max  = samples[n - 1];
    out->med  = samples[n / 2];
    out->p95  = samples[(int)(0.95 * n)];
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += samples[i];
    out->mean = sum / n;
}

/* ============================================================ */
/* Verify spot-check                                             */
/* ============================================================ */

static const char *verify_dosmemput(uint8_t *dst_readback)
{
    /* dst_readback is a sysmem buffer the caller filled via dosmemget from
     * Mode 13h VRAM. Check first 16 + last 16 vs linear gradient. */
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)(j & 0xFF);
        if (dst_readback[j] != exp) {
            plog("  verify FAIL: vram[%d] = 0x%02X (expected 0x%02X)",
                 j, (unsigned)dst_readback[j], (unsigned)exp);
            return "FAIL_FIRST";
        }
    }
    long off = MODE13H_BYTES - 16;
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)((off + j) & 0xFF);
        if (dst_readback[off + j] != exp) {
            plog("  verify FAIL: vram[%ld] = 0x%02X (expected 0x%02X)",
                 off + j, (unsigned)dst_readback[off + j], (unsigned)exp);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

static const char *verify_nearptr_linear_gradient(volatile uint8_t *vram)
{
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)(j & 0xFF);
        if (vram[j] != exp) {
            plog("  verify FAIL: nearptr_vram[%d] = 0x%02X (expected 0x%02X)",
                 j, (unsigned)vram[j], (unsigned)exp);
            return "FAIL_FIRST";
        }
    }
    long off = MODE13H_BYTES - 16;
    for (int j = 0; j < 16; j++) {
        uint8_t exp = (uint8_t)((off + j) & 0xFF);
        if (vram[off + j] != exp) {
            plog("  verify FAIL: nearptr_vram[%ld] = 0x%02X (expected 0x%02X)",
                 off + j, (unsigned)vram[off + j], (unsigned)exp);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

static const char *verify_nearptr_memset(volatile uint8_t *vram, uint8_t fill)
{
    for (int j = 0; j < 16; j++) {
        if (vram[j] != fill) {
            plog("  verify FAIL: nearptr_vram[%d] = 0x%02X (expected 0x%02X)",
                 j, (unsigned)vram[j], (unsigned)fill);
            return "FAIL_FIRST";
        }
    }
    long off = MODE13H_BYTES - 16;
    for (int j = 0; j < 16; j++) {
        if (vram[off + j] != fill) {
            plog("  verify FAIL: nearptr_vram[%ld] = 0x%02X (expected 0x%02X)",
                 off + j, (unsigned)vram[off + j], (unsigned)fill);
            return "FAIL_LAST";
        }
    }
    return "PASS";
}

/* ============================================================ */
/* Plausibility                                                  */
/* ============================================================ */

static int plausible(double v, double lo, double hi)
{
    return (v >= lo && v <= hi);
}

static double mbs_from_ms(double ms_per_op, long bytes_per_op)
{
    if (ms_per_op <= 0.0) return 0.0;
    return ((double)bytes_per_op * 1000.0) / (1048576.0 * ms_per_op);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== mode13h-probe v1 (wave-36 ceiling-bust B) starting ===");
    plog("DJGPP pure-C; target: Mode 13h packed-pixel bandwidth (no bank-switch)");
    plog("Hypothesis: dosmemput ceiling may be bank-switch cost, per-byte CPU PIO,");
    plog("or both. Mode 13h's single-bank 64000-byte FB isolates the bank-switch axis.");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");

    /* ----- Step 1: RDTSC calibration ----- */
    plog("---- Step 1: RDTSC calibration ----");
    calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u  us_per_cycle = %.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    int cpu_mhz_ok = plausible((double)g_cpu_mhz, 50.0, 5000.0);
    plog("");

    /* ----- Step 2: set Mode 13h ----- */
    plog("---- Step 2: INT 0x10 AX=0x0013 (Mode 13h 320x200x8 packed) ----");
    if (int10_set_mode_13h() != 0) {
        plog("FAIL: cannot set Mode 13h via INT 0x10");
        plog("[mode13h-probe DONE VERDICT=FAIL_MODE_SET reason=int10_failed]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("Mode 13h set OK.");
    plog("");

    /* ----- Step 3: __djgpp_nearptr_enable for direct 0xA000:0 access ----- */
    plog("---- Step 3: __djgpp_nearptr_enable (for direct 0xA000:0 access) ----");
    int nearptr_enabled = __djgpp_nearptr_enable();
    if (!nearptr_enabled) {
        plog("WARN: __djgpp_nearptr_enable failed; nearptr scenarios B+C will SKIP.");
    } else {
        plog("nearptr enabled.  __djgpp_base_address = 0x%08lX",
             (unsigned long)__djgpp_base_address);
    }
    volatile uint8_t *vram_nearptr =
        (volatile uint8_t *)(0xA0000UL - __djgpp_base_address);
    plog("VRAM nearptr base = 0x%08lX (= 0xA0000 - 0x%08lX)",
         (unsigned long)(uintptr_t)vram_nearptr,
         (unsigned long)__djgpp_base_address);
    plog("");

    /* ----- Step 4: sysmem source buffer (linear gradient i&0xFF) ----- */
    plog("---- Step 4: sysmem source buffer (linear gradient) ----");
    uint8_t *src = (uint8_t *)malloc(MODE13H_BYTES);
    uint8_t *readback = (uint8_t *)malloc(MODE13H_BYTES);
    if (!src || !readback) {
        plog("FATAL: malloc failed");
        if (nearptr_enabled) __djgpp_nearptr_disable();
        text_mode_restore();
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 3;
    }
    for (long i = 0; i < MODE13H_BYTES; i++) src[i] = (uint8_t)(i & 0xFF);
    plog("Pre-fill done (%ld bytes).", MODE13H_BYTES);
    plog("");

    /* ----- Step 5: Scenario A (dosmemput sysmem -> Mode 13h 0xA000:0) ----- */
    plog("---- Step 5: Scenario A (dosmemput -> Mode 13h x %d) ----", N_REPS);
    double *samples_A = (double *)malloc(sizeof(double) * N_REPS);

    /* Warmup. */
    dosmemput(src, MODE13H_BYTES, 0xA0000UL);

    for (int s = 0; s < N_REPS; s++) {
        uint64_t c0 = rdtsc();
        dosmemput(src, MODE13H_BYTES, 0xA0000UL);
        uint64_t c1 = rdtsc();
        samples_A[s] = cycles_to_us(c1 - c0) / 1000.0;
    }
    stats_t st_A;
    compute_stats(samples_A, N_REPS, &st_A);
    double mbs_A = mbs_from_ms(st_A.med, MODE13H_BYTES);
    plog("Scenario A (dosmemput Mode 13h): min=%.3f med=%.3f mean=%.3f p95=%.3f max=%.3f ms",
         st_A.min, st_A.med, st_A.mean, st_A.p95, st_A.max);
    plog("Scenario A effective MB/s (median): %.2f", mbs_A);
    /* Verify via dosmemget into readback. */
    dosmemget(0xA0000UL, MODE13H_BYTES, readback);
    const char *verify_A = verify_dosmemput(readback);
    plog("Scenario A verify (dosmemget readback): %s", verify_A);
    plog("");

    /* ----- Step 6: Scenario B (nearptr memcpy -> Mode 13h) ----- */
    plog("---- Step 6: Scenario B (nearptr memcpy -> Mode 13h x %d) ----",
         N_REPS);
    double *samples_B = NULL;
    stats_t st_B = {0};
    double mbs_B = 0.0;
    const char *verify_B = "SKIPPED";
    if (nearptr_enabled) {
        samples_B = (double *)malloc(sizeof(double) * N_REPS);
        /* Warmup. */
        memcpy((void *)vram_nearptr, src, MODE13H_BYTES);
        for (int s = 0; s < N_REPS; s++) {
            uint64_t c0 = rdtsc();
            memcpy((void *)vram_nearptr, src, MODE13H_BYTES);
            uint64_t c1 = rdtsc();
            samples_B[s] = cycles_to_us(c1 - c0) / 1000.0;
        }
        compute_stats(samples_B, N_REPS, &st_B);
        mbs_B = mbs_from_ms(st_B.med, MODE13H_BYTES);
        plog("Scenario B (nearptr memcpy): min=%.3f med=%.3f mean=%.3f p95=%.3f max=%.3f ms",
             st_B.min, st_B.med, st_B.mean, st_B.p95, st_B.max);
        plog("Scenario B effective MB/s (median): %.2f", mbs_B);
        verify_B = verify_nearptr_linear_gradient(vram_nearptr);
        plog("Scenario B verify (nearptr readback): %s", verify_B);
    } else {
        plog("Scenario B SKIPPED (nearptr_enable failed).");
    }
    plog("");

    /* ----- Step 7: Scenario C (nearptr memset 0xAA -> Mode 13h) ----- */
    plog("---- Step 7: Scenario C (nearptr memset 0xAA -> Mode 13h x %d) ----",
         N_REPS);
    double *samples_C = NULL;
    stats_t st_C = {0};
    double mbs_C = 0.0;
    const char *verify_C = "SKIPPED";
    if (nearptr_enabled) {
        samples_C = (double *)malloc(sizeof(double) * N_REPS);
        memset((void *)vram_nearptr, 0xAA, MODE13H_BYTES);
        for (int s = 0; s < N_REPS; s++) {
            uint64_t c0 = rdtsc();
            memset((void *)vram_nearptr, 0xAA, MODE13H_BYTES);
            uint64_t c1 = rdtsc();
            samples_C[s] = cycles_to_us(c1 - c0) / 1000.0;
        }
        compute_stats(samples_C, N_REPS, &st_C);
        mbs_C = mbs_from_ms(st_C.med, MODE13H_BYTES);
        plog("Scenario C (nearptr memset 0xAA): min=%.3f med=%.3f mean=%.3f p95=%.3f max=%.3f ms",
             st_C.min, st_C.med, st_C.mean, st_C.p95, st_C.max);
        plog("Scenario C effective MB/s (median): %.2f", mbs_C);
        verify_C = verify_nearptr_memset(vram_nearptr, 0xAA);
        plog("Scenario C verify (nearptr readback): %s", verify_C);
    } else {
        plog("Scenario C SKIPPED (nearptr_enable failed).");
    }
    plog("");

    /* ----- Step 8: cleanup ----- */
    if (nearptr_enabled) __djgpp_nearptr_disable();
    text_mode_restore();
    plog("---- Step 8: cleanup (nearptr_disable + text mode) ----");
    plog("");

    /* ----- Step 9: verdict + ANCHOR emit ----- */
    plog("---- Step 9: Verdict ----");
    int dosmemput_ok = plausible(mbs_A, 5.0, 300.0);
    int memcpy_ok    = (nearptr_enabled && plausible(mbs_B, 5.0, 300.0));
    int memset_ok    = (nearptr_enabled && plausible(mbs_C, 5.0, 300.0));
    int verify_A_pass = (strcmp(verify_A, "PASS") == 0);
    int verify_B_pass = nearptr_enabled && (strcmp(verify_B, "PASS") == 0);

    /* Cross-anchor: BLTFILL Scenario B = 19.6 MB/s for 76800 bytes (banked,
     * with bank-switch). Pro-rated to 64000 bytes baseline would be at the
     * SAME MB/s -- so anchor is 19.6 MB/s expected for dosmemput Mode 13h
     * IF per-byte CPU PIO dominates. If bank-switch was the cost, Mode 13h
     * should be substantially faster (>30% gain). */
    const double BLTFILL_BANKED_MBS = 19.6;
    double mode13h_speedup_vs_banked =
        (mbs_A > 0.0 && BLTFILL_BANKED_MBS > 0.0) ? (mbs_A / BLTFILL_BANKED_MBS) : 0.0;
    int speedup_anchor_ok = plausible(mode13h_speedup_vs_banked, 0.5, 10.0);

    /* Speedup of nearptr memcpy vs dosmemput Mode 13h (both single-bank;
     * isolates DPMI thunk per-call overhead). */
    double nearptr_speedup_vs_dosmemput =
        (nearptr_enabled && mbs_A > 0.0) ? (mbs_B / mbs_A) : 0.0;

    const char *verdict = "UNKNOWN";
    const char *reason  = "";
    if (!verify_A_pass) {
        verdict = "HARNESS_SUSPECT";
        reason  = "Scenario A verify FAIL (dosmemput Mode 13h broken? harness or BIOS bug)";
    } else if (!dosmemput_ok) {
        verdict = "FAIL_PLAUSIBILITY";
        reason  = "dosmemput Mode 13h MB/s out of [5, 300] band";
    } else if (mode13h_speedup_vs_banked > 1.3) {
        verdict = "SHIP_MODE13H";
        reason  = "Mode 13h dosmemput > 1.3x BLTFILL-banked-anchor; bank-switch IS the cost; Mode 13h wave-37 candidate";
    } else if (nearptr_enabled && verify_B_pass && nearptr_speedup_vs_dosmemput > 1.4) {
        verdict = "SHIP_NEARPTR";
        reason  = "nearptr memcpy > 1.4x dosmemput Mode 13h; DPMI thunk is the cost; nearptr wave-37 candidate";
    } else if (mode13h_speedup_vs_banked >= 0.9) {
        verdict = "DROP";
        reason  = "Mode 13h dosmemput approx banked-anchor; bank-switch NOT the cost; per-byte CPU PIO dominates";
    } else {
        verdict = "DROP";
        reason  = "Mode 13h dosmemput SLOWER than banked anchor; mode change penalty exceeds bank-switch savings";
    }
    plog("dosmemput_Mode13h_mbs = %.2f  (cross-anchor BLTFILL banked = ~19.6 MB/s for 76800 B)", mbs_A);
    plog("nearptr_memcpy_mbs    = %.2f  verify=%s", mbs_B, verify_B);
    plog("nearptr_memset_mbs    = %.2f  verify=%s", mbs_C, verify_C);
    plog("Mode13h_speedup_vs_banked     = %.3fx", mode13h_speedup_vs_banked);
    plog("Nearptr_speedup_vs_Mode13h    = %.3fx", nearptr_speedup_vs_dosmemput);
    plog("VERDICT = %s  (%s)", verdict, reason);
    plog("");

    plog("[mode13h-probe BEGIN n=%d mode=0x13]", N_REPS);
    plog("[mode13h-probe DONE");
    plog("    cpu_mhz=%u", (unsigned)g_cpu_mhz);
    plog("    dosmemput_Mode13h_ms_med=%.3f mbs=%.2f verify=%s",
         st_A.med, mbs_A, verify_A);
    if (nearptr_enabled) {
        plog("    nearptr_memcpy_ms_med=%.3f mbs=%.2f verify=%s",
             st_B.med, mbs_B, verify_B);
        plog("    nearptr_memset_ms_med=%.3f mbs=%.2f verify=%s",
             st_C.med, mbs_C, verify_C);
    } else {
        plog("    nearptr scenarios SKIPPED (nearptr_enable failed)");
    }
    plog("    Mode13h_vs_banked_anchor=%.3fx (BLTFILL banked = 19.6 MB/s)",
         mode13h_speedup_vs_banked);
    plog("    nearptr_vs_dosmemput=%.3fx", nearptr_speedup_vs_dosmemput);
    plog("    plausibility: cpu_mhz_in_band=%d dosmemput_in_band=%d memcpy_in_band=%d memset_in_band=%d speedup_anchor_in_band=%d",
         cpu_mhz_ok, dosmemput_ok, memcpy_ok, memset_ok, speedup_anchor_ok);
    plog("    VERDICT=%s reason=\"%s\"]", verdict, reason);
    plog("");
    plog("Cross-anchor: if nearptr_memcpy matches LFBNEAR Scenario B from sibling probe,");
    plog("the ceiling is sysmem->VRAM CPU bandwidth, not bank-switch nor DPMI thunk.");
    plog("");
    plog("=== mode13h-probe done ===");
    plog("[SENTINEL_END]");

    free(src);
    free(readback);
    free(samples_A);
    if (samples_B) free(samples_B);
    if (samples_C) free(samples_C);
    if (g_log) fclose(g_log);
    return 0;
}
