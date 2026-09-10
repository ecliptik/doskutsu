/*
 * pixprobe.c — SfxSynth SFX mix cost in isolation (Phase 11 wave-22.5 / iter H).
 *
 * Question: what's the per-flip CPU cost of SfxSynth SFX synthesis on PODP83?
 * User is considering alternate-flip mixing (slot 0114) — synthesize SfxSynth
 * every other flip instead of every flip — to claw back fps. Trade-off: SFX
 * latency goes from ~17 ms (60 fps target) to ~33 ms. Need a measured cost
 * to know if the trade is worth it.
 *
 * Decision criteria (per team-lead brief):
 *   per-flip SfxSynth cost < 3 ms   -> alternate-flip unnecessary
 *   per-flip SfxSynth cost 6-8 ms   -> proceed with alternate-flip (matches estimate)
 *   per-flip SfxSynth cost 10+ ms   -> alternate-flip critical; consider every-3-flip
 *
 * Per perf_predictions_unreliable.md: theoretical "32 multiplies/sample at
 * 83 MHz" math has been 5-30x wrong on PODP83+DJGPP+DPMI. The probe MEASURES
 * the actual per-call us; the analysis interprets it. No prose-side
 * cycle-count math embedded in output.
 *
 * Per dosbox_not_perf_proxy.md: DOSBox-X numbers from this probe will NOT
 * match real HW. RDTSC under cycles=fixed gives scaled cycle counts;
 * floating-point throughput differs between DOSBox-X's host-FPU forwarding
 * and real PODP83's on-die FPU pipeline. Smoke validates probe correctness;
 * real HW iter is the data gate.
 *
 * Probe scope (per team-lead brief):
 *
 *   1. Initialize a SfxSynth-equivalent synth structure that hits the same
 *      per-sample instruction mix as engine's stPXChannel::synth() (3
 *      wave-table indexed reads, ~5 integer multiplies, 1 integer divide,
 *      1 fp-mul-add for FM update, plus envelope.evaluate which has 3
 *      branches + linear interp per sample).
 *   2. Loop: synthesize M samples per call, K times per scenario.
 *   3. Buffer sizes M: 256 (Tier-2 default), 512, 1024.
 *   4. Project per-flip cost at K=1, K=4 (typical), K=8 (heavy combat).
 *   5. RDTSC timing on P54C+ (PODP83 supports it).
 *   6. N=100 reps per scenario, emit min/med/p95/max.
 *
 * The SfxSynth-equivalent in this probe is faithfully cribbed from
 * vendor/nxengine-evo/src/sound/Pixtone.cpp::stPXChannel::synth() (Cave
 * Story 2004 spec) — same instruction mix, same per-sample math. NOT
 * functional SfxSynth (no real waveform output, no sound played); this
 * probe only times the cost.
 *
 * Output: PIXPROB.LOG in cwd. Stdout mirrored. Pure DJGPP, no SDL.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/pixprobe.c (8 chars host-side; technically fits
 *             8.3 directly, but explicit Makefile rule renames to pixprob
 *             to match team-lead brief's PIXPROB.EXE convention)
 *   Binary:   PIXPROB.EXE  (7+3, fits)
 *   Log:      PIXPROB.LOG
 *   BAT:      PIXPROB.BAT
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
    g_log = fopen("PIXPROB.LOG", "w");
    if (!g_log) g_log = fopen("C:\\PIXPROB.LOG", "w");
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
/* RDTSC                                                         */
/* ============================================================ */

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* ============================================================ */
/* SfxSynth-equivalent channel structure                          */
/*                                                               */
/* Faithfully mirrors vendor/nxengine-evo/src/sound/Pixtone.cpp  */
/* stPXChannel layout — same fields, same types, so the per-     */
/* sample synth loop hits the same instruction mix as the engine.*/
/* ============================================================ */

