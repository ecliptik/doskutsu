/*
 * bandcomp.c -- banded L1-resident composition gate probe.
 *
 * Phase 11 wave-52/53, probe-engineer task #25. Standalone DJGPP probe;
 * NO SDL, NO engine, NO C++. Authored to the contract in
 * docs/internal/BANDCOMP-PROBE-DESIGN.md (flush-instr, contract owner).
 *
 * THE QUESTION (BANDCOMP-PROBE-DESIGN sec.0):
 *   Banded composition restructures the renderer layer-major -> band-major:
 *   each 8-16 row band has all its layers composed while the band's
 *   destination region stays resident in the P54C 8 KB L1 data cache,
 *   then flushed once. The win is real ONLY IF the band destination
 *   actually STAYS L1-resident across the layer passes -- the threat is
 *   that the interleaved SOURCE reads from the ~64 KB tileset conflict-
 *   evict the band lines mid-composition (P54C L1 = 8 KB, 2-way). That
 *   is unconfirmable by analysis; it must be MEASURED on g2k. DOSBox-X
 *   does not model P54C L1 conflict behaviour ([[dosbox_not_proxy]]).
 *
 * WHAT IT MEASURES (sec.2): three kernels, each at BOTH band sizes
 * (8-row 2560 B, 16-row 5120 B). All buffers are plain malloc'd sysmem
 * (normal flat DJGPP pointers -- the probe models CPU-cache behaviour,
 * not VRAM, so no __djgpp_nearptr is needed):
 *
 *   Kernel A -- T_L1:       ideal L1-resident band, no tileset-source
 *                           pressure. Upper bound.
 *   Kernel B -- T_pressure: realistic band-major composition -- band
 *                           writes interleaved with scattered ~64 KB
 *                           tileset-source reads. THE load-bearing
 *                           measurement.
 *   Kernel C -- T_cold:     today's layer-major architecture -- the
 *                           destination cycles through a 76800 B
 *                           framebuffer so it never stays resident,
 *                           same interleaved source reads. Floor.
 *
 *   resid_frac = (T_pressure - T_cold) / (T_L1 - T_cold)
 *     = the fraction of the L1 advantage that survives realistic
 *       tileset-source pressure.
 *
 * DECISION GATE (sec.3, applied by flush-instr from the log -- prefer
 * the 16-row size):
 *   GREEN  resid_frac >= 0.50  -- banding retains >=half the L1 win.
 *   RED    resid_frac <= 0.25  -- source reads evict the band; dead.
 *   AMBER  0.25 < resid_frac < 0.50 -- partial residency.
 *
 * KERNEL-A IMPLEMENTATION NOTE (probe-engineer -> flush-instr):
 *   The contract sec.2 phrases kernel A as "write the band-sized
 *   destination buffer N times. NO source reads interleaved." A literal
 *   memset would change the WRITE PRIMITIVE relative to kernels B/C
 *   (which copy 256 B tiles), inflating T_L1 by the memset-vs-memcpy
 *   gap -- a difference that has nothing to do with cache residency and
 *   would bias resid_frac toward RED. To keep resid_frac isolating
 *   *cache residency only*, kernel A here uses the SAME 256 B copy
 *   primitive as B/C, sourced from a tiny 256 B buffer that is always
 *   L1-resident -- i.e. "no source PRESSURE", which is the contract's
 *   intent. If flush-instr wants the literal memset reading instead,
 *   flip A_USE_RESIDENT_COPY to 0 (one #define) and rebuild.
 *
 * Output: C:\BANDCOMP.LOG (fsync per line), fallback BANDCOMP.LOG.
 *
 * Pure DJGPP libc. -march=i486 -mtune=pentium -O2, no MMX/SSE (P54C
 * predates them). size_t is 32-bit on DJGPP -- all byte math kept in
 * uint32_t / double; no ftell/off_t use.
 *
 * 8.3 DOS filename: BANDCOMP.EXE (8.3) -- fits.
 *
 * Runtime: ~3 sec (6 kernels x ~0.3 s timed + calibration).
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

/* Flip to 0 for the literal-memset reading of contract kernel A. */
#define A_USE_RESIDENT_COPY 1

/* ============================================================ */
/* Geometry constants (BANDCOMP-PROBE-DESIGN sec.1 + sec.2)      */
/* ============================================================ */

