/*
 * partial.c — Partial-flush crossover characterization.
 *
 * Phase 9 wave 20 task #8 (P1). Quantifies the per-call setup cost that
 * killed wave 17.4's partial-flush optimization (per
 * memory/wave_17_4_partial_flush_regression.md: "per-rect dosmemput
 * overhead dwarfs bytes-saved").
 *
 * The engine flushes a 320x240 INDEX8 frame as one contiguous 76800-byte
 * dosmemput. Wave 17.4 split this into K rect-sized dosmemput calls hoping
 * to skip unchanged rows; on real HW this REGRESSED -7.1 fps because each
 * rect added ~3-5 ms of DPMI / DJGPP per-call setup that the byte-savings
 * couldn't outweigh.
 *
 * This probe walks the K dimension explicitly:
 *
 *   K=1    one 76800-byte call (the wave-17.4 baseline = current default)
 *   K=2    two 38400-byte calls
 *   K=4    four 19200-byte calls
 *   K=8    eight 9600-byte calls
 *   K=16   sixteen 4800-byte calls
 *   K=32   thirty-two 2400-byte calls
 *   K=64   sixty-four 1200-byte calls (one per scanline-pair)
 *   K=240  two-hundred-and-forty 320-byte calls (one per scanline)
 *
 * For each K: time the full 76800-byte transfer split into K calls,
 * dst-addressed at K equally-spaced offsets in a 320 KB DOS conv-mem
 * buffer (so the dst lands at different bank-window-equivalent
 * positions, mimicking the engine's per-rect dst geometry).
 *
 * Crossover point K* is the value beyond which the per-call overhead
 * dominates byte-savings. Real HW K* informs whether ANY future
 * dirty-region work is viable: if K*=2 (i.e. even 2-rect splits
 * regress), the partial-flush direction is dead. If K*=8 or higher,
 * a future "merge dirty regions if count >= K*" heuristic could win.
 *
 * Output: C:\PARTIAL.LOG (fsync per line).
 *
 * Pure DJGPP (libc + dpmi). No SDL. Writes into DOS conventional memory,
 * not VRAM, because the wave-19 cirrus probe already measured the
 * VRAM-side bandwidth (4 ms per 76800 bytes); this probe isolates the
 * PER-CALL overhead from VRAM bandwidth so the crossover number can be
 * applied to either path. Real HW dosmemput-to-VRAM and dosmemput-to-RAM
 * exhibit the same per-call overhead at the DPMI layer.
 *
 * 8.3 DOS filename: PARTIAL.EXE (7.3) — fits.
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
#include <sys/movedata.h>
#include <time.h>
#include <unistd.h>

#define FRAME_SIZE       76800u   /* 320 * 240 INDEX8 bytes */
#define DEST_BUF_SIZE   327680u   /* 320 KB — covers 4 frame copies */

/* ============================================================ */
/* Logging                                                      */
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
    g_log = fopen("C:\\PARTIAL.LOG", "w");
    if (!g_log) g_log = fopen("PARTIAL.LOG", "w");
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
/* Test core                                                    */
/* ============================================================ */

/* Issue K calls of (FRAME_SIZE / K) bytes each, walking the dst by the
 * same stride. Total bytes transferred = FRAME_SIZE per outer call. */
static double timed_split_flush(const uint8_t *src,
                                uint32_t dos_lin_base,
                                int K,
                                int reps)
{
    int chunk = FRAME_SIZE / K;
    /* Stride between chunks in the dest buffer. The engine uses
     * row*pitch offsets; we use chunk-size strides which is equivalent
     * for measuring per-call overhead. */
    int stride = chunk;

    double t0 = now_secs();
    for (int r = 0; r < reps; r++) {
        for (int k = 0; k < K; k++) {
            dosmemput(src + (k * chunk),
                      chunk,
                      dos_lin_base + (uint32_t)(k * stride));
        }
    }
    return now_secs() - t0;
}

static const int g_K_values[] = { 1, 2, 4, 8, 16, 32, 64, 240, 0 };

static int pick_reps(int K)
{
    /* Larger K -> more per-call overhead -> probably slower per-frame.
     * Want each batch ~10 ms. Total bytes per outer call = 76800 always.
     * K=1 baseline ~ 4 ms (per wave 19), so reps=3 is enough; K=240
     * could be ~50 ms per outer call, reps=1. Be conservative. */
    if (K <= 8)   return 5;
    if (K <= 64)  return 3;
    return 1;
}