typedef struct {
    /* Three signal generators: carrier (main), frequency-mod, amplitude-mod.
     * Each has a 256-byte int8_t wavetable + level + pitch + offset. */
    int8_t wave_carr[256];
    int8_t wave_freq[256];
    int8_t wave_amp[256];
    int    level_carr, level_freq, level_amp;
    double pitch_carr, pitch_freq, pitch_amp;
    double offset_carr, offset_freq, offset_amp;
    /* Envelope: initial value + 3 (time, value) keypoints. */
    int env_initial;
    int env_t[3];
    int env_p[3];
} pix_channel_t;

/* ============================================================ */
/* SfxSynth envelope — verbatim port of stPXEnvelope::evaluate    */
/* ============================================================ */

static int pix_envelope(const pix_channel_t *c, int i)
{
    int prevval = c->env_initial, prevtime = 0;
    int nextval = 0, nexttime = 256;
    for (int j = 2; j >= 0; --j) {
        if (i < c->env_t[j]) {
            nexttime = c->env_t[j];
            nextval  = c->env_p[j];
        }
    }
    for (int j = 0; j <= 2; ++j) {
        if (i >= c->env_t[j]) {
            prevtime = c->env_t[j];
            prevval  = c->env_p[j];
        }
    }
    if (nexttime <= prevtime) return prevval;
    return (i - prevtime) * (nextval - prevval) / (nexttime - prevtime) + prevval;
}

/* ============================================================ */
/* SfxSynth synth — verbatim port of stPXChannel::synth          */
/*                                                               */
/* This is the load-bearing routine. Per-sample work:            */
/*   - 3 wavetable indexed reads (load + sign-extend)            */
/*   - 5 integer multiplies                                      */
/*   - 1 integer divide (in pix_envelope linear interp)          */
/*   - 1 fp-mul-add for mainpos delta                            */
/*   - 1 fp-div in mainpos delta (8192. or 2048. divisor)        */
/*   - envelope.evaluate per sample: 3+3 branches, 1 mul, 1 div  */
/* ============================================================ */

static __attribute__((noinline))
void pix_synth(const pix_channel_t *c, int8_t *out, int nsamples)
{
    double mainpos = c->offset_carr;
    double maindelta = 256.0 * c->pitch_carr / nsamples;
    for (int i = 0; i < nsamples; ++i) {
        double s = 256.0 * (double)i / (double)nsamples;
        int freqval = c->wave_freq[0xFF & (int)(c->offset_freq + s * c->pitch_freq)] * c->level_freq;
        int ampval  = c->wave_amp[0xFF & (int)(c->offset_amp + s * c->pitch_amp)] * c->level_amp;
        int mainval = c->wave_carr[0xFF & (int)mainpos] * c->level_carr;
        /* Apply amplitude & envelope to the main signal level. */
        out[i] = (int8_t)(mainval * (ampval + 4096) / 4096 * pix_envelope(c, (int)s) / 4096);
        /* Apply frequency modulation to mainpos. */
        mainpos += maindelta * (1.0 + ((double)freqval / (freqval < 0 ? 8192.0 : 2048.0)));
    }
}

/* ============================================================ */
/* Volatile sink — keeps -O2 from eliding the synth output       */
/* ============================================================ */

static volatile int32_t g_sink = 0;

/* ============================================================ */
/* Setup: one realistic SfxSynth channel                          */
/*                                                               */
/* Real Cave Story SFX use varied wavetable patterns. We seed a  */
/* mix of sine / triangle / sawtooth-like patterns that exercise */
/* the full data range so the int multiplies don't degenerate    */
/* (e.g., a wavetable of all zeros would optimize to a no-op).   */
/* ============================================================ */

