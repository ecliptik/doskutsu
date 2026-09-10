/*
 * audbuf.c — SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES sweep — IRQ rate + cost
 * (Phase 11 wave-25 / iter J, post-sdl-engine-confirm scope shift).
 *
 * SCOPE NOTE: sdl-engine confirmed slot 0116 is DEFERRED in iter J. The
 * tree-facts (SoundManager.cpp:269-281 / slot 0066 / W13.5 real-HW)
 * showed 1024 + 2048 were ~+13 ms regressions vs 512 — the cache-
 * pressure-up effect dominates the IRQ-rate-down effect on P54C with
 * 8 KB L1. This probe now PRODUCES the data that picks iter K's
 * sample_frames value, rather than verifying a pre-made choice.
 *
 * Question (revised): across a 6-point sweep, where does the per-second
 * IRQ wall-clock cost minimize on g2k? The crossover between fewer IRQs
 * (bigger buffer = better) and L1 cache thrashing (bigger buffer = worse)
 * is the iter K target.
 *
 * Decision criteria (data-driven, per sdl-engine 2026-05-07):
 *   buffer-size argmin(irq_wall_us)  -> recommended sample_frames for iter K
 *   if argmin == 256/384             -> small-buffer regime (cache wins)
 *   if argmin == 512                 -> W13.5 anchor confirmed
 *   if argmin == 1024+               -> bigger-buffer regime (rate wins)
 *
 * ISR-branch caveat (sdl-engine 2026-05-07): the SB16 IRQ-5 ISR has two
 * paths in `SoundBlasterIRQHandler` (vendor/SDL/src/audio/dos/SDL_dosaudio_sb.c
 * :331-365):
 *   - avail >= chunk_size  -> RingCopyOut memcpy(ring -> DMA buffer)
 *   - else (underrun)      -> SDL_memset(silence -> DMA buffer)
 *
 * This probe opens an audio stream + resumes but never calls
 * SDL_PutAudioStreamData; the ring underruns continuously, so the ISR
 * exclusively hits the memset path. The real engine (SfxSynth+Organya
 * feeding the mixer on the audio thread) hits the memcpy path. memcpy
 * stresses cache more than memset (touches src cache lines as well as
 * dst), so the cache-pressure regime at 1024+ chunks would show stronger
 * regression in real engine ISR than in this probe's silence-fill ISR.
 *
 * Implication: probe wall_pct is a LOWER BOUND on engine wall_pct in the
 * cache-pollution regime. Relative ranking across chunk sizes (argmin) is
 * preserved because both regimes scale similarly under both ISR branches;
 * absolute wall_pct numbers should be interpreted as floors. Iter K
 * decomp should cross-reference probe argmin against in-engine measurement
 * to confirm the chosen sample_frames behaves the same with the mixer
 * memcpy path active.
 *
 * Per perf_predictions_unreliable.md: "smaller buffer = more IRQs" is
 * intuitive but the actual rate depends on the SDL backend's device-
 * frame-size negotiation (DOS backend may clamp to multiples of DMA-buffer
 * size, may round up to a fixed minimum, etc.). The probe MEASURES.
 *
 * Per dosbox_not_behavioral_proxy_for_io.md: DOSBox-X's emulated SB16 IRQ
 * rate is host-thread-driven and does NOT match real PODP83+real-DSP IRQ
 * rates. DOSBox-X smoke = correctness only (probe runs, all 4 sweeps emit
 * a row, no crash). Real-HW iter is the data gate.
 *
 * Probe scope (per team-lead brief, joint with sdl-engine):
 *
 *   Symbol contract from SDL/0037+0038:
 *     - extern volatile uint32_t dos_port_audio_irq_count
 *     - SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES is upstream-honored hint
 *
 *   Per-buffer-size sweep:
 *     1. SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "<N>") for
 *        N ∈ {default(unset), 512, 1024, 2048, 4096}
 *     2. SDL_Init(SDL_INIT_AUDIO)
 *     3. SDL_OpenAudioDeviceStream(default playback, 11025 mono S16)
 *     4. SDL_ResumeAudioStreamDevice
 *     5. SDL_Delay(200) — let backend stabilize (IRQ rate settles)
 *     6. snapshot dos_port_audio_irq_count, spin uclock 1.0 sec, snapshot
 *        again, compute delta = IRQs/sec
 *     7. SDL_DestroyAudioStream + SDL_Quit (so next sweep's SetHint takes)
 *
 * Output: AUDBUF.LOG with per-buffer-size IRQ rate + computed sample-rate
 * implied (irq_rate * frames-per-irq should approx 11025).
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/audbuf.c (6 chars host-side, fits 8.3)
 *   Binary:   AUDBUF.EXE  (6+3, fits)
 *   Log:      AUDBUF.LOG  (6+3, fits)
 *   BAT:      AUDBUF.BAT  (6+3, fits)
 *
 * License: MIT.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* SDL/0037+0038+0039 externs — incremented from the SB16 IRQ-5 ISR.
 * Volatile because IRQ handler updates them asynchronously; reads of
 * 32-bit aligned values are atomic on x86 (no cli/sti needed).
 *   - irq_count: monotonic count of IRQ-5 fires
 *   - irq_wall_us: cumulative IRQ-5 ISR wall-clock in µs (PIT/RDTSC)
 *   - sfx_active_count: increments only when ring had non-silence (sanity) */