static void run_partial(uint32_t dos_lin)
{
    plog("---- PARTIAL: 76800 bytes split into K calls, dosmemput'd to DOS conv mem ----");
    plog("    src = 76800-byte protected-mode buffer (deterministic content)");
    plog("    dst = %u-byte DOS conventional memory at lin=0x%05lX",
         (unsigned)DEST_BUF_SIZE, (unsigned long)dos_lin);

    uint8_t *src = (uint8_t *)malloc(FRAME_SIZE);
    if (!src) { plog("FATAL: malloc(FRAME_SIZE) failed"); return; }
    for (uint32_t i = 0; i < FRAME_SIZE; i++) src[i] = (uint8_t)(i & 0xFF);

    const int N = 60;
    double *samples = (double *)malloc(sizeof(double) * N);
    if (!samples) { plog("FATAL: malloc samples failed"); free(src); return; }

    /* Print column header for at-a-glance reading. */
    plog("");
    plog("  K   chunk_B    reps  per-flush ms (min / med / mean / max)   per-call us (med)   overhead-vs-K=1 ms");

    double k1_med_ms = 0.0;

    for (int i = 0; g_K_values[i]; i++) {
        int K = g_K_values[i];
        int chunk = FRAME_SIZE / K;
        int reps = pick_reps(K);

        timed_split_flush(src, dos_lin, K, reps);  /* warm-up */

        for (int s = 0; s < N; s++) {
            samples[s] = timed_split_flush(src, dos_lin, K, reps) / (double)reps;
        }
        qsort(samples, N, sizeof samples[0], dbl_cmp);
        double mn  = samples[0];
        double mx  = samples[N - 1];
        double med = samples[N / 2];
        double sum = 0.0;
        for (int s = 0; s < N; s++) sum += samples[s];
        double mean = sum / N;

        double per_call_us = (med / (double)K) * 1e6;
        double med_ms = med * 1000.0;

        if (K == 1) k1_med_ms = med_ms;
        double overhead_vs_k1 = (k1_med_ms > 0.0) ? (med_ms - k1_med_ms) : 0.0;

        plog("  %3d  %7d  %5d   %7.3f / %7.3f / %7.3f / %7.3f      %7.2f             %+8.3f",
             K, chunk, reps,
             mn * 1000.0, med_ms, mean * 1000.0, mx * 1000.0,
             per_call_us, overhead_vs_k1);
    }

    free(samples);
    free(src);
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== PARTIAL wave-20 task #8 (P1) starting ===");
    plog("DJGPP build, target = partial-flush per-call overhead crossover");

    /* Allocate 320 KB conv-mem destination buffer (covers max stride). */
    int paragraphs = (DEST_BUF_SIZE + 15) / 16;
    int sel = -1;
    int seg = __dpmi_allocate_dos_memory(paragraphs, &sel);
    if (seg < 0) {
        plog("FATAL: __dpmi_allocate_dos_memory(%u) failed", (unsigned)DEST_BUF_SIZE);
        if (g_log) fclose(g_log);
        return 2;
    }
    uint32_t dos_lin = (uint32_t)seg << 4;
    plog("DOS-buf: %u bytes at lin=0x%05lX  selector=0x%04X",
         (unsigned)DEST_BUF_SIZE, (unsigned long)dos_lin, sel);

    run_partial(dos_lin);

    __dpmi_free_dos_memory(sel);

    plog("=== PARTIAL done ===");
    plog("");
    plog("Reading the table:");
    plog("  K=1 row is the wave-19 baseline (~4 ms on g2k for 76800 bytes).");
    plog("  Each row's 'overhead-vs-K=1' is the cost we'd pay to skip");
    plog("  bytes via partial flush — partial flush is only viable if the");
    plog("  dirty region is small enough to skip MORE bytes than this overhead.");
    plog("");
    plog("Wave-17.4 hindsight: partial flush regressed -7.1 fps with K~=2-4.");
    plog("If this probe shows K=2 already adds >0.5 ms, that explains the");
    plog("regression cleanly. K* (crossover) is the K beyond which overhead");
    plog("clearly dominates — informs any future dirty-rect viability.");

    if (g_log) fclose(g_log);
    return 0;
}