#define FB_WIDTH       320          /* INDEX8 framebuffer width      */
#define BAND8_BYTES   (FB_WIDTH * 8)   /* 8-row band  = 2560 B       */
#define BAND16_BYTES  (FB_WIDTH * 16)  /* 16-row band = 5120 B       */
#define FB_BYTES       76800        /* 320x240 INDEX8 full frame     */
#define SRC64K         65536        /* tileset source footprint      */
#define TILE_BYTES     256          /* 16x16 INDEX8 tile             */
#define TILE_SLOTS    (SRC64K / TILE_BYTES)   /* 256 tile slots      */

#define TILES_PER_PASS 30           /* ~tiles touched per band pass  */
#define LAYER_PASSES   3            /* backdrop + BG + FG (post-w53) */
/* dest bytes written per composite unit (identical for all 3 kernels)
 * so the three MB/s rates are directly comparable. */
#define COMPOSITE_BYTES ((uint32_t)TILES_PER_PASS * LAYER_PASSES * TILE_BYTES)

/* fixed LCG seed -- deterministic scattered tile sequence (sec.2 B). */
#define LCG_SEED       0x1234ABCDu

/* Timed-run shape. Each kernel is sampled in ROUNDS interleaved chunks
 * (round-robin A,B,C,A,B,C,...) so any monotonic drift across the run
 * -- DOSBox-X cycles=auto ramp + dynrec warmup in smoke, branch/TLB
 * warmup on real HW -- hits all three kernels equally and cancels in
 * the per-kernel MEDIAN. Single-run-per-kernel was order-biased (the
 * DOSBox-X smoke surfaced a clean monotonic A<B<C inversion). Median-
 * of-samples matches the l1fill.c precedent ([[probe_authoring_
 * discipline]] cross-anchor reuse). ROUNDS*CHUNK_SECS ~= 0.3 s timed
 * per kernel, well above the ~838 ns uclock granularity. */
#define ROUNDS         15          /* odd -> exact median element     */
#define CHUNK_SECS     0.020       /* timed chunk, per kernel, per round */
#define CALIB_COMPS    64

/* Cross-anchors -- podp83_membw_real_hw_actuals.md (carried forward as
 * named constants per [[probe_authoring_discipline]] cross-anchor reuse).
 * Used ONLY for emit-line plausibility bounds, never asserted as a
 * prediction ([[perf_measurement_discipline]] -- the probe MEASURES). */
#define ANCHOR_SYSMEM_MBS  17.0     /* g2k sysmem memcpy              */
#define ANCHOR_L1_MBS      40.8     /* g2k L1                        */
/* loose plausibility window: any throughput outside this is suspect. */
#define PLAUS_LO_MBS        0.5
#define PLAUS_HI_MBS     1000.0

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
    g_log = fopen("C:\\BANDCOMP.LOG", "w");
    if (!g_log) g_log = fopen("BANDCOMP.LOG", "w");
}

/* ============================================================ */
/* Timing                                                       */
/* ============================================================ */

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

/* ============================================================ */
/* Buffers + state                                              */
/* ============================================================ */

static uint8_t *g_band   = NULL;    /* fixed band dest, max 5120 B   */
static uint8_t *g_src64k = NULL;    /* 64 KB tileset source          */
static uint8_t *g_cold   = NULL;    /* 76800 B layer-major dest      */
static uint8_t  g_resident[TILE_BYTES]; /* always-L1-resident source */

static uint32_t g_lcg     = LCG_SEED;   /* scattered-tile sequence   */
static uint32_t g_cold_w  = 0;          /* cold dest running cursor  */
static volatile uint32_t g_sink = 0;    /* anti-elision checksum sink*/

/* next scattered tile-source byte offset within the 64 KB sheet --
 * a fixed pseudo-random (NOT linear) sequence, seed fixed. */
static uint32_t next_src_off(void)
{
    g_lcg = g_lcg * 1103515245u + 12345u;
    return ((g_lcg >> 8) & (TILE_SLOTS - 1)) * (uint32_t)TILE_BYTES;
}

/* ============================================================ */
/* Kernels -- one composite unit = LAYER_PASSES x TILES_PER_PASS */
/* tile copies of TILE_BYTES each. COMPOSITE_BYTES dest bytes.    */
/* ============================================================ */