static void init_channel(pix_channel_t *c)
{
    /* Carrier: sine wave, 256 entries, range [-64, 64] (matches engine
     * MOD_SINE table from Pixtone.cpp:297). */
    for (int i = 0; i < 256; i++) {
        c->wave_carr[i] = (int8_t)(0x40 * sin(i * 3.1416 / 0x80));
    }
    /* Frequency modulator: triangle wave (engine MOD_TRI form). */
    for (int i = 0; i < 256; i++) {
        c->wave_freq[i] = (int8_t)(((0x40 + i) & 0x80) ? (0x80 - i) : i);
    }
    /* Amplitude modulator: sawtooth-like ramp, range [-64, 64]. */
    for (int i = 0; i < 256; i++) {
        c->wave_amp[i] = (int8_t)((i & 0x7F) - 64);
    }

    c->level_carr = 32;  c->pitch_carr = 1.0;  c->offset_carr = 0.0;
    c->level_freq = 16;  c->pitch_freq = 0.5;  c->offset_freq = 0.0;
    c->level_amp  = 24;  c->pitch_amp  = 1.5;  c->offset_amp  = 0.0;

    /* Envelope: ADSR-like. Initial=0, ramps up to 3F at t=64, holds at 30
     * until t=192, then ramps to 0 at t=255. Mirrors realistic Cave Story
     * SFX envelope shapes. */
    c->env_initial = 0;
    c->env_t[0] = 64;   c->env_p[0] = 0x3F;
    c->env_t[1] = 192;  c->env_p[1] = 0x30;
    c->env_t[2] = 255;  c->env_p[2] = 0x00;
}

/* ============================================================ */
/* Statistics                                                    */
/* ============================================================ */

