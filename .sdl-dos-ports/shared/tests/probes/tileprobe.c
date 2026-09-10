/*
 * tileprobe.c — visit-loop overhead measurement (Phase 11 wave-22.5 / iter H).
 *
 * Question: in the engine's per-flip tilemap pass, the visit loop iterates
 * ~30000 tile cells per frame to draw ~1900 visible ones. Each skipped
 * iteration costs at least: int16_t load + branch + (possibly) bounds
 * check. Off-screen tile skip (nx-engine slot 0113) replaces the full
 * iteration with a precomputed bbox index list of just the visible tiles.
 *
 * What's the cost of the wasted ~28100 iterations? If < 1 ms / flip, the
 * optimization isn't worth the integration risk. If 2-4 ms, ship it. If
 * higher, ship it as a high-priority Lever.
 *
 * Per perf_predictions_unreliable.md: theoretical numbers from cycle counts
 * have been 5-30x wrong on PODP83+DJGPP+DPMI. The probe MEASURES; the
 * analysis interprets the measurement. No prose-side "30000 * 10 cycles
 * = 3.4 ms" math embedded in the output.
 *
 * Per dosbox_not_perf_proxy.md: DOSBox-X numbers from this probe will NOT
 * match real HW (host RDTSC under cycle=fixed scaling is meaningless).
 * Real HW iter is the data gate; smoke is correctness-only.
 *
 * Probe scope (per team-lead brief):
 *
 *   1. Allocate two int16_t[30000] arrays:
 *      - realistic: Cave Story-ish distribution (~50% non-zero)
 *      - zeros:     all 0 (pathological "skip everything" baseline)
 *   2. Loop A: iterate all 30000 with per-tile work
 *   3. Loop B: iterate ~1900 visible tiles via precomputed bbox index list
 *      (44x43 = 1892 contiguous tiles centered in a 200x150 tilemap)
 *   4. Per-tile work: int16_t load + branch + 256-byte memcpy if drawn
 *      ("256 bytes" = 16x16 INDEX8 tile blit equivalent)
 *   5. RDTSC timing on P54C+ (PODP83 supports it)
 *   6. N=100 reps per scenario, emit min/med/mean/p95/max
 *
 * Decision matrix (4 scenarios = 2 loops x 2 arrays):
 *
 *               | realistic   | zeros (all-skip)
 *   ------------+-------------+------------------
 *   full (30K)  | A_real      | A_zero (pure iter overhead)
 *   bbox (1.9K) | B_real      | B_zero (bbox iter overhead)
 *
 * Headline savings = (A_real - B_real) ms / flip if optimization shipped.
 *
 * Output: TILEPROB.LOG in cwd (operator runs from \DOSKUTSU\). Stdout
 * mirrored. Pure DJGPP, no SDL.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/tileprobe.c (host-side, no 8.3)
 *   Binary:   TILEPROB.EXE  (8+3, fits) — explicit Makefile rule maps
 *                            tileprobe.c (9 char) -> tileprob.exe (8 char)
 *   Log:      TILEPROB.LOG  (8+3, consistent w/ binary)
 *   BAT:      TILEPROB.BAT
 *
 * License: MIT.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging — fopen-direct + stdout mirror. Less aggressive      */
/* fsync than MPUPROBE because this probe has no hang risk;     */
/* per-line fsync would dominate runtime.                        */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("TILEPROB.LOG", "w");
    if (!g_log) g_log = fopen("C:\\TILEPROB.LOG", "w");
}

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
    }
}

/* ============================================================ */
/* RDTSC — Pentium+ time-stamp counter                           */
/*                                                               */
/* P54C supports RDTSC since Pentium (CPUID feature bit 4). The  */
/* PODP83 g2k target is a P54C OverDrive, so RDTSC works.        */
/* DOSBox-X also synthesizes RDTSC — under cycle=fixed it returns */
/* scaled values that don't match real HW (per dosbox_not_perf_  */
/* proxy.md). Numbers from DOSBox-X smoke are correctness-only.  */
/* ============================================================ */

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* ============================================================ */
/* Tilemap geometry                                              */
/* ============================================================ */

