/*
 * dacprog.c — VGA DAC programming-cost ablation harness.
 *
 * Phase 9 wave 20 task #7. Load-bearing for Lever C (palette DAC gating)
 * go/no-go decision after the W20A flip-instrumentation iter. Per memory
 * perf_predictions_unreliable.md, any cycle-count prediction (e.g.
 * "768 outportb @ ~1µs each ≈ 0.77 ms" or "~1.5 ms with index latching")
 * is a hypothesis. This probe SETTLES IT on the g2k Pentium-OD-83 / Cirrus
 * 5434 / CWSDPMI configuration.
 *
 * Three test modes, env-dispatched:
 *
 *   DOS_PORT_DACPROG_MODE=FULL     256-entry full reprogram
 *                                  (1 outportb to 0x3C8 + 768 to 0x3C9)
 *                                  Mirrors SDL3-DOS ProgramVGADAC's worst
 *                                  case — full palette refresh.
 *
 *   DOS_PORT_DACPROG_MODE=PART64   64-entry partial reprogram
 *                                  (1 outportb to 0x3C8 + 192 to 0x3C9)
 *                                  Mid case: a localized animated range
 *                                  (e.g. waterfall colors at indices 32-95).
 *
 *   DOS_PORT_DACPROG_MODE=SINGLE   1-entry update
 *                                  (1 outportb to 0x3C8 + 3 to 0x3C9)
 *                                  Best case: a single cycling color.
 *
 *   No env var (default): runs FULL, then PART64, then SINGLE sequentially.
 *
 * Per-mode timing harness:
 *   - 200 timed batches; each batch is K back-to-back DAC programs to keep
 *     the timed window large compared to uclock() overhead. K is mode-
 *     dependent (FULL=10, PART64=50, SINGLE=1000) so each batch is ~5-15 ms.
 *   - Per-call cost = batch_time / K. Report min/median/mean/max ms per
 *     call across the 200 batches, plus aggregate ms over 200×K calls.
 *
 * Output: C:\DACPROG.LOG (fsync-per-line, mirroring the wave-19 probe and
 * patches/nxengine-evo/0036-djgpp-fsync-debug-log-per-line.patch). Falls
 * back to ./DACPROG.LOG if C:\ is not writable. Same lines also go to
 * stdout for harness capture.
 *
 * Pure DJGPP. No SDL, no engine. Runs under text mode (DAC registers are
 * in the VGA core and accessible regardless of mode). Saves and restores
 * the DAC contents around the test so the post-exit text shell still
 * looks correct.
 *
 * 8.3 DOS filename: DACPROG.EXE (7.3) — fits per memory/dos_filename_8_3.md.
 *
 * Build: `make dacprog` (or `make probes` for the whole P0+P1 set).
 * Smoke under DOSBox-X for correctness only — DOSBox-X's emulated DAC has
 * no I/O-bus contention, so the numbers are real-HW-only per
 * memory/dosbox_not_perf_proxy.md.
 *
 * License: MIT.
 */

#include <dos.h>
#include <go32.h>
#include <pc.h>          /* outportb / inportb */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>        /* uclock() / UCLOCKS_PER_SEC */
#include <unistd.h>

/* ============================================================ */
/* VGA DAC port constants                                       */
/* ============================================================ */

#define DAC_PEL_MASK        0x3C6
#define DAC_INDEX_R         0x3C7  /* write start-index for sequential DAC reads */
#define DAC_INDEX_W         0x3C8  /* write start-index for sequential DAC writes */
#define DAC_DATA            0x3C9  /* read/write 6-bit RGB triples here */

#define DAC_TOTAL_ENTRIES   256
#define DAC_BYTES_PER_ENTRY 3      /* R, G, B */

/* ============================================================ */
/* Logging — fsync per line so DOS / CF cache survives crash    */
/* ============================================================ */

static FILE *g_log = NULL;

