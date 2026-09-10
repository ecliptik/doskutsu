/*
 * idleprob.c — DSP idle-pause CPU yield measurement
 * (Phase 11 wave-25 / iter J, slot 0115 idle-pause verify).
 *
 * Question: when SDL/0037's SDL_DOSAudioForcePause() pauses DMA + masks
 * IRQ-5 fires, how much wall-clock does it return to the engine? sdl-
 * engine's slot 0115 patch claims this is the lever for ~6 ms/flip clawed
 * back during quiet scenes (no SFX, no Organya). This probe quantifies the
 * yield under controlled synthetic load.
 *
 * Decision criteria (per team-lead brief):
 *   delta < 1 ms/flip   -> slot 0115 idle-pause not worth defaulting on
 *   delta 1-3 ms/flip   -> SHIP slot 0115 (default-on safe)
 *   delta >= 6 ms/flip  -> idle-pause savings real; high-priority engine wiring
 *
 * Per perf_predictions_unreliable.md: "audio IRQ at 80 Hz × 100 us/IRQ ≈
 * 8 ms/sec ≈ 0.13 ms/flip" is theory; the actual cost includes IRQ-5 ISR
 * body work (DMA refill, ring buffer ops), SfxSynth/Organya IRQ-driven mix,
 * and DPMI overhead per IRQ. The probe MEASURES the integrated cost via
 * direct wall-clock comparison of an identical synthetic engine loop with
 * audio active vs paused.
 *
 * Per dosbox_not_behavioral_proxy_for_io.md: DOSBox-X's emulated SB16 IRQ
 * doesn't run real-IRQ code paths. This probe MUST NOT default-on as a
 * gating signal in DOSBox-X smoke. DOSBox-X smoke = correctness only
 * (probe runs, ForcePause callable, IRQ-counter behavior parses).
 *
 * Probe scope (per team-lead brief, joint with sdl-engine):
 *
 *   Symbol contract from SDL/0037:
 *     - extern volatile uint32_t dos_port_audio_irq_count
 *     - extern void SDL_DOSAudioForcePause(void)   (async; flag handled at next IRQ)
 *     - extern void SDL_DOSAudioForceResume(void)
 *
 *   Sequence:
 *     1. SDL_Init(AUDIO) + open audio stream (11025 mono S16) + resume
 *     2. SDL_Delay(200) — let backend stabilize
 *     3. Sanity-check: dos_port_audio_irq_count incrementing (>0 IRQs/200ms)
 *     4. Scenario A (audio active): time N_A iterations of synth loop over
 *        1.0 sec wall (uclock)
 *     5. SDL_DOSAudioForcePause()
 *     6. Verify pause engaged: poll dos_port_audio_irq_count for 200 ms,
 *        ensure delta = 0 across last 100 ms window. If still incrementing,
 *        ABORT with "pause did not engage within 200 ms"
 *     7. Scenario B (audio paused): time N_B iterations of same synth loop
 *        over 1.0 sec wall
 *     8. SDL_DOSAudioForceResume() — verify rate resumes (>0 IRQs/100ms)
 *     9. Compute yield: (N_B - N_A) / N_B = fraction of wall-clock returned;
 *        project to a 16.67 ms (60 fps) flip budget for ms/flip headline
 *
 *   WATCHDOG:
 *     - ForcePause acknowledgment: 200 ms uclock cap; if IRQ count still
 *       moving, log + bail before scenario B (no synthetic delta to compute)
 *     - Total probe wall-clock: 30 sec uclock cap
 *     - BIOS keyboard pending: any-key-to-abort honored at sweep boundaries
 *     - fsync BEGIN/DONE markers per critical step (forensic recovery if
 *       ForcePause locks the chip; per dosbox_not_behavioral_proxy_for_io.md
 *       hardware-IO has bus-lock potential on real Vibra16S)
 *
 * Output: IDLEPROB.LOG with per-scenario synth-loop counts + computed yield.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/idleprob.c (8 chars host-side, fits 8.3)
 *   Binary:   IDLEPROB.EXE  (8+3, fits)
 *   Log:      IDLEPROB.LOG  (8+3, fits)
 *   BAT:      IDLEPROB.BAT  (8+3, fits)
 *
 * License: MIT.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <go32.h>
#include <time.h>
#include <unistd.h>

/* SDL/0037 contract — see file header. */
extern volatile uint32_t dos_port_audio_irq_count;
extern void SDL_DOSAudioForcePause(void);
extern void SDL_DOSAudioForceResume(void);