#define MAP_W   200
#define MAP_H   150
#define MAP_N   (MAP_W * MAP_H)   /* 30000 tiles */

/* Bbox covers a 44x43 contiguous region centered in the 200x150 tilemap.
 * 44 * 43 = 1892, ~= the brief's "~1900 visible tiles" target. Centered
 * at (78, 53) so it doesn't touch the tilemap edges. */
#define BBOX_W  44
#define BBOX_H  43
#define BBOX_X0 78
#define BBOX_Y0 53
#define BBOX_N  (BBOX_W * BBOX_H)  /* 1892 visible tiles */

/* Tile blit dest size: 16x16 = 256 bytes INDEX8 tile equivalent. */
#define TILE_BYTES 256

/* ============================================================ */
/* Volatile sink — keeps the optimizer from eliding the loop    */
/* body when a clever -O2 pass realizes nothing observable       */
/* depends on `drawn`.                                           */
/* ============================================================ */

static volatile uint32_t g_sink = 0;

/* ============================================================ */
/* The two loop variants — these are the load-bearing harness   */
/*                                                               */
/* IMPORTANT: keep them in two separate functions so the         */
/* compiler can't fuse / unroll one into the other and skew      */
/* the comparison. Marking them noinline would be belt-and-      */
/* suspenders but `-O2` shouldn't fuse across function calls.   */
/* ============================================================ */

static __attribute__((noinline))
uint32_t loop_full(const int16_t *tilemap, uint8_t *dst, const uint8_t *src)
{
    uint32_t drawn = 0;
    for (int i = 0; i < MAP_N; i++) {
        int16_t t = tilemap[i];
        if (t != 0) {
            /* 256-byte memcpy simulates a 16x16 INDEX8 tile blit. The dst
             * is a single 256-byte buffer hot in L1; not super realistic
             * (real engine writes to varied back-surface positions) but
             * the brief asked for "minimal blit-equivalent stub" and we
             * want to measure ITERATION overhead, not blit overhead. */
            memcpy(dst, src, TILE_BYTES);
            drawn++;
        }
    }
    return drawn;
}

static __attribute__((noinline))
uint32_t loop_bbox(const int16_t *tilemap, const uint16_t *visible_idx,
                   uint8_t *dst, const uint8_t *src)
{
    uint32_t drawn = 0;
    for (int i = 0; i < BBOX_N; i++) {
        int16_t t = tilemap[visible_idx[i]];
        if (t != 0) {
            memcpy(dst, src, TILE_BYTES);
            drawn++;
        }
    }
    return drawn;
}

/* ============================================================ */
/* Statistics                                                    */
/* ============================================================ */