/* Kernel A -- ideal L1-resident band. Same 256 B copy primitive as
 * B/C, source = the always-resident 256 B buffer (no source pressure).
 * See KERNEL-A IMPLEMENTATION NOTE in the file header. */
static void composite_A(uint32_t band_bytes)
{
    uint32_t slots = band_bytes / TILE_BYTES;
    uint32_t w;
    for (w = 0; w < (uint32_t)(TILES_PER_PASS * LAYER_PASSES); w++) {
        uint32_t doff = (w % slots) * (uint32_t)TILE_BYTES;
#if A_USE_RESIDENT_COPY
        memcpy(g_band + doff, g_resident, TILE_BYTES);
#else
        memset(g_band + doff, (int)(w & 0xFF), TILE_BYTES);
#endif
    }
}

/* Kernel B -- realistic band under tileset-source pressure. The
 * load-bearing kernel: band writes interleaved with scattered 64 KB
 * source reads. Band dest is the SAME fixed region every pass. */
static void composite_B(uint32_t band_bytes)
{
    uint32_t slots = band_bytes / TILE_BYTES;
    uint32_t w;
    for (w = 0; w < (uint32_t)(TILES_PER_PASS * LAYER_PASSES); w++) {
        uint32_t doff = (w % slots) * (uint32_t)TILE_BYTES;
        uint32_t soff = next_src_off();
        memcpy(g_band + doff, g_src64k + soff, TILE_BYTES);
    }
}

/* Kernel C -- cold sysmem baseline (today's layer-major). Same
 * scattered source reads as B; dest cycles through the full 76800 B
 * framebuffer so it never stays resident. */
static void composite_C(uint32_t band_bytes)
{
    uint32_t w;
    (void)band_bytes;               /* C does not use the band region */
    for (w = 0; w < (uint32_t)(TILES_PER_PASS * LAYER_PASSES); w++) {
        uint32_t doff = (g_cold_w % (FB_BYTES / TILE_BYTES))
                            * (uint32_t)TILE_BYTES;
        uint32_t soff = next_src_off();
        memcpy(g_cold + doff, g_src64k + soff, TILE_BYTES);
        g_cold_w++;
    }
}

/* ============================================================ */
/* Kernel runner -- calibrate, then ROUNDS interleaved chunks    */
/* ============================================================ */

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* per-kernel result of one band size. */
typedef struct {
    double med;         /* median chunk throughput, MB/s             */
    double lo;          /* min chunk throughput                      */
    double hi;          /* max chunk throughput                      */
    long   chunkK;      /* composites per timed chunk                */
} kres_t;

/* Calibrate one kernel: pick the composite count whose timed cost is
 * ~CHUNK_SECS, so each of the ROUNDS chunks is well above timer
 * granularity. State is reset deterministically before+after. */
static long calib_chunkK(void (*comp)(uint32_t), uint32_t band_bytes)
{
    long i, k;
    double t0, dt, per;

    g_lcg = LCG_SEED; g_cold_w = 0;
    comp(band_bytes);               /* warm-up composite (discarded)  */

    g_lcg = LCG_SEED; g_cold_w = 0;
    t0 = now_secs();
    for (i = 0; i < CALIB_COMPS; i++) comp(band_bytes);
    dt  = now_secs() - t0;
    per = dt / (double)CALIB_COMPS;
    if (per <= 0.0) per = 1e-6;     /* guard timer granularity        */
    k = (long)(CHUNK_SECS / per) + 1;
    if (k < 8) k = 8;
    return k;
}

/* Time one chunk of `chunkK` composites; return chunk throughput MB/s.
 * State reset per chunk -> every chunk is the IDENTICAL deterministic
 * workload, so the ROUNDS samples are directly comparable. */
static double time_chunk(void (*comp)(uint32_t), uint32_t band_bytes,
                         long chunkK)
{
    long i;
    double t0, dt, bytes;

    g_lcg = LCG_SEED; g_cold_w = 0;
    t0 = now_secs();
    for (i = 0; i < chunkK; i++) comp(band_bytes);
    dt = now_secs() - t0;

    /* anti-elision: fold dest bytes into the volatile sink. */
    g_sink += g_band[0] + g_band[band_bytes - 1] + g_cold[0]
                + g_cold[FB_BYTES - 1];

    bytes = (double)chunkK * (double)COMPOSITE_BYTES;
    return (dt > 0.0) ? bytes / (dt * 1048576.0) : 0.0;
}

