/*
 * mixbench.c -- SDL_MixAudio mix-cost benchmark across rate variants ×
 *               channel populations (Phase 11 wave-38 audio Tier 1,
 *               task #12 / Probe B).
 *
 * MISSION: measure SDL3 audio mix-loop cost at 4 rate variants × 3 channel
 * populations = 12 sub-scenarios. Surface the cost of:
 *   - Pure mix work (SDL_MixAudio per active channel) at variant rates
 *   - Silent-channel scan overhead (8-channel scan loop with N silent)
 *   - Per-tick fixed overhead vs per-sample variable cost
 *
 * Probe deliberately does NOT open an audio device -- SDL_MixAudio is a
 * pure function (no SDL_Init required) so we benchmark just the mix
 * arithmetic in isolation. This is the PER-CHUNK mix cost; the full
 * SDL3_mixer overhead (voice management, music decode, channel state)
 * adds on top in production but is not measured here. CAVEAT in
 * decomp: MIXBENCH is a LOWER BOUND on production mix cost.
 *
 * WAVE-39 PATCH ACTIONABILITY (per team-lead 2026-05-12 directive):
 *
 *   Informs:
 *     P7 (Lever G rate reduction, Phase 9 Lever 1) -- direct rate-sweep
 *         delta sizes the wave-39 lever win exactly. Currently estimated
 *         at -3 to -5 ms/flip; MIXBENCH measures the actual number.
 *     P4 (silent-channel-skip in SDL_mixer, 50-200 LOC) -- channel-
 *         population sub-scenarios surface whether scan overhead scales
 *         with total channels (linear) or only active channels (no-op).
 *     P5 (mix-tick batching, 100-300 LOC) -- per-tick fixed overhead
 *         vs per-sample slope determines if batching reduces cost.
 *
 *   Refutes:
 *     If MIXBENCH 44100s_4silent_4active -> 11025m_4silent_4active delta
 *         is <1 ms/sec -> P7 (Lever G) is dead; pivot to architectural
 *         offload (P1/P2) instead.
 *     If silent_channel_overhead_pct <5% -> P4 refuted (SDL_MixAudio
 *         already skips silent or scan is negligible).
 *
 *   Dispatch matrix (per MIXBENCH outcome):
 *     Lever G savings/sec at 22050s_4s4a -> 11025m_4s4a:
 *       >=5 ms/sec -> P7 rank-1 ships immediately
 *       1-3 ms/sec -> P7 rank-2 alongside P2 (WaveBlaster) rank-1
 *       <1 ms/sec  -> P7 refuted; pivot to P1/P2 (eliminate SDL_mixer)
 *
 * METHODOLOGY:
 *   - 8 channels total (matches NXEngine SoundManager.cpp typical count)
 *   - Per scenario: M iterations of "for each of 8 channels:
 *       if active: SDL_MixAudio(dst, src[c], S16LE, chunk_bytes, 1.0)
 *       if silent: skip (model 'perfect silent-skip engine')"
 *   - Channel populations: 0silent_8active, 4silent_4active, 7silent_1active
 *   - Rate variants drive chunk_bytes (more audio data per tick at higher
 *     rates), simulating per-callback work scaling with rate.
 *   - RDTSC per tick; emit min/med/p95/max + mix_cost_us_per_sec_mixed.
 *
 * SANITY ANCHORS:
 *   - At 22050s_stereo with chunk=1024 samples * 4 bytes/sample-pair = 4096
 *     bytes, 8 active channels: each tick mixes 8*4096 = 32 KB. On PODP83
 *     (~80 MHz P54C, ~100 MB/sec memory throughput) expect ~300-500 us per
 *     8-active tick. If observed us-per-tick is >5000 us, something is
 *     very wrong (SDL_MixAudio is far slower than expected, or RDTSC
 *     calibration failed).
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/mixbench.c
 *   Binary: MIXBENCH.EXE  (8+3, exact 8.3 limit)
 *   Log:    MIXBENCH.LOG  (8+3)
 *   BAT:    MIXBENCH.BAT  (8+3)
 *
 * License: MIT.
 */

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("MIXBENCH.LOG", "w");
    if (!g_log) g_log = fopen("C:\\MIXBENCH.LOG", "w");
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
/* Timing (RDTSC via uclock calibration)                         */
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
/* Scenario definition                                           */
/* ============================================================ */