static void plog(const char *fmt, ...)
{
    char buf[512];
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

static void open_log(void)
{
    g_log = fopen("C:\\DACPROG.LOG", "w");
    if (!g_log) {
        g_log = fopen("DACPROG.LOG", "w");
    }
}

/* ============================================================ */
/* Timing — uclock gives ~838ns resolution from the PIT         */
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

static void summarize(const char *label,
                      double *samples,
                      int n_samples,
                      int calls_per_sample)
{
    /* Convert each batch sample → per-call seconds. Reuses the samples
     * array (caller's free) — not const. */
    for (int i = 0; i < n_samples; i++) {
        samples[i] = samples[i] / (double)calls_per_sample;
    }
    qsort(samples, n_samples, sizeof samples[0], dbl_cmp);

    double sum = 0.0;
    for (int i = 0; i < n_samples; i++) sum += samples[i];
    double mean = sum / n_samples;
    double mn   = samples[0];
    double mx   = samples[n_samples - 1];
    double med  = samples[n_samples / 2];

    plog("DAC-STAT %-10s n=%d K=%d  per-call:  min=%9.3f  med=%9.3f  mean=%9.3f  max=%9.3f  us",
         label, n_samples, calls_per_sample,
         mn * 1e6, med * 1e6, mean * 1e6, mx * 1e6);

    /* Aggregate timing: total ms across all N×K calls. */
    int total_calls = n_samples * calls_per_sample;
    plog("DAC-AGG  %-10s total=%9.3f ms over %d calls  (mean ms/call=%.4f)",
         label, sum * 1000.0 * (double)calls_per_sample,
         total_calls, mean * 1000.0);
}

/* ============================================================ */
/* DAC save / restore                                           */
/* ============================================================ */

static void dac_save(uint8_t saved[DAC_TOTAL_ENTRIES * DAC_BYTES_PER_ENTRY])
{
    outportb(DAC_INDEX_R, 0);
    for (int i = 0; i < DAC_TOTAL_ENTRIES * DAC_BYTES_PER_ENTRY; i++) {
        saved[i] = inportb(DAC_DATA);
    }
}

static void dac_restore(const uint8_t saved[DAC_TOTAL_ENTRIES * DAC_BYTES_PER_ENTRY])
{
    outportb(DAC_INDEX_W, 0);
    for (int i = 0; i < DAC_TOTAL_ENTRIES * DAC_BYTES_PER_ENTRY; i++) {
        outportb(DAC_DATA, saved[i]);
    }
}

/* ============================================================ */
/* DAC program primitives — the actual things being measured    */
/* ============================================================ */

/* Program `count` consecutive DAC entries starting at `start_idx`. The
 * RGB values are sourced from `rgb` (3 bytes per entry).
 *
 * This is the canonical SDL3-DOS ProgramVGADAC inner loop: one outportb
 * to set the index, then count×3 outportb's to write R/G/B for each entry.
 * No reads back, no PEL_MASK toggle — straight-line writes. */
static inline void dac_program(uint8_t start_idx,
                               int count,
                               const uint8_t *rgb)
{
    outportb(DAC_INDEX_W, start_idx);
    int n_bytes = count * DAC_BYTES_PER_ENTRY;
    for (int i = 0; i < n_bytes; i++) {
        outportb(DAC_DATA, rgb[i]);
    }
}

/* ============================================================ */
/* Timed runs — three modes, each batched for measurement       */
/* ============================================================ */

/* Generate a deterministic 6-bit RGB ramp into rgb[count*3]. The actual
 * values don't matter for outportb timing (no chip dependency on data),
 * but they're real values rather than zero so any chip-side oddity that
 * cares about specific values would surface. */
static void make_rgb_ramp(uint8_t *rgb, int count)
{
    for (int i = 0; i < count; i++) {
        rgb[i * 3 + 0] = (uint8_t)((i * 0x07) & 0x3F);
        rgb[i * 3 + 1] = (uint8_t)((i * 0x0B) & 0x3F);
        rgb[i * 3 + 2] = (uint8_t)((i * 0x11) & 0x3F);
    }
}

/* One call to dac_program with `entries` count, repeated K times in a
 * tight loop. Returns elapsed seconds for the whole batch. */
static double timed_batch(uint8_t start_idx,
                          int entries,
                          const uint8_t *rgb,
                          int K)
{
    double t0 = now_secs();
    for (int k = 0; k < K; k++) {
        dac_program(start_idx, entries, rgb);
    }
    return now_secs() - t0;
}

static void run_mode(const char *label, int entries, int K, int N)
{
    plog("---- mode=%s entries=%d K=%d N=%d ----", label, entries, K, N);

    uint8_t *rgb = (uint8_t *)malloc(entries * DAC_BYTES_PER_ENTRY);
    if (!rgb) {
        plog("FATAL: malloc(%d) failed for rgb buffer", entries * 3);
        return;
    }
    make_rgb_ramp(rgb, entries);

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) {
        plog("FATAL: malloc samples failed");
        free(rgb);
        return;
    }

    /* Warm-up: two batches discarded — flushes any first-call cost into
     * the void (cache fills, DPMI scheduling jitter on the first I/O). */
    timed_batch(0, entries, rgb, K);
    timed_batch(0, entries, rgb, K);

    /* Sample. start_idx walks 0,1,2,... mod (256-entries+1) so we exercise
     * different entry ranges; the chip should not care, but if it does
     * we'll see it as variance in the samples. */
    for (int i = 0; i < N; i++) {
        uint8_t idx = (uint8_t)(i % (256 - entries + 1));
        samples[i] = timed_batch(idx, entries, rgb, K);
    }

    summarize(label, samples, N, K);

    free(samples);
    free(rgb);
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== DACPROG wave-20 task #7 (P0) starting ===");
    plog("DJGPP build, target = VGA DAC at ports 0x3C8/0x3C9");
    plog("UCLOCKS_PER_SEC = %lu (resolution ~%.0f ns)",
         (unsigned long)UCLOCKS_PER_SEC,
         1e9 / (double)UCLOCKS_PER_SEC);

    /* Save the existing DAC so post-exit text shell isn't garbled. */
    static uint8_t saved_dac[DAC_TOTAL_ENTRIES * DAC_BYTES_PER_ENTRY];
    dac_save(saved_dac);
    plog("DAC: saved 768 bytes of existing palette");

    const char *mode = getenv("DOS_PORT_DACPROG_MODE");
    if (!mode || mode[0] == 0) {
        plog("MODE: (default) running FULL -> PART64 -> SINGLE");
        run_mode("FULL",   256, 10,   200);
        run_mode("PART64",  64, 50,   200);
        run_mode("SINGLE",   1, 1000, 200);
    } else if (strcmp(mode, "FULL") == 0) {
        run_mode("FULL", 256, 10, 200);
    } else if (strcmp(mode, "PART64") == 0) {
        run_mode("PART64", 64, 50, 200);
    } else if (strcmp(mode, "SINGLE") == 0) {
        run_mode("SINGLE", 1, 1000, 200);
    } else {
        plog("FATAL: unknown DOS_PORT_DACPROG_MODE='%s' (expected FULL|PART64|SINGLE)", mode);
        dac_restore(saved_dac);
        if (g_log) fclose(g_log);
        return 2;
    }

    /* Restore the DAC so the text shell looks correct after exit. */
    dac_restore(saved_dac);
    plog("DAC: restored saved palette");

    plog("=== DACPROG done ===");
    plog("");
    plog("Lever-C decision input:");
    plog("  If FULL median >> 1 ms and SDL3-DOS ProgramVGADAC fires every flip,");
    plog("  Lever C (palette DAC gating) is HIGH-VALUE — fix gating to stop");
    plog("  programming when palette content is unchanged.");
    plog("  If FULL median < 0.3 ms, Lever C is LOW-VALUE — drop or defer.");
    plog("  PART64 + SINGLE numbers inform whether partial-update strategies");
    plog("  are worth the complexity if FULL is large but SINGLE is cheap.");

    if (g_log) fclose(g_log);
    return 0;
}