/* ============================================================ */
/* Per-band-size driver                                          */
/* ============================================================ */

static int g_warn_count = 0;

static void plaus_check(const char *what, double mbs)
{
    if (mbs < PLAUS_LO_MBS || mbs > PLAUS_HI_MBS) {
        g_warn_count++;
        llog("BANDCOMP-WARN %s = %.2f MB/s OUTSIDE plausible window "
             "[%.1f, %.1f] -- emit suspect (cf. anchors sysmem=%.1f "
             "L1=%.1f MB/s)", what, mbs, PLAUS_LO_MBS, PLAUS_HI_MBS,
             ANCHOR_SYSMEM_MBS, ANCHOR_L1_MBS);
    }
}

/* sample a kernel over ROUNDS interleaved chunks -> median/min/max.
 * The ROUNDS samples for sA/sB/sC are filled round-robin by run_band. */
static kres_t reduce_kernel(const char *tag, int band_label,
                            double *samples, long chunkK)
{
    kres_t r;
    double tmp[ROUNDS];
    int i;

    for (i = 0; i < ROUNDS; i++) tmp[i] = samples[i];
    qsort(tmp, ROUNDS, sizeof tmp[0], dbl_cmp);
    r.med    = tmp[ROUNDS / 2];
    r.lo     = tmp[0];
    r.hi     = tmp[ROUNDS - 1];
    r.chunkK = chunkK;

    llog("BANDCOMP-RAW size=%-2d kernel=%-9s chunkK=%-5ld rounds=%d  "
         "MB/s med=%7.2f min=%7.2f max=%7.2f",
         band_label, tag, chunkK, ROUNDS, r.med, r.lo, r.hi);
    return r;
}