extern volatile uint32_t dos_port_audio_irq_count;
extern volatile uint32_t dos_port_audio_irq_wall_us;
extern volatile uint32_t dos_port_audio_sfx_active_count;

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("AUDBUF.LOG", "w");
    if (!g_log) g_log = fopen("C:\\AUDBUF.LOG", "w");
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
        fsync(fileno(g_log));
    }
}

/* ============================================================ */
/* Timing                                                        */
/* ============================================================ */

static double now_secs(void)
{
    return (double)uclock() / (double)UCLOCKS_PER_SEC;
}

/* ============================================================ */
/* Silent audio callback — SDL fills with silence; backend ticks */
/* ============================================================ */

static void SDLCALL silent_callback(void *userdata, SDL_AudioStream *stream,
                                    int additional, int total)
{
    (void)userdata; (void)stream; (void)additional; (void)total;
    /* Intentionally empty — SDL fills silence; audio thread keeps ticking. */
}

/* ============================================================ */
/* Per-sweep result                                              */
/* ============================================================ */

typedef struct {
    int buffer_size;          /* hint value, -1 = unset (default) */
    int sdl_init_ok;
    int stream_open_ok;
    uint32_t irq_count_pre;
    uint32_t irq_count_post;
    uint32_t irq_us_pre;
    uint32_t irq_us_post;
    uint32_t sfx_pre;
    uint32_t sfx_post;
    double measure_secs;
    double irq_rate;          /* irq/sec */
    double irq_wall_pct;      /* fraction of measure_secs spent in IRQ-5 ISR */
    double frames_per_irq;    /* implied: 11025 / irq_rate */
} sweep_result_t;

/* ============================================================ */
/* One sweep iteration                                          */
/*                                                                */
/* HAZARD: we re-init SDL audio between sweeps. SDL_DestroyAudio */
/* + SDL_Quit must complete before the next SDL_SetHint, else   */
/* the new hint won't influence the next OpenAudioDeviceStream. */
/* ============================================================ */

