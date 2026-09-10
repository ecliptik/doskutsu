/*
 * orgsynth.c -- Organya live-synth cost benchmark per audio-callback chunk
 *               (Phase 11 wave-38 audio Tier 2 stretch, task #12 / Probe C).
 *
 * MISSION: isolate Organya's per-sample synth cost from SDL_mixer mix
 * infrastructure cost + SB16 IRQ-hook cost. Cave Story uses Organya for
 * nearly all music, synthesized LIVE inside the audio callback (NOT
 * pre-rendered). This probe ports the core Song::Synth() per-sample math
 * from vendor/nxengine-evo/src/sound/Organya.cpp (lines 282-328) and times
 * the synth loop in isolation at 4 rate variants.
 *
 * PIXPROB PRECEDENT (per pixprobe.c header): probe is "faithfully cribbed
 * from {synth} -- same instruction mix, same per-sample math. NOT
 * functional {music} (no real waveform output, no sound played); this
 * probe only times the cost." Same applies here for Organya. We are
 * measuring per-sample CPU cost, not validating synth correctness.
 *
 * SYNTHETIC WAVETABLE: instead of loading wavetable.dat + drum samples
 * (which would require ResourceManager + 50+ KB of embedded data and add
 * porting risk), we generate a synthetic 100x256 int16_t wavetable with
 * triangle-equivalent waveforms at probe init. The per-sample math is
 * IDENTICAL; only the wave-content is synthetic. The instruction mix
 * (one array lookup, one modulo, two FP multiply-adds, one FP add per
 * sample) is what we benchmark.
 *
 * WAVE-39 PATCH ACTIONABILITY (per team-lead 2026-05-12 directive):
 *
 *   Informs:
 *     P1 (OPL3 backend, 1-2 wk eng) + P2 (WaveBlaster MIDI, 3-5 days;
 *         user-preferred per 2026-04-30 direction). Both eliminate
 *         Organya CPU synthesis via hardware MIDI/FM offload. ORGSYNTH
 *         measures the CPU work being eliminated.
 *     P8 (cooperative-yield cadence tuning) -- per-chunk wall-clock
 *         determines whether yield-cadence has room to reduce blocking.
 *     P7 (Lever G, secondary) -- 4-rate sweep surfaces whether Organya
 *         cost scales linearly with rate (Lever G helps) or is rate-
 *         insensitive (Lever G doesn't reduce Organya portion).
 *
 *   Refutes:
 *     If ORGSYNTH share-of-audio_thread <25% -> P1 + P2 refuted. Music
 *         synth is not the bottleneck; pivot to P3 (SfxSynth pre-render)
 *         + P5/P7 (mix-side).
 *     If ORGSYNTH share <15% -> even WaveBlaster (3-5 day work) doesn't
 *         pay back; close architectural offload as wave-39+ candidate.
 *
 *   Dispatch matrix (per ORGSYNTH share of audio_thread):
 *     >50% (Organya-dominated)   -> P2 rank-1 (user-preferred WB MIDI);
 *                                   P1 (OPL3) reserved fallback.
 *     25-50% (balanced)          -> P7 rank-1 + P3 rank-2 bundle.
 *     <25% (Organya-minor)       -> P3 rank-1; P4 or P5 rank-2; P1/P2 refuted.
 *
 * METHODOLOGY:
 *   - Synthetic 100x256 int16_t wavetable populated with triangle-
 *     equivalent waves at probe init.
 *   - 6 simulated active instruments (matches Cave Story typical) with
 *     hard-coded phase/freq/pan/vol per instrument.
 *   - Per scenario: synthesize N chunks of CHUNK_SAMPLES samples; RDTSC
 *     each chunk. Output: us_per_chunk + synth_cost_us_per_sec_output.
 *   - Variants: 4 rates × 1 channel-population = 4 scenarios.
 *
 * SANITY ANCHORS:
 *   - At 22050s_stereo with chunk=512 samples, 6 instruments, expect
 *     ~500-1500 us per chunk on PODP83 (P54C + 80 MHz FPU). chunks/sec
 *     = 22050/512 = ~43 chunks/sec; CPU usage ~22-65 ms/sec of audio.
 *     If observed us-per-chunk is >10000 us, something is far slower
 *     than expected.
 *
 * 8.3 DOS filenames:
 *   Source: tests/probes/orgsynth.c
 *   Binary: ORGSYNTH.EXE  (8+3, exact 8.3 limit)
 *   Log:    ORGSYNTH.LOG  (8+3)
 *   BAT:    ORGSYNTH.BAT  (8+3)
 *
 * License: MIT.
 */