static void run_band(int band_label, uint32_t band_bytes)
{
    kres_t a, b, c;
    double denom, resid;
    double sA[ROUNDS], sB[ROUNDS], sC[ROUNDS];
    long kA, kB, kC;
    int r;

    llog("---- band size = %d rows (%lu B dest region) ----",
         band_label, (unsigned long)band_bytes);

    /* calibrate each kernel's per-chunk composite count. */
    kA = calib_chunkK(composite_A, band_bytes);
    kB = calib_chunkK(composite_B, band_bytes);
    kC = calib_chunkK(composite_C, band_bytes);

    /* ROUNDS interleaved chunks: A,B,C,A,B,C,... -- monotonic drift
     * (cycle ramp / warmup) lands on all three equally and cancels in
     * the per-kernel median. */
    for (r = 0; r < ROUNDS; r++) {
        sA[r] = time_chunk(composite_A, band_bytes, kA);
        sB[r] = time_chunk(composite_B, band_bytes, kB);
        sC[r] = time_chunk(composite_C, band_bytes, kC);
    }

    a = reduce_kernel("T_L1",       band_label, sA, kA);
    b = reduce_kernel("T_pressure", band_label, sB, kB);
    c = reduce_kernel("T_cold",     band_label, sC, kC);

    /* derived-metric sanity ([[probe_authoring_discipline]] -- SDLPROBE
     * post-mortem: structural smoke is necessary but not sufficient). */
    plaus_check("T_L1",       a.med);
    plaus_check("T_pressure", b.med);
    plaus_check("T_cold",     c.med);

    if (!(a.med >= b.med && b.med >= c.med)) {
        g_warn_count++;
        llog("BANDCOMP-WARN size=%d ordering T_L1>=T_pressure>=T_cold "
             "VIOLATED (%.2f / %.2f / %.2f MB/s) -- small inversions "
             "are timer noise; a large one means a kernel defect. NOTE: "
             "DOSBox-X has no L1 model + ramps cycles, so an inversion "
             "under emulation is EXPECTED ([[dosbox_not_proxy]]); the "
             "verdict is real-HW-only.",
             band_label, a.med, b.med, c.med);
    }

    denom = a.med - c.med;
    if (denom <= 0.0) {
        /* resid_frac undefined -- emit sentinel, do NOT print garbage. */
        g_warn_count++;
        llog("BANDCOMP-WARN size=%d T_L1 (%.2f) <= T_cold (%.2f): no L1 "
             "advantage exists; resid_frac undefined. Probe inconclusive "
             "for this band size -- flush-instr: treat as no-signal, not "
             "RED.", band_label, a.med, c.med);
        llog("[bandcomp size=%-2d T_L1=%.1f T_pressure=%.1f T_cold=%.1f "
             "MB/s  resid_frac=UNDEF]",
             band_label, a.med, b.med, c.med);
        return;
    }

    resid = (b.med - c.med) / denom;

    /* the contract STAT line -- flush-instr keys the sec.3 gate off it. */
    llog("[bandcomp size=%-2d T_L1=%.1f T_pressure=%.1f T_cold=%.1f "
         "MB/s  resid_frac=%.2f]",
         band_label, a.med, b.med, c.med, resid);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    uint32_t i;

    (void)argc; (void)argv;

    open_log();
    llog("=== BANDCOMP wave-52/53 banded-composition gate probe ===");
    llog("BANDCOMP-BEGIN");
    llog("contract: docs/internal/BANDCOMP-PROBE-DESIGN.md (flush-instr)");
    llog("build: DJGPP -march=i486 -mtune=pentium -O2, no MMX/SSE");
    llog("UCLOCKS_PER_SEC = %lu (resolution ~%.0f ns)",
         (unsigned long)UCLOCKS_PER_SEC, 1e9 / (double)UCLOCKS_PER_SEC);
    llog("kernel A primitive = %s",
         A_USE_RESIDENT_COPY ? "256B copy from resident src (no pressure)"
                             : "memset (literal contract reading)");
    llog("geometry: composite = %d passes x %d tiles x %d B = %lu B dest",
         LAYER_PASSES, TILES_PER_PASS, TILE_BYTES,
         (unsigned long)COMPOSITE_BYTES);

    g_band   = (uint8_t *)malloc(BAND16_BYTES);
    g_src64k = (uint8_t *)malloc(SRC64K);
    g_cold   = (uint8_t *)malloc(FB_BYTES);
    if (!g_band || !g_src64k || !g_cold) {
        llog("FATAL: malloc failed (band=%p src=%p cold=%p)",
             (void *)g_band, (void *)g_src64k, (void *)g_cold);
        free(g_band); free(g_src64k); free(g_cold);
        if (g_log) fclose(g_log);
        return 2;
    }

    /* fill + page-touch every buffer so demand-paging does not bias the
     * first timed batch. */
    for (i = 0; i < SRC64K; i++)      g_src64k[i] = (uint8_t)(i * 7 + 1);
    for (i = 0; i < BAND16_BYTES; i++) g_band[i]  = 0;
    for (i = 0; i < FB_BYTES; i++)     g_cold[i]  = 0;
    for (i = 0; i < TILE_BYTES; i++)   g_resident[i] = (uint8_t)(i + 0x40);

    llog("BUF: band=%p (%d B)  src64k=%p (%d B)  cold=%p (%d B)",
         (void *)g_band, BAND16_BYTES, (void *)g_src64k, SRC64K,
         (void *)g_cold, FB_BYTES);
    llog("");

    run_band(16, BAND16_BYTES);     /* primary -- aligns to tile height */
    llog("");
    run_band(8,  BAND8_BYTES);      /* fall-back size                   */
    llog("");

    llog("BANDCOMP-SELFTEST warnings=%d sink=%lu",
         g_warn_count, (unsigned long)g_sink);
    if (g_warn_count == 0)
        llog("BANDCOMP-SELFTEST OK -- all emit lines plausibility-bound");
    else
        llog("BANDCOMP-SELFTEST FLAGGED -- review BANDCOMP-WARN lines "
             "before applying the sec.3 gate");

    llog("");
    llog("Gate (BANDCOMP-PROBE-DESIGN sec.3, prefer size=16):");
    llog("  resid_frac >= 0.50  GREEN -- banding retains >=half L1 win");
    llog("  resid_frac <= 0.25  RED   -- source reads evict band; dead");
    llog("  0.25 < rf  < 0.50   AMBER -- partial residency");
    llog("flush-instr applies the gate from the [bandcomp ...] lines.");
    llog("BANDCOMP-DONE");

    free(g_band);
    free(g_src64k);
    free(g_cold);
    if (g_log) fclose(g_log);
    return 0;
}