static int u64_cmp(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a, vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

typedef struct {
    uint64_t min, p95, max;
    double median, mean;
    uint32_t expected_drawn;  /* sanity: matches across reps? */
} stats_t;

static void compute_stats(uint64_t *samples, int n, stats_t *out,
                          uint32_t drawn)
{
    qsort(samples, n, sizeof samples[0], u64_cmp);
    out->min = samples[0];
    out->max = samples[n - 1];
    out->median = (double)samples[n / 2];
    out->p95 = samples[(int)(0.95 * n)];

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += (double)samples[i];
    out->mean = sum / n;

    out->expected_drawn = drawn;
}

/* ============================================================ */
/* Run one scenario: N reps of one loop variant + one tilemap   */
/* ============================================================ */

typedef enum { LOOP_FULL, LOOP_BBOX } loop_kind_t;

#define N_REPS 100

static void run_scenario(const char *label, loop_kind_t kind,
                         const int16_t *tilemap, const uint16_t *visible_idx,
                         uint8_t *dst, const uint8_t *src,
                         double mhz_estimate)
{
    static uint64_t samples[N_REPS];
    uint32_t drawn = 0;

    /* Warm-up — primes branch predictor + pulls tilemap into cache. */
    if (kind == LOOP_FULL) drawn = loop_full(tilemap, dst, src);
    else                   drawn = loop_bbox(tilemap, visible_idx, dst, src);
    g_sink ^= drawn;

    for (int s = 0; s < N_REPS; s++) {
        uint64_t t0 = rdtsc();
        uint32_t d;
        if (kind == LOOP_FULL) d = loop_full(tilemap, dst, src);
        else                   d = loop_bbox(tilemap, visible_idx, dst, src);
        uint64_t t1 = rdtsc();
        samples[s] = t1 - t0;
        g_sink ^= d;
    }

    stats_t st;
    compute_stats(samples, N_REPS, &st, drawn);

    /* Convert cycles to ms via mhz_estimate. Conservative: PODP83 is
     * 83 MHz nominal; real CPU clock varies +/- 1% from that. The
     * conversion is for human-readable headline only; per-cycle
     * comparisons across scenarios use raw cycles for precision. */
    double med_ms = st.median / (mhz_estimate * 1000.0);
    double min_ms = (double)st.min / (mhz_estimate * 1000.0);
    double max_ms = (double)st.max / (mhz_estimate * 1000.0);

    plog("[scenario %-22s] N=%d  drawn=%-5u  cycles min=%9llu med=%9.0f mean=%9.0f p95=%9llu max=%9llu  (med ~%5.2f ms / range %5.2f-%5.2f)",
         label, N_REPS, st.expected_drawn,
         (unsigned long long)st.min, st.median, st.mean,
         (unsigned long long)st.p95, (unsigned long long)st.max,
         med_ms, min_ms, max_ms);
}

/* ============================================================ */
/* CPU MHz calibration — RDTSC-vs-uclock baseline                */
/*                                                               */
/* uclock ticks at 1193180 Hz (PIT mode-2 channel-0). RDTSC      */
/* ticks at CPU clock. Their ratio is CPU MHz. We measure this   */
/* once at probe start so the cycle->ms conversion is honest     */
/* across DOSBox-X (cycles=fixed scaling) AND real HW (true 83). */
/* ============================================================ */

static double calibrate_mhz(void)
{
    /* Spin for ~100 ms by uclock; measure RDTSC delta. */
    double t_start_uc = (double)uclock() / (double)UCLOCKS_PER_SEC;
    uint64_t t_start_tsc = rdtsc();
    while (((double)uclock() / (double)UCLOCKS_PER_SEC) - t_start_uc < 0.1) {
        /* spin */
    }
    uint64_t t_end_tsc = rdtsc();
    double t_end_uc = (double)uclock() / (double)UCLOCKS_PER_SEC;

    double secs = t_end_uc - t_start_uc;
    uint64_t cycles = t_end_tsc - t_start_tsc;
    double mhz = (double)cycles / (secs * 1e6);
    return mhz;
}

/* ============================================================ */
/* Setup helpers                                                 */
/* ============================================================ */

static void fill_realistic(int16_t *tilemap)
{
    /* ~50% non-zero distribution. Pseudo-random but deterministic so the
     * probe's "drawn" count matches across repeats and across emulator
     * vs real HW. srand seeded to fixed value. */
    srand(0xCAFE);
    for (int i = 0; i < MAP_N; i++) {
        if ((rand() & 1) == 0) {
            tilemap[i] = (int16_t)(1 + (rand() & 0x7F));  /* tile id 1..128 */
        } else {
            tilemap[i] = 0;
        }
    }
}

static void fill_zeros(int16_t *tilemap)
{
    memset(tilemap, 0, MAP_N * sizeof(int16_t));
}

static void build_bbox_index(uint16_t *visible_idx)
{
    int k = 0;
    for (int dy = 0; dy < BBOX_H; dy++) {
        for (int dx = 0; dx < BBOX_W; dx++) {
            int row = BBOX_Y0 + dy;
            int col = BBOX_X0 + dx;
            visible_idx[k++] = (uint16_t)(row * MAP_W + col);
        }
    }
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== TILEPROB wave-22.5 / iter H starting ===");
    plog("DJGPP build, target = visit-loop overhead measurement");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");
    plog("Question: how much wall-clock per flip does the engine save by");
    plog("switching from full 30000-tile visit-loop to bbox-only ~1900-tile");
    plog("loop? Answer drives nx-engine slot 0113 ship/no-ship decision.");
    plog("");

    /* Calibrate CPU MHz from RDTSC vs uclock. ~100 ms spin. */
    plog("Calibrating CPU MHz (~100 ms spin)...");
    double mhz = calibrate_mhz();
    plog("CPU clock estimate: %.3f MHz (PODP83 nominal = 83.0; DOSBox-X varies)",
         mhz);
    plog("");

    /* Allocate tilemaps + bbox index + memcpy buffers. */
    int16_t *tilemap_real = malloc(MAP_N * sizeof(int16_t));
    int16_t *tilemap_zero = malloc(MAP_N * sizeof(int16_t));
    uint16_t *visible_idx = malloc(BBOX_N * sizeof(uint16_t));
    uint8_t *dst = malloc(TILE_BYTES);
    uint8_t *src = malloc(TILE_BYTES);
    if (!tilemap_real || !tilemap_zero || !visible_idx || !dst || !src) {
        plog("FATAL: malloc failed");
        return 2;
    }

    fill_realistic(tilemap_real);
    fill_zeros(tilemap_zero);
    build_bbox_index(visible_idx);
    /* Initialize src buffer with non-trivial pattern; dst with zeros. */
    for (int i = 0; i < TILE_BYTES; i++) src[i] = (uint8_t)(i & 0xFF);
    memset(dst, 0, TILE_BYTES);

    plog("Tilemap: %dx%d = %d cells (int16_t)", MAP_W, MAP_H, MAP_N);
    plog("Bbox:    %dx%d = %d cells centered at (%d,%d)",
         BBOX_W, BBOX_H, BBOX_N, BBOX_X0, BBOX_Y0);
    plog("Tile blit stub: %d bytes/tile (16x16 INDEX8)", TILE_BYTES);
    plog("Reps per scenario: N=%d", N_REPS);
    plog("");

    /* Sanity check: count drawn tiles in realistic distribution. */
    uint32_t real_drawn_full = loop_full(tilemap_real, dst, src);
    uint32_t real_drawn_bbox = loop_bbox(tilemap_real, visible_idx, dst, src);
    plog("Realistic distribution: %u/%u full drawn (%.0f%%), %u/%u bbox drawn (%.0f%%)",
         real_drawn_full, (unsigned)MAP_N,
         100.0 * real_drawn_full / MAP_N,
         real_drawn_bbox, (unsigned)BBOX_N,
         100.0 * real_drawn_bbox / BBOX_N);
    plog("");

    /* ============================================================ */
    /* Run all 4 scenarios                                          */
    /* ============================================================ */
    plog("---- 4-scenario sweep (each is N=%d reps, RDTSC-timed) ----", N_REPS);
    run_scenario("A_full_realistic",      LOOP_FULL, tilemap_real, visible_idx, dst, src, mhz);
    run_scenario("A_full_zeros",          LOOP_FULL, tilemap_zero, visible_idx, dst, src, mhz);
    run_scenario("B_bbox_realistic",      LOOP_BBOX, tilemap_real, visible_idx, dst, src, mhz);
    run_scenario("B_bbox_zeros",          LOOP_BBOX, tilemap_zero, visible_idx, dst, src, mhz);
    plog("");

    /* Re-run the headline pair to compute the savings delta with same-batch
     * cache state (avoids "first run primed cache for second" bias). */
    plog("---- Headline pair re-run (back-to-back for same-batch comparison) ----");
    static uint64_t samples_A[N_REPS], samples_B[N_REPS];
    uint32_t dummy;
    for (int s = 0; s < N_REPS; s++) {
        uint64_t t0 = rdtsc();
        dummy = loop_full(tilemap_real, dst, src);
        uint64_t t1 = rdtsc();
        samples_A[s] = t1 - t0;
        g_sink ^= dummy;

        uint64_t t2 = rdtsc();
        dummy = loop_bbox(tilemap_real, visible_idx, dst, src);
        uint64_t t3 = rdtsc();
        samples_B[s] = t3 - t2;
        g_sink ^= dummy;
    }
    qsort(samples_A, N_REPS, sizeof samples_A[0], u64_cmp);
    qsort(samples_B, N_REPS, sizeof samples_B[0], u64_cmp);
    double med_A = (double)samples_A[N_REPS / 2];
    double med_B = (double)samples_B[N_REPS / 2];
    double med_A_ms = med_A / (mhz * 1000.0);
    double med_B_ms = med_B / (mhz * 1000.0);
    double savings_ms = med_A_ms - med_B_ms;

    plog("PAIRED A_full_realistic median:  %9.0f cycles  ~%5.2f ms",
         med_A, med_A_ms);
    plog("PAIRED B_bbox_realistic median:  %9.0f cycles  ~%5.2f ms",
         med_B, med_B_ms);
    plog("");

    plog("=== HEADLINE: visit-loop savings = %.2f ms / flip ===", savings_ms);
    plog("");

    /* Decision-criteria interpretation per team-lead brief. */
    plog("Decision criteria (per team-lead brief):");
    plog("  < 1.0 ms / flip   -> drop the optimization (integration risk > value)");
    plog("  1.0-2.0 ms / flip -> marginal; defer until other Levers exhausted");
    plog("  2.0-4.0 ms / flip -> ship as standard optimization");
    plog("  > 4.0 ms / flip   -> high-priority Lever; ship ASAP");
    plog("");
    if (savings_ms < 1.0) {
        plog("Result: %.2f ms < 1.0 -> RECOMMEND DROP slot 0113 visit-loop opt", savings_ms);
    } else if (savings_ms < 2.0) {
        plog("Result: %.2f ms in [1.0, 2.0) -> RECOMMEND DEFER slot 0113", savings_ms);
    } else if (savings_ms < 4.0) {
        plog("Result: %.2f ms in [2.0, 4.0) -> RECOMMEND SHIP slot 0113", savings_ms);
    } else {
        plog("Result: %.2f ms >= 4.0 -> RECOMMEND SHIP slot 0113 PRIORITY", savings_ms);
    }
    plog("");
    plog("Cross-check: pure iteration overhead (A_full_zeros - B_bbox_zeros)");
    plog("isolates the visit-loop iteration cost from the memcpy work. If");
    plog("savings >= overhead, the optimization is recouping wasted iterations.");
    plog("If savings < overhead, the realistic-vs-zero distribution affects");
    plog("the savings asymmetrically (drawn tiles in bbox vs full).");
    plog("");
    plog("Cross-check: PODP83 nominal MHz = 83.0; calibrated = %.1f. If they",
         mhz);
    plog("agree within 5%%, the cycle->ms conversion is trustworthy. If");
    plog("DOSBox-X under cycles=fixed mode reports >> 83 MHz, the savings");
    plog("number is unreliable for real-HW prediction (per dosbox_not_perf_proxy.md).");

    /* Print the read sink so the optimizer is forced to honor the loops. */
    plog("");
    plog("g_sink final = 0x%08lX (non-zero confirms loops weren't elided)",
         (unsigned long)g_sink);

    free(tilemap_real);
    free(tilemap_zero);
    free(visible_idx);
    free(dst);
    free(src);

    plog("");
    plog("=== TILEPROB done ===");

    if (g_log) fclose(g_log);
    return 0;
}