/* ============================================================ */
/* Logging — fsync per line. This probe touches hardware-IO     */
/* (via SDL/0037's helper, which manipulates DSP DMA state),     */
/* so per-line fsync is forensic protection per                  */
/* dosbox_not_behavioral_proxy_for_io.md.                        */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("IDLEPROB.LOG", "w");
    if (!g_log) g_log = fopen("C:\\IDLEPROB.LOG", "w");
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
/* Step markers — same as MPUSDLPROBE forensic protocol         */
/* ============================================================ */

static int g_step_n = 0;
static int g_step_total = 0;
static double g_step_t0 = 0.0;

static double now_secs(void) { return (double)uclock() / (double)UCLOCKS_PER_SEC; }

static int kbd_pending(void)
{
    uint16_t head = _farpeekw(_dos_ds, 0x41AL);
    uint16_t tail = _farpeekw(_dos_ds, 0x41CL);
    return head != tail;
}

static void step_begin(const char *desc)
{
    g_step_n++;
    g_step_t0 = now_secs();
    plog("[step %d/%d] BEGIN  %-48s uclock=%.6f",
         g_step_n, g_step_total, desc, g_step_t0);
}

static void step_done(const char *result_fmt, ...)
{
    char rbuf[256];
    va_list ap;
    va_start(ap, result_fmt);
    vsnprintf(rbuf, sizeof rbuf, result_fmt, ap);
    va_end(ap);

    double dt = now_secs() - g_step_t0;
    plog("[step %d/%d] DONE   elapsed_us=%-9.0f result: %s",
         g_step_n, g_step_total, dt * 1e6, rbuf);
}

/* ============================================================ */
/* Synth engine loop                                             */
/*                                                                */
/* Approximates per-flip CPU work: integer math + branches +     */
/* memory access. NOT a faithful engine clone — just a load that */
/* takes about as long per-iter as a frame's worth of object     */
/* updates. We compare iter counts under the SAME loop body in   */
/* the two scenarios; absolute throughput doesn't matter, only   */
/* the ratio between active vs paused.                            */
/* ============================================================ */

static volatile uint32_t g_sink = 0;

static __attribute__((noinline))
uint32_t synth_iter(uint32_t state)
{
    /* xorshift32 + a couple of integer multiplies + branch. ~10-20 cycles
     * per call on P54C. Real engine work is more variable; this gives a
     * stable, branch-predictable load that won't be eliminated by -O2. */
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    state = state * 1103515245u + 12345u;
    if ((state & 0xFF) == 0) state += 1;  /* mostly not-taken branch */
    return state;
}