#include <math.h>
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
    g_log = fopen("ORGSYNTH.LOG", "w");
    if (!g_log) g_log = fopen("C:\\ORGSYNTH.LOG", "w");
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
/* Organya synth core (faithfully cribbed from Song::Synth)      */
/* ============================================================ */

/* WaveTable: 100 waves × 256 samples each, int16_t. Synthetic content
 * (triangle-equivalent waveforms with varying period per wave-index)
 * preserves the per-sample-lookup instruction mix without needing the
 * real wavetable.dat. Cribbed sizing from Organya.cpp line 37. */
#define NUM_WAVES 100
#define WAVE_LEN  256
static int16_t WaveTable[NUM_WAVES * WAVE_LEN];

static void init_wavetable(void)
{
    for (int w = 0; w < NUM_WAVES; w++) {
        int period = 8 + (w % 32);  /* periods 8..39 */
        for (int s = 0; s < WAVE_LEN; s++) {
            int phase = s % period;
            int amp = (phase < period / 2)
                      ? (phase * 32767 / (period / 2) - 16384)
                      : ((period - phase) * 32767 / (period / 2) - 16384);
            WaveTable[w * WAVE_LEN + s] = (int16_t)amp;
        }
    }
}

/* Per-instrument state: matches the relevant fields from Organya.cpp's
 * `ins` member struct (Instrument). Probe ports only the fields used
 * by the inner per-sample loop, not the event-handling or note-mgmt. */
typedef struct {
    double  phaseacc;
    double  phaseinc;
    int     wave_step;
    const int16_t *cur_wave;
    int     cur_wavesize;
    int     cur_length;
    double  cur_vol;
    int     cur_pan;       /* 0..12, indexes panning_table */
} instrument_t;

/* Tables from Organya.cpp lines 179-183 (DOS-PORT: preserved verbatim). */
static const int panning_table[13] = {0, 43, 86, 129, 172, 215, 256, 297, 340, 383, 426, 469, 512};

/* Output mix buffer (stereo float32 samples, 2 floats per frame). */
#define MAX_CHUNK_SAMPLES 1024
static float mix_samples[MAX_CHUNK_SAMPLES * 2];

/* Core synth loop -- faithfully cribbed from Organya.cpp lines 282-328
 * (nearest-neighbour interpolation path, which is the cheap default). */
static void synth_chunk(instrument_t *ins, int n_instruments,
                        int chunk_samples)
{
    /* Zero the mix buffer per chunk (matches Song::Synth samples.resize 0.f). */
    memset(mix_samples, 0, chunk_samples * 2 * sizeof(float));

    for (int i = 0; i < n_instruments; i++) {
        instrument_t *I = &ins[i];

        /* Panning math (Organya.cpp lines 268-279). */
        int pan_idx = I->cur_pan;
        if (pan_idx < 0) pan_idx = 0;
        if (pan_idx > 12) pan_idx = 12;
        const double pan = (panning_table[pan_idx] - 256) * 10.0;
        double left  = 1.0;
        double right = 1.0;
        if (pan < 0)      right = pow(10.0, pan  / 2000.0);
        else if (pan > 0) left  = pow(10.0, -pan / 2000.0);
        left  *= I->cur_vol;
        right *= I->cur_vol;

        /* Per-sample loop (Organya.cpp lines 282-328, nearest-neighbour). */
        int n = (chunk_samples > I->cur_length) ? I->cur_length : chunk_samples;
        for (int p = 0; p < n; p++) {
            const double pos = I->phaseacc * I->wave_step;
            double sample = (double)I->cur_wave[ ((unsigned)pos) % I->cur_wavesize ];
            mix_samples[p * 2 + 0] += (float)(sample * left);
            mix_samples[p * 2 + 1] += (float)(sample * right);
            I->phaseacc += I->phaseinc;
        }
        I->cur_length -= n;
        /* Re-arm cur_length if exhausted, so the loop keeps running
         * across many chunks for measurement purposes (a real song would
         * trigger new notes per beat, but the per-sample math is the same). */
        if (I->cur_length <= 0) {
            I->cur_length = chunk_samples * 10;
            I->phaseacc = 0;
        }
    }
}

