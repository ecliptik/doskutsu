/*
 * cffsync.c — CF-card fsync wall-clock cost characterization.
 *
 * Phase 9 wave 20 task #12 (P3, medium pri). Tests the hypothesis from
 * W20A v2 findings § 6: per-line fsync (patch nxengine/0036, used for
 * DEBUG.LOG mid-run readability) costs 5-15 ms each on a CF card. With
 * ~17 fsync's per 100 flips at ~10 ms each = ~1.7 ms distributed over
 * 100 flips, contributing tail latency rather than median. This probe
 * quantifies the per-fsync cost so we can validate (or eliminate) the
 * hypothesis.
 *
 * Three payload sizes:
 *   16 B    one short LOG line (e.g. "FLIP n=42 t=8.2ms")
 *   256 B   typical LOG-block flush (a full instrumentation line)
 *   4096 B  one CF sector boundary
 *
 * Plus a no-fsync baseline (fwrite only, OS buffer caching) for
 * comparison — confirms the wall-clock cost is fsync, not write.
 *
 * For each payload size: N=30 iterations of {fwrite + fsync},
 * sample-by-sample timing (no batching — each fsync should be tens of
 * ms on real HW, plenty above uclock granularity). Report per-call
 * min/median/mean/max ms.
 *
 * Output: C:\CFFSYNC.LOG (fsync per line — itself uses the same call
 * the probe measures, intentional; the probe's own log lines exhibit
 * the cost). Test data file: C:\CFFSYNC.DAT (created + truncated each
 * run; freed at end via remove()).
 *
 * Pure DJGPP. No SDL.
 *
 * 8.3 DOS filename: CFFSYNC.EXE (7.3) — fits per dos_filename_8_3.md.
 *
 * Build: `make cffsync`. DOSBox-X smoke is host-FS-cached so the
 * numbers are NOT real-HW: real CF on g2k can be 100-1000x slower per
 * fsync. Per dosbox_not_perf_proxy.md.
 *
 * License: MIT.
 */

#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                      */
/* ============================================================ */

static FILE *g_log = NULL;

static void cflog(const char *fmt, ...)
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
    g_log = fopen("C:\\CFFSYNC.LOG", "w");
    if (!g_log) g_log = fopen("CFFSYNC.LOG", "w");
}

/* ============================================================ */
/* Timing                                                       */
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

static void summarize(const char *label, double *samples, int n)
{
    qsort(samples, n, sizeof samples[0], dbl_cmp);
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += samples[i];
    double mean = sum / n;
    double mn = samples[0];
    double mx = samples[n - 1];
    double med = samples[n / 2];

    cflog("CF-STAT  %-26s n=%d   per-call ms:  min=%9.3f  med=%9.3f  mean=%9.3f  max=%9.3f",
         label, n, mn * 1000.0, med * 1000.0, mean * 1000.0, mx * 1000.0);
}

/* ============================================================ */
/* Test core                                                    */
/* ============================================================ */

static const char *DATA_PATH = "C:\\CFFSYNC.DAT";

/* Time N iterations of {fwrite + fflush + fsync} for `bytes` payload.
 * Returns array of N per-call seconds (caller frees). */
static double *time_writes_with_fsync(int bytes, int N, const char *label)
{
    FILE *f = fopen(DATA_PATH, "wb");
    if (!f) {
        f = fopen("CFFSYNC.DAT", "wb");
        if (!f) {
            cflog("FATAL: cannot open data file");
            return NULL;
        }
    }

    uint8_t *payload = (uint8_t *)malloc(bytes);
    if (!payload) {
        fclose(f);
        cflog("FATAL: malloc(%d) failed", bytes);
        return NULL;
    }
    for (int i = 0; i < bytes; i++) payload[i] = (uint8_t)(i & 0xFF);

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) {
        free(payload);
        fclose(f);
        cflog("FATAL: malloc samples failed");
        return NULL;
    }

    /* Warm-up — first write opens the underlying CF write path; toss it. */
    fwrite(payload, 1, bytes, f);
    fflush(f);
    fsync(fileno(f));

    for (int i = 0; i < N; i++) {
        double t0 = now_secs();
        fwrite(payload, 1, bytes, f);
        fflush(f);
        fsync(fileno(f));
        samples[i] = now_secs() - t0;
    }

    fclose(f);
    free(payload);

    summarize(label, samples, N);
    return samples;
}