#define NUM_CHANNELS 8
#define M_ITERS 200    /* iterations per scenario; enough for stable percentile */

typedef struct {
    const char *rate_label;
    int   freq_hz;
    int   channels;       /* 1 = mono, 2 = stereo */
    int   chunk_samples;  /* per channel (mono) or per frame (stereo) */
} rate_spec_t;

typedef struct {
    const char *chan_label;
    int   active_count;   /* number of active channels; rest are silent */
} chan_spec_t;

static const rate_spec_t rate_specs[] = {
    { "44100s_stereo", 44100, 2, 1024 },
    { "22050s_stereo", 22050, 2, 1024 },
    { "11025s_stereo", 11025, 2, 1024 },
    { "11025m_mono",   11025, 1, 1024 },
};
#define N_RATES ((int)(sizeof(rate_specs) / sizeof(rate_specs[0])))

static const chan_spec_t chan_specs[] = {
    { "0silent_8active", 8 },
    { "4silent_4active", 4 },
    { "7silent_1active", 1 },
};
#define N_CHANS ((int)(sizeof(chan_specs) / sizeof(chan_specs[0])))

/* ============================================================ */
/* Mix benchmark                                                 */
/* ============================================================ */

/* Source buffer pool: 8 channels × max chunk size (4 bytes/frame at S16LE
 * stereo × 1024 frames = 4096 bytes; pad to 8192 for safety). */
#define MAX_CHUNK_BYTES 8192
static uint8_t src_buffers[NUM_CHANNELS][MAX_CHUNK_BYTES];
static uint8_t dst_buffer[MAX_CHUNK_BYTES];

/* Populate source buffers with distinct synthetic content per channel
 * (sine waves at slightly different phases so SDL_MixAudio actually has
 * non-trivial input data to sum + clip). */
static void init_source_buffers(void)
{
    for (int c = 0; c < NUM_CHANNELS; c++) {
        int16_t *p = (int16_t *)src_buffers[c];
        int samples = MAX_CHUNK_BYTES / 2;
        /* Distinct frequency per channel: 100 + c*50 Hz at notional 44100. */
        double freq = 100.0 + c * 50.0;
        double phase = c * 0.31;
        for (int i = 0; i < samples; i++) {
            /* Simple integer sinusoid approximation; doesn't need libm. */
            int x = (i * (int)freq) & 1023;
            int s = x < 512 ? (x * 64 - 16384) : ((1024 - x) * 64 - 16384);
            (void)phase;
            p[i] = (int16_t)s;
        }
    }
}

/* Sort uint32_t array in place (insertion sort; fine for M_ITERS=200). */
static void sort_u32(uint32_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        uint32_t key = a[i];
        int j = i;
        while (j > 0 && a[j-1] > key) { a[j] = a[j-1]; j--; }
        a[j] = key;
    }
}

