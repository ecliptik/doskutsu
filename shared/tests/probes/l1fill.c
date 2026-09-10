/*
 * l1fill.c — L1/L2/system-RAM bandwidth characterization via memcpy.
 *
 * Phase 9 wave 20 task #8 (P1). Sweeps memcpy / memset bandwidth for a
 * range of working-set sizes that span the Pentium classic cache
 * hierarchy:
 *
 *     1 KB     well inside L1 (8 KB on P54C / Pentium OD)
 *     4 KB     inside L1
 *     16 KB    spills L1, fits in L2
 *     64 KB    inside L2 (256 KB typical for PODP)
 *     256 KB   L2 boundary
 *     1 MB     spills L2, system-RAM bandwidth dominates
 *
 * For each size: time both memcpy(dst, src, n) and memset(dst, val, n).
 * Report per-call min/median/mean/max us + effective bandwidth (MB/s).
 *
 * Why: NXEngine-evo's per-flip cost is split across (a) drawcall blits
 * into the back surface (hot — fits in L2 for the 320x240 INDEX8 frame
 * = 76800 bytes), (b) the dosmemput VRAM flush (4 ms per wave-19), and
 * (c) the unaccounted misc slice. If L2 bandwidth on PODP83 is ~50 MB/s
 * for 64 KB working set, the 76800-byte intra-engine memcpy should cost
 * ~1.5 ms — useful sanity bound on what's possible from sysmem-side
 * blit work.
 *
 * Per perf_predictions_unreliable.md: any prediction (e.g. "Pentium L2 =
 * 100 MB/s based on 83 MHz × bus width") is a hypothesis until measured.
 * The Pentium OverDrive has a 32-bit external bus running at the lower
 * bus speed (50 MHz on a Socket 3 board), throttling L2 bandwidth.
 *
 * Output: C:\L1FILL.LOG (fsync per line).
 *
 * Pure DJGPP. No SDL.
 *
 * 8.3 DOS filename: L1FILL.EXE (6.3) — fits per dos_filename_8_3.md.
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

static void llog(const char *fmt, ...)
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
    g_log = fopen("C:\\L1FILL.LOG", "w");
    if (!g_log) g_log = fopen("L1FILL.LOG", "w");
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

/* ============================================================ */
/* Sweep                                                        */
/* ============================================================ */

/* Sizes in bytes; the trailing 0 terminates. We pick sizes that bracket
 * P54C cache boundaries (8 KB L1, 256 KB L2 typical for PODP83). */
static const uint32_t g_sizes[] = {
    1024,
    4096,
    8192,
    16384,
    32768,
    65536,
    131072,
    262144,
    524288,
    1048576,
    0
};

static int pick_K(uint32_t bytes)
{
    /* Target ~5-15 ms per timed batch given memcpy bandwidth ~50 MB/s
     * for in-cache and ~20 MB/s for sysmem. K = ms / per_call_ms. */
    if (bytes <=    4096) return 5000;
    if (bytes <=   16384) return 1500;
    if (bytes <=   65536) return  300;
    if (bytes <=  262144) return   80;
    if (bytes <= 1048576) return   20;
    return 10;
}

static void sweep(const char *label,
                  void (*op)(uint8_t *dst, const uint8_t *src, uint32_t n),
                  uint8_t *dst, uint8_t *src)
{
    llog("---- %s ----", label);
    const int N = 80;
    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { llog("FATAL: malloc samples failed"); return; }

    for (int i = 0; g_sizes[i]; i++) {
        uint32_t n = g_sizes[i];
        int K = pick_K(n);

        /* Warm-up batch (discarded). */
        op(dst, src, n);

        for (int s = 0; s < N; s++) {
            double t0 = now_secs();
            for (int k = 0; k < K; k++) {
                op(dst, src, n);
            }
            samples[s] = (now_secs() - t0) / (double)K;
        }
        qsort(samples, N, sizeof samples[0], dbl_cmp);
        double mn  = samples[0];
        double mx  = samples[N - 1];
        double med = samples[N / 2];
        double sum = 0.0;
        for (int s = 0; s < N; s++) sum += samples[s];
        double mean = sum / N;

        double mb_per_s = (double)n / (med * 1024.0 * 1024.0);
        llog("L1F-STAT %-7s %7lu B  K=%5d  per-call us:  min=%9.3f  med=%9.3f  mean=%9.3f  max=%9.3f  bw=%6.1f MB/s",
             label, (unsigned long)n, K,
             mn * 1e6, med * 1e6, mean * 1e6, mx * 1e6, mb_per_s);
    }

    free(samples);
}

/* op wrappers so we can pass them through a function pointer with a
 * uniform signature. */
static void op_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    memcpy(dst, src, n);
}

static void op_memset(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    (void)src;
    memset(dst, 0xA5, n);
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    llog("=== L1FILL wave-20 task #8 (P1) starting ===");
    llog("DJGPP build, target = memcpy/memset bandwidth across cache hierarchy");
    llog("UCLOCKS_PER_SEC = %lu (resolution ~%.0f ns)",
         (unsigned long)UCLOCKS_PER_SEC, 1e9 / (double)UCLOCKS_PER_SEC);

    /* Allocate two 1 MB buffers in protected-mode RAM. */
    const uint32_t MAX = 1048576;
    uint8_t *src = (uint8_t *)malloc(MAX);
    uint8_t *dst = (uint8_t *)malloc(MAX);
    if (!src || !dst) {
        llog("FATAL: malloc(2 x 1MB) failed (XMS exhausted?)");
        free(src); free(dst);
        if (g_log) fclose(g_log);
        return 2;
    }
    /* Touch every page so demand-paging doesn't bias the first batch. */
    for (uint32_t i = 0; i < MAX; i += 4096) {
        src[i] = (uint8_t)(i & 0xFF);
        dst[i] = 0;
    }
    llog("BUF: src=%p  dst=%p  (each 1 MB, page-touched)", src, dst);

    sweep("memcpy", op_memcpy, dst, src);
    sweep("memset", op_memset, dst, src);

    free(src);
    free(dst);

    llog("=== L1FILL done ===");
    llog("");
    llog("Reading the curve:");
    llog("  Bandwidth flat across small sizes  -> L1 hit, peak per-cycle throughput");
    llog("  Drop at ~8 KB                       -> L1 miss boundary (P54C: 8 KB)");
    llog("  Second drop at ~256 KB              -> L2 miss boundary (PODP: 256 KB)");
    llog("  Plateau >>256 KB                    -> system-RAM bandwidth (DRAM page misses)");
    llog("");
    llog("76800-byte (320x240 INDEX8 frame) sits in L2; the engine's");
    llog("intra-frame blits should run at L2 bandwidth, not sysmem.");

    if (g_log) fclose(g_log);
    return 0;
}