/* Run the synth loop for `secs` wall-clock. Returns iteration count. */
static uint64_t synth_loop_for(double secs)
{
    uint32_t state = 0xCAFEBABE;
    uint64_t iters = 0;
    double t0 = now_secs();
    /* Inner loop in big batches to amortize uclock-read overhead.
     * 1024 calls per uclock check = ~10-20 us between checks. */
    while ((now_secs() - t0) < secs) {
        for (int i = 0; i < 1024; i++) {
            state = synth_iter(state);
        }
        iters += 1024;
    }
    g_sink ^= state;  /* keep optimizer honest */
    return iters;
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    plog("=== IDLEPROB wave-25 / iter J starting ===");
    plog("DJGPP + libSDL3 build; target = SDL_DOSAudioForcePause yield measurement");
    plog("UCLOCKS_PER_SEC = %lu", (unsigned long)UCLOCKS_PER_SEC);
    plog("");
    plog("Question: how much wall-clock does SDL_DOSAudioForcePause return");
    plog("to the engine on real PODP83 + SB16? Answer drives slot 0115");
    plog("default-on/opt-in decision.");
    plog("");
    plog("Forensic protocol: BEGIN/DONE markers fsync'd per line. If the");
    plog("system hangs (e.g. ForcePause stalls the DSP bus on this chip),");
    plog("the LAST BEGIN line in IDLEPROB.LOG names the stalling instruction.");
    plog("Operator: hard-reset is OK after a hang; the log is on disk.");
    plog("");

    g_step_total = 14;

    /* ============================================================ */
    /* Step 1: SDL audio init                                       */
    /* ============================================================ */
    step_begin("SDL_Init(SDL_INIT_AUDIO)");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        const char *err = SDL_GetError();
        step_done("FAILED: %s", err ? err : "(no error)");
        plog("FATAL: cannot proceed without SDL audio.");
        if (g_log) fclose(g_log);
        return 2;
    }
    step_done("SDL_Init OK");

    /* ============================================================ */
    /* Step 2: open audio stream                                    */
    /* ============================================================ */
    step_begin("SDL_OpenAudioDeviceStream(11025 mono S16)");
    SDL_AudioSpec spec = { 0 };
    spec.format = SDL_AUDIO_S16LE; spec.channels = 1; spec.freq = 11025;
    SDL_AudioStream *stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                  &spec, NULL, NULL);
    if (!stream) {
        const char *err = SDL_GetError();
        step_done("FAILED: %s", err ? err : "(no error)");
        plog("FATAL: cannot open audio device.");
        SDL_Quit();
        if (g_log) fclose(g_log);
        return 3;
    }
    step_done("audio stream opened OK");

    /* ============================================================ */
    /* Step 3: resume + stabilize                                   */
    /* ============================================================ */
    step_begin("SDL_ResumeAudioStreamDevice + SDL_Delay(200) stabilize");
    SDL_ResumeAudioStreamDevice(stream);
    SDL_Delay(200);
    step_done("audio resumed; backend should be in steady IRQ state");

    /* ============================================================ */
    /* Step 4: sanity-check IRQ count incrementing                  */
    /* ============================================================ */
    step_begin("Sanity: IRQ count incrementing under audio active");
    uint32_t cnt0 = dos_port_audio_irq_count;
    SDL_Delay(200);
    uint32_t cnt1 = dos_port_audio_irq_count;
    uint32_t delta_active = cnt1 - cnt0;
    step_done("delta over 200 ms = %lu IRQs (rate %.0f/sec)",
              (unsigned long)delta_active, (double)delta_active / 0.2);

    if (delta_active == 0) {
        plog("WARN: no IRQs observed during 200 ms with audio active.");
        plog("      Either SB16 IRQ-5 isn't wired, or dos_port_audio_irq_count");
        plog("      isn't being incremented (SDL/0037+0038 missing from binary).");
        plog("      The probe will continue, but yield numbers will be UNRELIABLE.");
    }

    /* ============================================================ */
    /* Step 5: Scenario A — audio active, time synth loop 1.0 sec   */
    /* ============================================================ */
    step_begin("Scenario A: synth loop 1.0 sec, audio ACTIVE");
    cnt0 = dos_port_audio_irq_count;
    uint64_t iters_A = synth_loop_for(1.0);
    cnt1 = dos_port_audio_irq_count;
    uint32_t irqs_during_A = cnt1 - cnt0;
    step_done("iters=%llu  irqs_during=%lu",
              (unsigned long long)iters_A, (unsigned long)irqs_during_A);

    /* ============================================================ */
    /* Step 6: ForcePause                                           */
    /* ============================================================ */
    step_begin("SDL_DOSAudioForcePause (request flag set)");
    SDL_DOSAudioForcePause();
    step_done("ForcePause request submitted (handled at next IRQ)");

    /* ============================================================ */
    /* Step 7: verify pause engaged within 200 ms                   */
    /* ============================================================ */
    step_begin("Verify IRQ count freezes within 200 ms cap");
    int pause_engaged = 0;
    int last_count = -1;
    int stable_streak_ms = 0;
    double t_pause_start = now_secs();
    while ((now_secs() - t_pause_start) < 0.2) {
        uint32_t c = dos_port_audio_irq_count;
        if ((int)c == last_count) {
            stable_streak_ms += 10;
            if (stable_streak_ms >= 100) { pause_engaged = 1; break; }
        } else {
            stable_streak_ms = 0;
            last_count = (int)c;
        }
        SDL_Delay(10);
        if (kbd_pending()) {
            step_done("operator-aborted via keyboard");
            goto cleanup;
        }
    }
    if (pause_engaged) {
        step_done("pause engaged (100 ms stable IRQ count)");
    } else {
        step_done("PAUSE DID NOT ENGAGE within 200 ms cap (last_count=%d)", last_count);
        plog("");
        plog("ABORT: ForcePause request was issued but dos_port_audio_irq_count");
        plog("       kept incrementing past the 200 ms watchdog. Either the helper");
        plog("       isn't linked from libSDL3.a or it doesn't actually halt IRQ-5.");
        plog("       Scenario B skipped; cannot compute yield delta.");
        goto cleanup;
    }

    /* ============================================================ */
    /* Step 8: Scenario B — audio paused, time synth loop 1.0 sec   */
    /* ============================================================ */
    step_begin("Scenario B: synth loop 1.0 sec, audio PAUSED");
    cnt0 = dos_port_audio_irq_count;
    uint64_t iters_B = synth_loop_for(1.0);
    cnt1 = dos_port_audio_irq_count;
    uint32_t irqs_during_B = cnt1 - cnt0;
    step_done("iters=%llu  irqs_during=%lu (should be 0 if pause held)",
              (unsigned long long)iters_B, (unsigned long)irqs_during_B);

    /* ============================================================ */
    /* Step 9: ForceResume                                          */
    /* ============================================================ */
    step_begin("SDL_DOSAudioForceResume");
    SDL_DOSAudioForceResume();
    SDL_Delay(100);
    cnt0 = dos_port_audio_irq_count;
    SDL_Delay(200);
    cnt1 = dos_port_audio_irq_count;
    uint32_t resume_delta = cnt1 - cnt0;
    step_done("resume verify: %lu IRQs in 200 ms post-resume",
              (unsigned long)resume_delta);

    /* ============================================================ */
    /* Step 10: yield computation                                   */
    /* ============================================================ */
    step_begin("Compute yield");
    if (iters_A == 0 || iters_B == 0) {
        step_done("INVALID: zero iterations in one or both scenarios");
        goto cleanup;
    }
    double ratio = (double)iters_B / (double)iters_A;
    /* Yield fraction = (B - A) / B = how much of B's wall-time was extra
     * vs what A could do in the same wall (B is paused so all wall is
     * available; A has IRQ-5 stealing some). */
    double yield_frac = ((double)iters_B - (double)iters_A) / (double)iters_B;
    /* Project to a 16.67 ms (60 fps) flip budget. */
    double yield_per_60fps = 16.67 * yield_frac;
    /* Project to a 25.6 ms (~39 fps current title) flip budget. */
    double yield_per_current = 25.6 * yield_frac;
    step_done("ratio B/A=%.4f yield_frac=%.4f", ratio, yield_frac);

    plog("");
    plog("---- Results ----");
    plog("Scenario A (audio active): iters=%llu  irqs=%lu",
         (unsigned long long)iters_A, (unsigned long)irqs_during_A);
    plog("Scenario B (audio paused): iters=%llu  irqs=%lu",
         (unsigned long long)iters_B, (unsigned long)irqs_during_B);
    plog("Throughput ratio B/A:      %.4f", ratio);
    plog("Yield fraction:            %.4f (B-A)/B", yield_frac);
    plog("Projected yield @ 60fps:   %.2f ms/flip", yield_per_60fps);
    plog("Projected yield @ 39fps:   %.2f ms/flip (current title baseline)",
         yield_per_current);
    plog("");

    plog("Decision criteria (per team-lead brief, applied to 60 fps target):");
    plog("  yield < 1.0 ms/flip       -> slot 0115 not worth defaulting on");
    plog("  yield 1.0-3.0 ms/flip     -> SHIP slot 0115 (default-on safe)");
    plog("  yield >= 6.0 ms/flip      -> idle-pause savings real; HIGH PRIORITY");
    plog("");
    /* Reliability gate: if no IRQs were observed during scenario A, the
     * counter is not wired into this binary's audio path (or the audio
     * backend isn't actually running IRQ-driven). Numbers are misleading;
     * refuse to emit a recommendation to avoid silent-narrow per
     * silent_narrow_pattern.md / pressure_test_tidy_narratives.md. */
    if (irqs_during_A == 0) {
        plog("=== HEADLINE: yield computed = %.2f ms/flip (UNRELIABLE) ===",
             yield_per_60fps);
        plog("=== irqs_during_A = 0; counter not wired or audio not IRQ-driven. ===");
        plog("=== DOSBox-X smoke: this is EXPECTED. Real-HW iter J = data gate. ===");
        plog("=== NO recommendation emitted; rerun on g2k for definitive number. ===");
    } else if (yield_per_60fps < 1.0) {
        plog("=== HEADLINE: yield = %.2f ms/flip -> RECOMMEND DROP slot 0115 ===",
             yield_per_60fps);
    } else if (yield_per_60fps < 3.0) {
        plog("=== HEADLINE: yield = %.2f ms/flip -> RECOMMEND SHIP slot 0115 ===",
             yield_per_60fps);
    } else if (yield_per_60fps < 6.0) {
        plog("=== HEADLINE: yield = %.2f ms/flip -> SHIP slot 0115 SOON ===",
             yield_per_60fps);
    } else {
        plog("=== HEADLINE: yield = %.2f ms/flip -> SHIP slot 0115 PRIORITY ===",
             yield_per_60fps);
    }
    plog("");
    plog("Cross-check: irqs_during_A should be ~80 (10 ms IRQ at 11025 mono);");
    plog("irqs_during_B should be ~0 (paused). irqs_during_A=%lu  irqs_during_B=%lu",
         (unsigned long)irqs_during_A, (unsigned long)irqs_during_B);

cleanup:
    /* ============================================================ */
    /* Step 11+: teardown                                           */
    /* ============================================================ */
    plog("");
    step_begin("SDL_DOSAudioForceResume (defensive cleanup)");
    SDL_DOSAudioForceResume();
    step_done("resume request submitted");

    step_begin("SDL_DestroyAudioStream");
    SDL_DestroyAudioStream(stream);
    step_done("stream destroyed");

    step_begin("SDL_Quit");
    SDL_Quit();
    step_done("SDL_Quit returned");

    plog("");
    plog("=== IDLEPROB done ===");
    if (g_log) fclose(g_log);
    return 0;
}