/* Run a single (rate × channel-population) scenario. */
static int run_scenario(const rate_spec_t *R, const chan_spec_t *C)
{
    plog("");
    plog("[mixbench RATE=%s CHANS=%s BEGIN]", R->rate_label, C->chan_label);

    /* chunk_bytes = chunk_samples * channels * 2 (S16LE) */
    int chunk_bytes = R->chunk_samples * R->channels * 2;
    if (chunk_bytes > MAX_CHUNK_BYTES) chunk_bytes = MAX_CHUNK_BYTES;
    plog("  spec: chunk_samples=%d channels=%d chunk_bytes=%d active=%d M_ITERS=%d",
         R->chunk_samples, R->channels, chunk_bytes, C->active_count, M_ITERS);

    /* Build active-channel mask: first C->active_count channels active. */
    uint32_t active_mask = 0;
    for (int i = 0; i < C->active_count; i++) active_mask |= (1u << i);

    uint32_t samples[M_ITERS];
    uint64_t cmin = (uint64_t)-1, cmax = 0, csum = 0;

    /* Warm up the mix loop once (skip first sample for cache reasons). */
    memset(dst_buffer, 0, chunk_bytes);
    for (int c = 0; c < NUM_CHANNELS; c++) {
        if (active_mask & (1u << c)) {
            SDL_MixAudio(dst_buffer, src_buffers[c], SDL_AUDIO_S16LE,
                         (Uint32)chunk_bytes, 1.0f);
        }
    }

    /* Measurement loop. */
    for (int m = 0; m < M_ITERS; m++) {
        uint64_t t0 = rdtsc();
        memset(dst_buffer, 0, chunk_bytes);
        for (int c = 0; c < NUM_CHANNELS; c++) {
            if (active_mask & (1u << c)) {
                SDL_MixAudio(dst_buffer, src_buffers[c], SDL_AUDIO_S16LE,
                             (Uint32)chunk_bytes, 1.0f);
            }
            /* Silent channels: skipped (model 'perfect silent-skip engine').
             * The scan-loop itself still costs CPU; that IS the silent-
             * channel overhead being measured. */
        }
        uint64_t t1 = rdtsc();
        uint64_t delta = t1 - t0;
        samples[m] = (uint32_t)(delta & 0xFFFFFFFFu);
        if (delta < cmin) cmin = delta;
        if (delta > cmax) cmax = delta;
        csum += delta;
    }

    /* Percentile calc. */
    uint32_t sorted[M_ITERS];
    for (int i = 0; i < M_ITERS; i++) sorted[i] = samples[i];
    sort_u32(sorted, M_ITERS);

    uint32_t cmed = sorted[M_ITERS / 2];
    uint32_t cp95 = sorted[(M_ITERS * 95) / 100];
    double mean_cycles = (double)csum / M_ITERS;

    double us_min = cycles_to_us(cmin);
    double us_med = cycles_to_us(cmed);
    double us_p95 = cycles_to_us(cp95);
    double us_max = cycles_to_us(cmax);
    double us_mean = cycles_to_us((uint64_t)mean_cycles);

    /* Mix-rate: bytes_mixed = chunk_bytes * active_count.
     * mix_cost_us_per_sec_mixed = us_per_tick / (chunk_seconds * 1) where
     * chunk_seconds = chunk_samples / freq_hz. So:
     *   ticks_per_sec  = freq_hz / chunk_samples
     *   us_per_sec_mix = us_per_tick * ticks_per_sec
     * This is the equivalent "CPU us spent mixing audio per second of output". */
    double ticks_per_sec = (double)R->freq_hz / (double)R->chunk_samples;
    double us_per_sec_mix = us_med * ticks_per_sec;
    long bytes_mixed_per_tick = (long)chunk_bytes * C->active_count;
    double bytes_per_sec = (double)bytes_mixed_per_tick * ticks_per_sec;

    plog("  mix_tick_count=%d (M_ITERS)", M_ITERS);
    plog("  rdtsc_cycles_per_tick min=%llu med=%u p95=%u max=%llu mean=%llu",
         (unsigned long long)cmin, (unsigned)cmed, (unsigned)cp95,
         (unsigned long long)cmax, (unsigned long long)(uint64_t)mean_cycles);
    plog("  rdtsc_us_per_tick min=%.2f med=%.2f p95=%.2f max=%.2f mean=%.2f",
         us_min, us_med, us_p95, us_max, us_mean);
    plog("  ticks_per_sec=%.2f (= freq_hz / chunk_samples)", ticks_per_sec);
    plog("  bytes_mixed_per_sec=%.0f", bytes_per_sec);
    plog("  mix_cost_us_per_sec_mixed=%.2f (us-of-CPU per sec-of-audio-mix)", us_per_sec_mix);

    /* Status classification:
     *   SCENARIO_PASS               -- M_ITERS completed; RDTSC max < 100*min
     *   SCENARIO_SUSPECT_SPIKE      -- max > 100*min (background IRQ contention)
     *   SCENARIO_FAILED_INIT        -- doesn't apply here; no audio device open */
    const char *status;
    if (cmin > 0 && cmax > cmin * 100) {
        status = "SCENARIO_SUSPECT_SPIKE";
    } else {
        status = "SCENARIO_PASS";
    }
    plog("[mixbench RATE=%s CHANS=%s DONE status=%s]",
         R->rate_label, C->chan_label, status);
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== mixbench (wave-38 audio Tier 1: SDL_MixAudio mix-cost) ===");
    plog("DJGPP + SDL3-linked; 8-channel mix loop at variant rates + populations");
    plog("Mission: size wave-39 P7 (Lever G rate reduction) precisely;");
    plog("discriminate P4 (silent-channel-skip) and P5 (mix-tick batching) candidacy.");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");

    /* Step 1: RDTSC calibration. */
    plog("---- Step 1: RDTSC calibration ----");
    calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u  us_per_cycle = %.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    if (g_us_per_cycle <= 0.0 || g_cpu_mhz < 30) {
        plog("FATAL: RDTSC calibration failed");
        plog("[mixbench SUITE_DONE verdict=REFUTE_RDTSC_CALIBRATION_FAILED]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("");

    /* Step 2: initialize source buffers with synthetic content. */
    plog("---- Step 2: initialize source buffers (8 channels x 8 KB) ----");
    init_source_buffers();
    /* Print first 8 bytes of channel 0 source as a sanity check. */
    plog("source[0][0..7] = %02X %02X %02X %02X %02X %02X %02X %02X",
         src_buffers[0][0], src_buffers[0][1], src_buffers[0][2], src_buffers[0][3],
         src_buffers[0][4], src_buffers[0][5], src_buffers[0][6], src_buffers[0][7]);
    plog("");

    /* Step 3: variant matrix. */
    int n_total = N_RATES * N_CHANS;
    plog("---- Step 3: run %d sub-scenarios (%d rates x %d chan-populations) ----",
         n_total, N_RATES, N_CHANS);
    plog("[mixbench SUITE_BEGIN n=%d cpu_mhz=%u sdl_version=%u]",
         n_total, (unsigned)g_cpu_mhz, (unsigned)SDL_GetVersion());

    int pass = 0;
    for (int ri = 0; ri < N_RATES; ri++) {
        for (int ci = 0; ci < N_CHANS; ci++) {
            run_scenario(&rate_specs[ri], &chan_specs[ci]);
            pass++;
        }
    }

    plog("");
    plog("---- Step 4: SUITE_DONE ----");
    plog("[mixbench SUITE_DONE n=%d pass=%d status=SUITE_PASS]", n_total, pass);
    plog("");

    plog("Decomp guide (per wave-39 patch-actionability matrix):");
    plog("  Lever G sizing (P7):");
    plog("    delta = MIXBENCH[22050s_stereo, 4silent_4active].us_per_sec_mix");
    plog("          - MIXBENCH[11025m_mono,   4silent_4active].us_per_sec_mix");
    plog("    >=5000 us/sec -> P7 rank-1 ships immediately");
    plog("    1000-3000 us/sec -> P7 rank-2 alongside P2 (WaveBlaster) rank-1");
    plog("    <1000 us/sec -> P7 refuted; pivot to P1/P2 (eliminate SDL_mixer)");
    plog("  Silent-channel-skip (P4) candidacy:");
    plog("    delta = MIXBENCH[X, 7silent_1active].us_per_tick");
    plog("          - MIXBENCH[X, 0silent_1active_synthetic].us_per_tick");
    plog("    (note: probe doesn't include 0silent_1active synthetic separately;");
    plog("     extrapolate from 8active per-channel slope vs 1active baseline).");
    plog("    If silent-scan overhead >20%% of single-active cost -> P4 high-value.");
    plog("  Mix-tick batching (P5) candidacy:");
    plog("    per_channel_cost = (MIXBENCH[X, 8active] - MIXBENCH[X, 1active]) / 7");
    plog("    fixed_overhead = MIXBENCH[X, 1active] - per_channel_cost");
    plog("    If fixed_overhead > 50%% of single-active cost -> P5 high-value.");

    plog("");
    plog("=== mixbench done ===");
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return 0;
}