/* ============================================================ */
/* Scenario definition                                           */
/* ============================================================ */

typedef struct {
    const char *rate_label;
    int   freq_hz;
    int   stereo;          /* 1 = stereo, 0 = mono (mono = half mix-buffer use) */
    int   chunk_samples;   /* per-chunk sample count */
    int   n_chunks;        /* total chunks to synthesize per scenario */
} rate_spec_t;

static const rate_spec_t rate_specs[] = {
    /* chunk_samples picked so each chunk corresponds to ~11.6 ms at 22050s,
     * a common SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES setting. */
    { "44100_stereo", 44100, 1, 512, 200 },
    { "22050_stereo", 22050, 1, 512, 200 },
    { "11025_stereo", 11025, 1, 512, 200 },
    { "11025_mono",   11025, 0, 512, 200 },
};
#define N_RATES ((int)(sizeof(rate_specs) / sizeof(rate_specs[0])))

#define N_INSTRUMENTS 6  /* Cave Story typical active instrument count */

/* Initialize 6 instruments with varied params so each does distinct work
 * (different wave-index, phaseinc, pan; matches the data variety a real
 * Org song would produce). */
static void init_instruments(instrument_t *ins, int freq_hz, int chunk_samples)
{
    for (int i = 0; i < N_INSTRUMENTS; i++) {
        ins[i].phaseacc = 0.0;
        /* phaseinc derived from notional freq (matches Organya.cpp line 229).
         * Choose notional freq = 220 + i*40 Hz to vary per instrument. */
        double notional_freq = 220.0 + i * 40.0;
        ins[i].phaseinc = notional_freq / (double)freq_hz;
        ins[i].wave_step = 256 / (8 + i);   /* matches wave_length_table[octave] */
        ins[i].cur_wave = &WaveTable[(i * 17) % NUM_WAVES * WAVE_LEN];
        ins[i].cur_wavesize = WAVE_LEN;
        ins[i].cur_length = chunk_samples * 10;
        ins[i].cur_vol = 0.5;
        ins[i].cur_pan = (i * 2) % 13;
    }
}

/* Sort uint32_t array ascending (insertion sort; fine for 200 samples). */
static void sort_u32(uint32_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        uint32_t key = a[i];
        int j = i;
        while (j > 0 && a[j-1] > key) { a[j] = a[j-1]; j--; }
        a[j] = key;
    }
}