static void run_sweep(int buffer_size, sweep_result_t *out)
{
    memset(out, 0, sizeof *out);
    out->buffer_size = buffer_size;

    /* Set hint BEFORE SDL_Init so backend reads it at open time. */
    char hint_str[16];
    if (buffer_size > 0) {
        snprintf(hint_str, sizeof hint_str, "%d", buffer_size);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, hint_str);
        plog("Sweep buffer=%d frames: SDL_SetHint set", buffer_size);
    } else {
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, NULL);
        plog("Sweep buffer=DEFAULT (hint unset)");
    }

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        plog("  SDL_Init failed: %s", SDL_GetError());
        return;
    }
    out->sdl_init_ok = 1;

    SDL_AudioSpec spec = { 0 };
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = 1;
    spec.freq     = 11025;

    SDL_AudioStream *stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                  &spec, silent_callback, NULL);
    if (!stream) {
        plog("  SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        SDL_Quit();
        return;
    }
    out->stream_open_ok = 1;
    SDL_ResumeAudioStreamDevice(stream);

    /* Stabilize — let backend settle into steady IRQ rate (the first few
     * IRQs may be stretched while DMA primes its buffer). */
    SDL_Delay(200);

    /* Measure delta over a 1-second window. */
    out->irq_count_pre = dos_port_audio_irq_count;
    out->irq_us_pre    = dos_port_audio_irq_wall_us;
    out->sfx_pre       = dos_port_audio_sfx_active_count;
    double t0 = now_secs();
    while ((now_secs() - t0) < 1.0) {
        /* Tight uclock spin — no SDL_Delay here because we want the
         * window length to match wall-clock as closely as possible. */
    }
    double t1 = now_secs();
    out->irq_count_post = dos_port_audio_irq_count;
    out->irq_us_post    = dos_port_audio_irq_wall_us;
    out->sfx_post       = dos_port_audio_sfx_active_count;
    out->measure_secs = t1 - t0;

    uint32_t irq_delta = out->irq_count_post - out->irq_count_pre;
    uint32_t us_delta  = out->irq_us_post    - out->irq_us_pre;
    uint32_t sfx_delta = out->sfx_post       - out->sfx_pre;
    out->irq_rate = (double)irq_delta / out->measure_secs;
    /* irq_wall_pct = (us spent in ISR) / (measurement window in us) */
    out->irq_wall_pct = (double)us_delta / (out->measure_secs * 1e6) * 100.0;
    out->frames_per_irq = (out->irq_rate > 0.0)
                            ? (11025.0 / out->irq_rate)
                            : 0.0;

    plog("  irq_count_delta=%lu  irq_us_delta=%lu  sfx_delta=%lu  over %.3f sec",
         (unsigned long)irq_delta, (unsigned long)us_delta,
         (unsigned long)sfx_delta, out->measure_secs);
    plog("  -> %.1f IRQ/sec  %.2f%% wall in ISR  (%.0f frames/IRQ implied)",
         out->irq_rate, out->irq_wall_pct, out->frames_per_irq);

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== AUDBUF wave-25 / iter J starting ===");
    plog("DJGPP + libSDL3 build");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");
    plog("Question: does SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES change the");
    plog("audio device's buffer size on the DOS backend? Answer drives");
    plog("slot 0116 verification.");
    plog("");
    plog("Symbol contract (SDL/0037+0038+0039):");
    plog("  extern volatile uint32_t dos_port_audio_irq_count       (IRQ-5 fires)");
    plog("  extern volatile uint32_t dos_port_audio_irq_wall_us     (cumulative ISR cost)");
    plog("  extern volatile uint32_t dos_port_audio_sfx_active_count (sanity-check)");
    plog("  SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES                     (upstream hint)");
    plog("");
    plog("ISR-branch caveat (sdl-engine 2026-05-07): probe runs the IRQ-5 ISR's");
    plog("silence-fill memset path (no SDL_PutAudioStreamData calls -> ring");
    plog("underruns -> ISR exclusively memsets DMA buffer with silence). The");
    plog("real engine runs the memcpy path (SfxSynth/Organya feed ring -> ISR");
    plog("RingCopyOut(ring -> DMA)). memcpy stresses cache more than memset;");
    plog("probe wall_pct is therefore a LOWER BOUND on engine ISR wall_pct in");
    plog("the cache-pollution regime. Argmin across chunk sizes is still the");
    plog("correct iter K signal; absolute wall_pct numbers are floors.");
    plog("");

    /* Sweep buffer sizes. -1 = leave hint unset (default). Expanded sweep
     * range per sdl-engine 2026-05-07: original 4-point sweep widened to
     * 7 points to map BOTH the cache-pressure curve (256/384) and the
     * IRQ-rate curve (1024+). 384 is non-power-of-2; SB16 DMA programming
     * accepts arbitrary byte counts but if OpenDevice fails on this value
     * the probe logs init=FAIL and continues to the next. */
    int buffer_sizes[] = { -1, 256, 384, 512, 1024, 2048, 4096 };
    int n_sweeps = sizeof(buffer_sizes) / sizeof(buffer_sizes[0]);

    sweep_result_t results[7];
    for (int i = 0; i < n_sweeps; i++) {
        plog("---- Sweep %d/%d ----", i + 1, n_sweeps);
        run_sweep(buffer_sizes[i], &results[i]);
        plog("");
    }

    /* ============================================================ */
    /* Tabular summary                                              */
    /* ============================================================ */
    plog("---- Summary table ----");
    plog("buffer_size  irq/sec   wall%%    frames/irq  status");
    plog("-----------  --------  -------  ----------  ----------");
    for (int i = 0; i < n_sweeps; i++) {
        const sweep_result_t *r = &results[i];
        char bs_str[16];
        if (r->buffer_size > 0) snprintf(bs_str, sizeof bs_str, "%d", r->buffer_size);
        else                    snprintf(bs_str, sizeof bs_str, "(default)");
        plog("%-11s  %8.1f  %6.2f%%  %10.0f  %s%s",
             bs_str, r->irq_rate, r->irq_wall_pct, r->frames_per_irq,
             r->sdl_init_ok ? "init=OK"     : "init=FAIL",
             r->stream_open_ok ? " open=OK" : " open=FAIL");
    }
    plog("");

    /* ============================================================ */
    /* Verdict — data-driven argmin(irq_wall_pct) per sdl-engine    */
    /* 2026-05-07 scope shift. The buffer size that minimizes total */
    /* IRQ-5 wall-clock cost is the iter K recommendation.          */
    /* ============================================================ */
    plog("---- Verdict ----");

    /* Find argmin(irq_wall_pct) among sweeps that had non-zero IRQ activity. */
    int argmin = -1;
    double min_wall_pct = 1e9;
    int captured = 0;
    for (int i = 0; i < n_sweeps; i++) {
        if (!results[i].stream_open_ok) continue;
        if (results[i].irq_rate <= 0.0) continue;
        captured++;
        if (results[i].irq_wall_pct < min_wall_pct) {
            min_wall_pct = results[i].irq_wall_pct;
            argmin = i;
        }
    }

    if (captured == 0) {
        plog("INCOMPLETE: no sweep captured non-zero IRQ activity.");
        plog("Cannot identify cost-minimizing buffer size. Possible causes:");
        plog("  - SDL/0038 IRQ-counter hook not wired in this libSDL3.a");
        plog("  - SB16 hardware IRQ-5 not actually firing (DOSBox-X correctness");
        plog("    expected; on real HW investigate IRQ-5 vector + DSP DMA setup)");
        plog("  - audio device using a non-IRQ-driven path (unexpected)");
        plog("=== HEADLINE: NO DATA — RUN ON REAL HW FOR DEFINITIVE ANSWER ===");
    } else if (argmin >= 0) {
        char bs_str[16];
        if (results[argmin].buffer_size > 0) {
            snprintf(bs_str, sizeof bs_str, "%d", results[argmin].buffer_size);
        } else {
            snprintf(bs_str, sizeof bs_str, "(default=512)");
        }
        plog("argmin(irq_wall_pct) = buffer_size %s at %.2f%% wall in ISR",
             bs_str, min_wall_pct);
        plog("");
        plog("Cross-anchor: SoundManager.cpp:269-281 / slot 0066 / W13.5 measured");
        plog("the engine real-HW config of 512 frames as the post-regression");
        plog("anchor. argmin == 512 confirms W13.5; argmin == 256/384 says smaller");
        plog("is even better (cache-pressure regime); argmin >= 1024 says rate-down");
        plog("dominates cache-pressure-up on this hardware.");
        plog("");
        plog("=== HEADLINE: iter K should set sample_frames = %s ===", bs_str);
    }

    plog("");
    plog("Per-buffer-size detail interpretation:");
    plog("  irq_rate  = IRQ-5 fires per second (lower = bigger buffer = fewer DMA refills)");
    plog("  wall%%    = fraction of wall-clock spent inside the IRQ-5 ISR");
    plog("              (this is the COST signal — lower = more time for engine)");
    plog("              REMINDER: probe runs ISR memset path; engine runs memcpy");
    plog("              path. wall_pct is a LOWER BOUND on engine ISR cost in the");
    plog("              cache-pollution regime (1024+ chunks). Argmin still valid.");
    plog("  frames/irq = implied actual buffer size SDL chose (should match request");
    plog("              if backend honors the hint; if constant across requests,");
    plog("              backend is using a fixed buffer regardless of hint)");
    plog("");
    plog("=== AUDBUF done ===");

    if (g_log) fclose(g_log);
    return 0;
}