static int u64_cmp(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a, vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

#define N_REPS 100

typedef struct {
    uint64_t min, p95, max;
    double median, mean;
} stats_t;

static void compute_stats(uint64_t *samples, int n, stats_t *out)
{
    qsort(samples, n, sizeof samples[0], u64_cmp);
    out->min = samples[0];
    out->max = samples[n - 1];
    out->median = (double)samples[n / 2];
    out->p95 = samples[(int)(0.95 * n)];

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += (double)samples[i];
    out->mean = sum / n;
}

/* ============================================================ */
/* Run one scenario: K calls of synth(M samples) per rep, N reps */
/* ============================================================ */

static void run_scenario(const pix_channel_t *c, int8_t *outbuf,
                         int M, int K, double mhz)
{
    static uint64_t samples[N_REPS];

    /* Warm-up. */
    for (int k = 0; k < K; k++) {
        pix_synth(c, outbuf, M);
        g_sink += outbuf[M / 2];  /* mid-buffer; envelope non-zero there */
    }

    for (int s = 0; s < N_REPS; s++) {
        uint64_t t0 = rdtsc();
        for (int k = 0; k < K; k++) {
            pix_synth(c, outbuf, M);
            /* XOR last-sample of each call into sink — keeps optimizer
             * honest without dominating the timed loop. */
            g_sink += outbuf[M / 2];  /* mid-buffer; envelope non-zero there */
        }
        uint64_t t1 = rdtsc();
        samples[s] = t1 - t0;
    }

    stats_t st;
    compute_stats(samples, N_REPS, &st);

    /* Per-call us = (median cycles / K) / mhz */
    double per_call_cycles = st.median / (double)K;
    double per_call_us = per_call_cycles / mhz;
    /* Per-rep ms (full K-call batch) */
    double per_rep_ms = st.median / (mhz * 1000.0);

    plog("[scenario M=%-4d K=%-2d] N=%d  cycles min=%9llu med=%9.0f p95=%9llu max=%9llu  per-call=%6.1f us  total=%5.2f ms",
         M, K, N_REPS,
         (unsigned long long)st.min, st.median,
         (unsigned long long)st.p95, (unsigned long long)st.max,
         per_call_us, per_rep_ms);
}

/* ============================================================ */
/* CPU MHz calibration (same as TILEPROBE)                       */
/* ============================================================ */

static double calibrate_mhz(void)
{
    double t_start_uc = (double)uclock() / (double)UCLOCKS_PER_SEC;
    uint64_t t_start_tsc = rdtsc();
    while (((double)uclock() / (double)UCLOCKS_PER_SEC) - t_start_uc < 0.1) {
        /* spin */
    }
    uint64_t t_end_tsc = rdtsc();
    double t_end_uc = (double)uclock() / (double)UCLOCKS_PER_SEC;
    return (double)(t_end_tsc - t_start_tsc) / ((t_end_uc - t_start_uc) * 1e6);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== PIXPROB wave-22.5 / iter H starting ===");
    plog("DJGPP build, target = SfxSynth synth cost characterization");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");
    plog("Question: what's the per-flip CPU cost of SfxSynth SFX synth on");
    plog("PODP83? Answer drives slot 0114 (alternate-flip mix) ship/no-ship.");
    plog("");

    /* MHz calibration. */
    plog("Calibrating CPU MHz (~100 ms spin)...");
    double mhz = calibrate_mhz();
    plog("CPU clock estimate: %.3f MHz (PODP83 nominal = 83.0; DOSBox-X varies)",
         mhz);
    plog("");

    /* Initialize one channel + output buffer. */
    pix_channel_t ch;
    init_channel(&ch);

    /* Allocate max-sized output buffer (1024 samples × int8 = 1KB). */
    int8_t *outbuf = malloc(1024);
    if (!outbuf) {
        plog("FATAL: malloc(1024) failed");
        return 2;
    }

    plog("SfxSynth channel initialized:");
    plog("  carrier wavetable:   sine,     level=%d pitch=%.2f", ch.level_carr, ch.pitch_carr);
    plog("  frequency modulator: triangle, level=%d pitch=%.2f", ch.level_freq, ch.pitch_freq);
    plog("  amplitude modulator: sawtooth, level=%d pitch=%.2f", ch.level_amp, ch.pitch_amp);
    plog("  envelope keypoints:  initial=%d (t=%d,v=%d) (t=%d,v=%d) (t=%d,v=%d)",
         ch.env_initial,
         ch.env_t[0], ch.env_p[0],
         ch.env_t[1], ch.env_p[1],
         ch.env_t[2], ch.env_p[2]);
    plog("");

    /* ============================================================ */
    /* Run scenarios at three buffer sizes × three K values         */
    /*                                                              */
    /* M = 256 (Tier-2 default), 512, 1024 (Tier-1 default)         */
    /* K = 1 (low SFX scene), 4 (typical), 8 (combat heavy)         */
    /* ============================================================ */
    plog("---- 9-scenario sweep: 3 buffer sizes × 3 K values, N=%d each ----", N_REPS);
    plog("Buffer size M = samples per SfxSynth call");
    plog("K            = number of concurrent SFX calls per flip");
    plog("");

    int Ms[] = { 256, 512, 1024 };
    int Ks[] = { 1, 4, 8 };

    for (int mi = 0; mi < 3; mi++) {
        for (int ki = 0; ki < 3; ki++) {
            run_scenario(&ch, outbuf, Ms[mi], Ks[ki], mhz);
        }
    }
    plog("");

    /* ============================================================ */
    /* Headline pair: M=256 (Tier-2 buffer) at K=1 + K=4            */
    /* This is the most likely realistic per-flip SfxSynth load.      */
    /* ============================================================ */
    plog("---- Headline pair: M=256 (Tier-2 buffer) ----");
    static uint64_t samples_K1[N_REPS], samples_K4[N_REPS];
    /* Warm-up. */
    pix_synth(&ch, outbuf, 256); g_sink += outbuf[128];  /* mid-buffer; envelope non-zero there */

    for (int s = 0; s < N_REPS; s++) {
        uint64_t t0 = rdtsc();
        pix_synth(&ch, outbuf, 256);
        g_sink += outbuf[128];  /* mid-buffer; envelope non-zero there */
        uint64_t t1 = rdtsc();
        samples_K1[s] = t1 - t0;

        uint64_t t2 = rdtsc();
        for (int k = 0; k < 4; k++) {
            pix_synth(&ch, outbuf, 256);
            g_sink += outbuf[128];  /* mid-buffer; envelope non-zero there */
        }
        uint64_t t3 = rdtsc();
        samples_K4[s] = t3 - t2;
    }

    qsort(samples_K1, N_REPS, sizeof samples_K1[0], u64_cmp);
    qsort(samples_K4, N_REPS, sizeof samples_K4[0], u64_cmp);
    double med_K1 = (double)samples_K1[N_REPS / 2];
    double med_K4 = (double)samples_K4[N_REPS / 2];
    double per_call_us = med_K1 / mhz;
    double per_flip_K1_ms = med_K1 / (mhz * 1000.0);
    double per_flip_K4_ms = med_K4 / (mhz * 1000.0);
    double per_flip_K8_est_ms = per_flip_K4_ms * 2.0;  /* linear projection */

    plog("PAIRED M=256 K=1 (1 SFX/flip):  med=%9.0f cycles  per-call=%6.1f us  per-flip=%5.2f ms",
         med_K1, per_call_us, per_flip_K1_ms);
    plog("PAIRED M=256 K=4 (4 SFX/flip):  med=%9.0f cycles                       per-flip=%5.2f ms",
         med_K4, per_flip_K4_ms);
    plog("EST    M=256 K=8 (heavy combat): linear projection                     per-flip=%5.2f ms",
         per_flip_K8_est_ms);
    plog("");

    /* ============================================================ */
    /* Decision interpretation                                      */
    /* ============================================================ */
    plog("=== HEADLINE: per-flip SfxSynth cost = %.2f ms (K=4 typical) ===",
         per_flip_K4_ms);
    plog("");

    plog("Decision criteria (per team-lead brief, applied to K=4 typical):");
    plog("  < 3.0 ms / flip   -> alternate-flip unnecessary (savings < 1.5 ms)");
    plog("  3.0-6.0 ms / flip -> marginal; consider after Levers exhausted");
    plog("  6.0-10.0 ms / flip -> ship alternate-flip (savings 3-5 ms)");
    plog("  10.0+ ms / flip   -> alternate-flip critical; consider every-3-flip");
    plog("");

    if (per_flip_K4_ms < 3.0) {
        plog("Result: %.2f ms < 3.0 -> RECOMMEND DROP slot 0114 alternate-flip",
             per_flip_K4_ms);
    } else if (per_flip_K4_ms < 6.0) {
        plog("Result: %.2f ms in [3.0, 6.0) -> RECOMMEND DEFER slot 0114",
             per_flip_K4_ms);
    } else if (per_flip_K4_ms < 10.0) {
        plog("Result: %.2f ms in [6.0, 10.0) -> RECOMMEND SHIP slot 0114",
             per_flip_K4_ms);
    } else {
        plog("Result: %.2f ms >= 10.0 -> RECOMMEND SHIP slot 0114 + every-3-flip",
             per_flip_K4_ms);
    }
    plog("");

    /* ============================================================ */
    /* Cross-checks                                                 */
    /* ============================================================ */
    plog("Cross-check: per-call us should scale roughly linearly with M.");
    plog("  M=256  per-call (above) = %.1f us", per_call_us);
    plog("  Expected: M=512 ~ 2x, M=1024 ~ 4x (linear in nsamples).");
    plog("  If non-linear, FPU pipeline stalls or cache effects are at play.");
    plog("");
    plog("Cross-check: PODP83 nominal MHz = 83.0; calibrated = %.1f.", mhz);
    plog("  If they agree within 5%%, cycle->ms conversion is trustworthy.");
    plog("  DOSBox-X under cycles=fixed reports scaled values; smoke is");
    plog("  correctness-only (per dosbox_not_perf_proxy.md).");
    plog("");
    plog("g_sink final = 0x%08lX (mid-buffer-sample accumulator; non-zero",
         (unsigned long)(uint32_t)g_sink);
    plog("                 confirms the synth loop wrote real data the optimizer");
    plog("                 could not elide; per-call us numbers are trustworthy)");

    free(outbuf);

    plog("");
    plog("=== PIXPROB done ===");

    if (g_log) fclose(g_log);
    return 0;
}