static void time_writes_no_fsync(int bytes, int N, const char *label)
{
    FILE *f = fopen(DATA_PATH, "wb");
    if (!f) f = fopen("CFFSYNC.DAT", "wb");
    if (!f) { cflog("FATAL: cannot open data file"); return; }

    uint8_t *payload = (uint8_t *)malloc(bytes);
    if (!payload) { fclose(f); return; }
    for (int i = 0; i < bytes; i++) payload[i] = (uint8_t)(i & 0xFF);

    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { free(payload); fclose(f); return; }

    fwrite(payload, 1, bytes, f);  /* warm-up */

    for (int i = 0; i < N; i++) {
        double t0 = now_secs();
        fwrite(payload, 1, bytes, f);
        samples[i] = now_secs() - t0;
    }
    fclose(f);
    free(payload);

    summarize(label, samples, N);
    free(samples);
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    cflog("=== CFFSYNC wave-20 task #12 (P3) starting ===");
    cflog("DJGPP build, target = CF-card fsync wall-clock cost");
    cflog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    cflog("Data file: %s (created + truncated each test, removed at end)", DATA_PATH);

    /* No-fsync baseline (write-only — OS / CF write-back cache absorbs it). */
    cflog("---- BASELINE: fwrite only (no fsync) — write-cache absorbs the call ----");
    time_writes_no_fsync(   16, 50, "fwrite-only      16B");
    time_writes_no_fsync(  256, 50, "fwrite-only     256B");
    time_writes_no_fsync( 4096, 50, "fwrite-only    4096B");

    /* fsync-bracketed write: each call MUST flush to CF before returning. */
    cflog("---- FSYNC: fwrite + fflush + fsync(fd) per call ----");
    double *s16   = time_writes_with_fsync(  16, 30, "fwrite+fsync     16B");
    double *s256  = time_writes_with_fsync( 256, 30, "fwrite+fsync    256B");
    double *s4096 = time_writes_with_fsync(4096, 30, "fwrite+fsync   4096B");

    /* Per nxengine/0036 hypothesis: ~17 fsync's per 100 flips at the
     * median fsync cost = distributed cost per flip. */
    if (s16) {
        qsort(s16, 30, sizeof(double), dbl_cmp);
        double med_ms = s16[30 / 2] * 1000.0;
        double per_flip_distributed = (17.0 / 100.0) * med_ms;
        cflog("");
        cflog("CF-MODEL fsync 16B median = %.3f ms;  scaled to 17 fsync/100 flips = %.3f ms/flip distributed",
             med_ms, per_flip_distributed);
    }

    free(s16); free(s256); free(s4096);

    /* Cleanup. */
    remove(DATA_PATH);
    remove("CFFSYNC.DAT");

    cflog("=== CFFSYNC done ===");
    cflog("");
    cflog("Reading the results:");
    cflog("  fwrite-only times << fsync times             -> write cache is absorbing");
    cflog("  fsync 16B  med >= 5 ms                       -> CF write-cycle dominates");
    cflog("  fsync scaling flat (16B == 4096B)            -> cost is per-call, not per-byte");
    cflog("  fsync scaling proportional to size           -> cost is per-byte (DMA bandwidth)");
    cflog("");
    cflog("If fsync 16B is 10+ ms on g2k, the engine's per-LINE fsync from");
    cflog("nxengine/0036 contributes tail latency to the flip() distribution.");
    cflog("Median impact remains modest (~0.17 * fsync_ms per flip), but");
    cflog("p99 latency would benefit from batching log writes.");

    if (g_log) fclose(g_log);
    return 0;
}