/* Run one rate scenario. */
static int run_scenario(const rate_spec_t *R)
{
    plog("");
    plog("[orgsynth RATE=%s BEGIN]", R->rate_label);
    plog("  spec: freq_hz=%d channels=%d chunk_samples=%d n_chunks=%d n_instruments=%d",
         R->freq_hz, R->stereo ? 2 : 1, R->chunk_samples, R->n_chunks, N_INSTRUMENTS);
    plog("  org_source=embedded_synthetic_wavetable (100x256 triangle waves)");

    int chunk_samples = R->chunk_samples;
    if (chunk_samples > MAX_CHUNK_SAMPLES) chunk_samples = MAX_CHUNK_SAMPLES;

    /* Per-mono variant: synthesize the same instrument set but half the
     * effective chunk_samples (mono uses 1 channel of mix buffer instead of 2).
     * Practically: the per-sample math doesn't change, but the output rate
     * halves so chunks-per-sec halves too. We model this by halving
     * chunk_samples for mono — this approximates the "half output rate"
     * effect on the synth loop. */
    int effective_chunk = R->stereo ? chunk_samples : chunk_samples / 2;

    instrument_t ins[N_INSTRUMENTS];
    init_instruments(ins, R->freq_hz, effective_chunk);

    int n_chunks = R->n_chunks;
    uint32_t *samples = (uint32_t *)malloc(n_chunks * sizeof(uint32_t));
    if (!samples) {
        plog("[orgsynth RATE=%s DONE status=RATE_FAILED_INIT reason=alloc]", R->rate_label);
        return -1;
    }

    /* Warm up (1 chunk; skip caching artifacts). */
    synth_chunk(ins, N_INSTRUMENTS, effective_chunk);

    /* Measurement loop. */
    uint64_t cmin = (uint64_t)-1, cmax = 0, csum = 0;
    for (int c = 0; c < n_chunks; c++) {
        uint64_t t0 = rdtsc();
        synth_chunk(ins, N_INSTRUMENTS, effective_chunk);
        uint64_t t1 = rdtsc();
        uint64_t delta = t1 - t0;
        samples[c] = (uint32_t)(delta & 0xFFFFFFFFu);
        if (delta < cmin) cmin = delta;
        if (delta > cmax) cmax = delta;
        csum += delta;
    }

    /* Percentile calc. */
    sort_u32(samples, n_chunks);
    uint32_t cmed = samples[n_chunks / 2];
    uint32_t cp95 = samples[(n_chunks * 95) / 100];
    double mean_cycles = (double)csum / n_chunks;

    double us_min  = cycles_to_us(cmin);
    double us_med  = cycles_to_us(cmed);
    double us_p95  = cycles_to_us(cp95);
    double us_max  = cycles_to_us(cmax);
    double us_mean = cycles_to_us((uint64_t)mean_cycles);

    /* synth_cost_us_per_sec_output: each chunk produces chunk_samples
     * samples at freq_hz. So chunks_per_sec_output = freq_hz / chunk_samples.
     * Synth cost = us_med * chunks_per_sec_output. */
    double chunks_per_sec = (double)R->freq_hz / (double)effective_chunk;
    double synth_cost_per_sec = us_med * chunks_per_sec;

    plog("  chunks_synthesized=%d", n_chunks);
    plog("  rdtsc_cycles_per_chunk min=%llu med=%u p95=%u max=%llu mean=%llu",
         (unsigned long long)cmin, (unsigned)cmed, (unsigned)cp95,
         (unsigned long long)cmax, (unsigned long long)(uint64_t)mean_cycles);
    plog("  rdtsc_us_per_chunk min=%.2f med=%.2f p95=%.2f max=%.2f mean=%.2f",
         us_min, us_med, us_p95, us_max, us_mean);
    plog("  chunks_per_sec_output=%.2f", chunks_per_sec);
    plog("  synth_cost_us_per_sec_output=%.2f", synth_cost_per_sec);

    /* Status classification. */
    const char *status;
    if (cmin > 0 && cmax > cmin * 100) {
        status = "RATE_SUSPECT_SPIKE";
    } else {
        status = "RATE_PASS";
    }
    plog("[orgsynth RATE=%s DONE status=%s]", R->rate_label, status);

    free(samples);
    return 0;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== orgsynth (wave-38 audio Tier 2: Organya live-synth cost) ===");
    plog("DJGPP pure-C; ports Organya Song::Synth nearest-neighbour per-sample math");
    plog("from vendor/nxengine-evo/src/sound/Organya.cpp lines 282-328.");
    plog("Synthetic wavetable (100x256 triangle waves); 6 simulated instruments.");
    plog("Measures wave-39 P1/P2 candidate work-elimination (MIDI offload).");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");

    /* Step 1: RDTSC calibration. */
    plog("---- Step 1: RDTSC calibration ----");
    calibrate_rdtsc();
    plog("cpu_mhz_calibrated = %u  us_per_cycle = %.6f",
         (unsigned)g_cpu_mhz, g_us_per_cycle);
    if (g_us_per_cycle <= 0.0 || g_cpu_mhz < 30) {
        plog("FATAL: RDTSC calibration failed");
        plog("[orgsynth SUITE_DONE verdict=REFUTE_RDTSC_CALIBRATION_FAILED]");
        plog("[SENTINEL_END]");
        if (g_log) fclose(g_log);
        return 2;
    }
    plog("");

    /* Step 2: init wavetable. */
    plog("---- Step 2: init synthetic wavetable (%d waves x %d samples) ----",
         NUM_WAVES, WAVE_LEN);
    init_wavetable();
    plog("wavetable[0][0..7] = %d %d %d %d %d %d %d %d",
         WaveTable[0], WaveTable[1], WaveTable[2], WaveTable[3],
         WaveTable[4], WaveTable[5], WaveTable[6], WaveTable[7]);
    plog("wavetable[50][0..7] = %d %d %d %d %d %d %d %d",
         WaveTable[50 * WAVE_LEN + 0], WaveTable[50 * WAVE_LEN + 1],
         WaveTable[50 * WAVE_LEN + 2], WaveTable[50 * WAVE_LEN + 3],
         WaveTable[50 * WAVE_LEN + 4], WaveTable[50 * WAVE_LEN + 5],
         WaveTable[50 * WAVE_LEN + 6], WaveTable[50 * WAVE_LEN + 7]);
    plog("");

    /* Step 3: variant matrix. */
    plog("---- Step 3: run %d rate variants ----", N_RATES);
    plog("[orgsynth SUITE_BEGIN n=%d cpu_mhz=%u n_instruments=%d]",
         N_RATES, (unsigned)g_cpu_mhz, N_INSTRUMENTS);

    int pass = 0, fail_init = 0;
    for (int i = 0; i < N_RATES; i++) {
        int rc = run_scenario(&rate_specs[i]);
        if (rc < 0) fail_init++;
        else pass++;
    }

    plog("");
    plog("---- Step 4: SUITE_DONE ----");
    const char *suite_status =
        (fail_init == 0) ? "SUITE_PASS"
        : (pass >= 2)    ? "SUITE_DEGRADED"
        :                  "SUITE_FAILED";
    plog("[orgsynth SUITE_DONE n=%d pass=%d fail_init=%d status=%s]",
         N_RATES, pass, fail_init, suite_status);
    plog("");

    plog("Decomp guide (per wave-39 patch-actionability matrix):");
    plog("  ORGSYNTH share of audio_thread total:");
    plog("    share_pct = ORGSYNTH[22050_stereo].synth_cost_us_per_sec_output");
    plog("              / (wave-36 PLAY5 P12 audio_thread total per equivalent window) * 100");
    plog("  Verdict:");
    plog("    >50%% (Organya-dominated) -> P2 (WaveBlaster) rank-1; P1 (OPL3) reserved fallback.");
    plog("    25-50%% (balanced)        -> P7 (Lever G) rank-1 + P3 (SfxSynth pre-render) rank-2.");
    plog("    <25%% (Organya-minor)     -> P3 rank-1; P1/P2 refuted.");
    plog("  Rate-sensitivity check (P7 secondary): if ORGSYNTH[44100] - ORGSYNTH[11025m] is");
    plog("    small relative to 4x rate change, Lever G doesn't help the Organya bucket.");

    plog("");
    plog("=== orgsynth done ===");
    plog("[SENTINEL_END]");
    if (g_log) fclose(g_log);
    return (fail_init == 0) ? 0 : 1;
}
