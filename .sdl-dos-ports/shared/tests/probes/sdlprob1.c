/*
 * sdlprob1.c -- SDL3-DOS per-primitive cost decomposition (section 1)
 * + auxiliary signals (section 3)
 * (Phase 11 iter L, task #23 deliverable, sdl-engine).
 *
 * Per docs/PHASE11-SDLPROBE-CONTRACT.md section 1: 10 named primitives
 * expanded across 22+ sub-cells (BlitSurface alone = 12 sub-cells across
 * 3 sizes x +/-colorkey x +/-alpha). Per section 3 auxiliary signals:
 * hidden-allocation tracking is built into common.h's mem_delta per
 * scenario; DPMI hop detection emerges via the bimodality flag; PR #15377
 * untried-knob status is emitted as an early-init banner block (one
 * `active=N` line per hint).
 *
 * Companion: `sdlprob2.c` (probe-engineer task #24) covers composites
 * section 2 A-D. Both binaries share `sdlprobe_common.h` (probe-engineer
 * task #24 deliverable: BEGIN/DONE markers, watchdog, sample-stash, RDTSC
 * calibration via SDL/0039 timer dispatch, scenario-emit framework).
 *
 * Per dosbox_not_perf_proxy.md + dosbox_not_behavioral_proxy_for_io.md:
 * DOSBox-X smoke = correctness only (probe runs end-to-end, BEGIN/DONE
 * markers fire, watchdog clean exit, LOG file written, structural sanity).
 * Real-HW iter L is the data gate.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/sdlprob1.c
 *   Binary:   SDLPROB1.EXE  (8+3, fits)
 *   Log:      SDLPROB1.LOG  (8+3, fits)
 *   BAT:      SDLPROB1.BAT  (8+3, fits) -- flush-instr authors per iter L bundling
 *
 * License: MIT.
 */

#define SDLPROBE_LOG_PATH "SDLPROB1.LOG"
#define SDLPROBE_BANNER   "=== SDLPROB1 iter L per-primitive starting ==="

#include "sdlprobe_common.h"

/* ============================================================ */
/* Per-scenario state (probe-local; common.h provides g_state)   */
/* ============================================================ */

static sdlprobe_state_t g_state;

/* Persistent SDL_FRect for stable-pointer pass-through to the body
 * functions. Not measured directly; just argument plumbing. */
static SDL_FRect g_frect_1to1_32   = { 0.0f, 0.0f, 32.0f, 32.0f };
static SDL_FRect g_frect_2x_32     = { 0.0f, 0.0f, 64.0f, 64.0f };

/* Volatile sink to prevent the optimizer eliding scenarios with no
 * observable side effect (e.g. SDL_GetTicks(); body must consume the
 * return value). */
static volatile uint64_t g_sink_u64 = 0;
static volatile uint32_t g_sink_u32 = 0;

/* ============================================================ */
/* Scenario 1: SDL_GetTicks() -- pure call cost                  */
/* ============================================================ */

static void body_get_ticks(void *ud)
{
    (void)ud;
    g_sink_u64 = SDL_GetTicks();
}

/* ============================================================ */
/* Scenario 2: SDL_PumpEvents() -- 2 sub-cells                   */
/*   2a: queue empty (no-pending)                                */
/*   2b: synthetic event pushed via SDL_PushEvent                */
/*                                                                */
/* "kbd-pending" / "mouse-pending" sub-cells from contract are    */
/* approximated by the synthetic-pushed-event variant; truly      */
/* hardware-driven event sources require operator interaction     */
/* which the probe doesn't have.                                  */
/* ============================================================ */

static void body_pump_events_no_pending(void *ud)
{
    (void)ud;
    SDL_PumpEvents();
}

static void body_pump_events_with_pushed(void *ud)
{
    (void)ud;
    /* Push a synthetic event each iter so the queue isn't empty when
     * PumpEvents is called. SDL_USEREVENT is appropriate (no SDL-internal
     * handler will mutate state). */
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;
    SDL_PushEvent(&ev);
    SDL_PumpEvents();
}

/* ============================================================ */
/* Scenario 3: SDL_BlitSurface (INDEX8 -> INDEX8)                */
/*   3 sizes (8x8 / 32x32 / 320x240) x                           */
/*   2 colorkey states (without / with) x                         */
/*   2 alpha states (full / half) =                               */
/*   12 sub-cells                                                 */
/*                                                                */
/* Userdata indexes into a permutation: [size_idx, ck, alpha].   */
/* size_idx = 0/1/2 = 8x8 / 32x32 / 320x240.                     */
/* ============================================================ */

typedef struct {
    SDL_Surface *src;
    SDL_Surface *dst;
} blit_ud_t;

static void body_blit(void *ud)
{
    blit_ud_t *b = (blit_ud_t *)ud;
    SDL_BlitSurface(b->src, NULL, b->dst, NULL);
}

/* Configure src colorkey + alpha state, run the scenario. */
static void blit_run_subcell(sdlprobe_state_t *s, int size_idx,
                             bool with_colorkey, bool half_alpha,
                             const char *name)
{
    SDL_Surface *src;
    switch (size_idx) {
        case 0: src = s->src_8x8;     break;
        case 1: src = s->src_32x32;   break;
        case 2: src = s->src_320x240; break;
        default: return;
    }
    if (!src) return;

    SDL_SetSurfaceColorKey(src, with_colorkey, 0);
    SDL_SetSurfaceAlphaMod(src, half_alpha ? 128 : 255);
    SDL_SetSurfaceBlendMode(src, half_alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);

    blit_ud_t ud = { src, s->dst };

    /* Heavy variant (320x240) gets fewer reps per common.h's MAX_SAMPLES
     * cap (1024); also benefits from heavy-tier watchdog. Mid (32x32) +
     * cheap (8x8) get full N=1000. */
    int reps = (size_idx == 2) ? 256 : SDLPROBE_DEFAULT_N;
    int warmup = (size_idx == 0) ? SDLPROBE_WARMUP_CHEAP : SDLPROBE_WARMUP_DEFAULT;

    sdlprobe_run_body_scenario(name, warmup, reps, body_blit, &ud);

    /* Reset colorkey + alpha so subsequent scenarios start from clean
     * src state. */
    SDL_SetSurfaceColorKey(src, false, 0);
    SDL_SetSurfaceAlphaMod(src, 255);
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
}

/* ============================================================ */
/* Scenario 4: SDL_CreateTextureFromSurface + SDL_DestroyTexture */
/*                                                                */
/* Per-call alloc cost; body creates + immediately destroys. The */
/* mem_delta tracking in common.h's DONE marker reveals whether  */
/* SDL caches/leaks across N=1000 reps. (Negative mem_delta =    */
/* leak; zero = caches or returns memory cleanly.)                */
/* ============================================================ */

typedef struct {
    SDL_Renderer *r;
    SDL_Surface  *src;
} create_destroy_ud_t;

static void body_create_destroy_texture(void *ud)
{
    create_destroy_ud_t *cd = (create_destroy_ud_t *)ud;
    SDL_Texture *t = SDL_CreateTextureFromSurface(cd->r, cd->src);
    if (t) SDL_DestroyTexture(t);
}

/* ============================================================ */
/* Scenario 5: SDL_RenderTexture (sw renderer)                   */
/*   5a: 1:1 dst (32x32 src to 32x32 dst FRect)                  */
/*   5b: 2x stretch (32x32 src to 64x64 dst FRect)                */
/* ============================================================ */

typedef struct {
    SDL_Renderer *r;
    SDL_Texture  *t;
    const SDL_FRect *dst;
} render_tex_ud_t;

static void body_render_texture(void *ud)
{
    render_tex_ud_t *rt = (render_tex_ud_t *)ud;
    SDL_RenderTexture(rt->r, rt->t, NULL, rt->dst);
}

/* ============================================================ */
/* Scenario 6: SDL_SetSurfaceColorMod / SDL_SetSurfaceBlendMode  */
/*   6a: SetSurfaceColorMod (white -> nonwhite alternation)      */
/*   6b: SetSurfaceBlendMode (none -> blend alternation)         */
/* ============================================================ */

static volatile int g_color_mod_toggle = 0;

static void body_set_color_mod(void *ud)
{
    SDL_Surface *src = (SDL_Surface *)ud;
    g_color_mod_toggle ^= 1;
    if (g_color_mod_toggle) {
        SDL_SetSurfaceColorMod(src, 255, 255, 255);
    } else {
        SDL_SetSurfaceColorMod(src, 128, 128, 128);
    }
}

static void body_set_blend_mode(void *ud)
{
    SDL_Surface *src = (SDL_Surface *)ud;
    g_color_mod_toggle ^= 1;
    SDL_SetSurfaceBlendMode(src, g_color_mod_toggle ? SDL_BLENDMODE_BLEND
                                                    : SDL_BLENDMODE_NONE);
}

/* ============================================================ */
/* Scenario 7: SDL_RenderPresent (cross-validate W18 ~1.1 ms)    */
/*                                                                */
/* The DOS direct-VESA flush path. Per `sdl3_dos_quirks.md` the   */
/* hint flip is REQUIRED; common.h's setup_state already does it. */
/* This scenario isolates RenderPresent's per-call cost to        */
/* cross-validate wave-18's anchor measurement.                   */
/* ============================================================ */

static void body_render_present(void *ud)
{
    SDL_Renderer *r = (SDL_Renderer *)ud;
    SDL_RenderPresent(r);
}

/* ============================================================ */
/* Scenario 8: SDL_Delay(1) -- sleep granularity vs DPMI quantum */
/*                                                                */
/* Engine main loop calls SDL_Delay(1) for cooperative yielding.  */
/* Cap is 5s default but with N=1000 reps x ~1 ms each, this      */
/* will hit the 5s cap reliably -- USE A SMALLER N here.          */
/* ============================================================ */

static void body_delay_1ms(void *ud)
{
    (void)ud;
    SDL_Delay(1);
}

/* ============================================================ */
/* Scenario 9: SDL audio callback entry overhead (custom-shape) */
/*                                                                */
/* Cannot measure per-call entry+exit from probe context (the    */
/* IRQ fires on its own schedule; we don't drive it). Instead we */
/* sample the dos_port_audio_irq_count + irq_wall_us pair over   */
/* a fixed wall-clock window and compute mean per-IRQ cost.      */
/* Single derived sample emitted via emit_done_stats.             */
/* ============================================================ */

static void scenario_audio_callback_entry(void)
{
    sdlprobe_scenario_begin("audio_callback_entry_overhead");

    /* Open an audio stream so the SDL audio thread starts and IRQ-5 fires.
     * Use a small buffer (256 frames @ 11025 mono S16 matches operator's
     * Tier-2 default for IRQ rate cohesion). */
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256",
                            SDL_HINT_OVERRIDE);
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = 11025;
    SDL_AudioStream *as = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!as) {
        sdlprobe_plog("WARN: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        sdlprobe_emit_done_stats("audio_callback_entry_overhead",
                                 NULL, 0, 0, "stream_open_failed");
        return;
    }
    SDL_ResumeAudioStreamDevice(as);

    /* Wait a short settling period so IRQ-5 fires steadily. */
    SDL_Delay(200);

    uint32_t irq0     = dos_port_audio_irq_count;
    uint32_t wall_us0 = dos_port_audio_irq_wall_us;
    double   t0       = sdlprobe_now_secs();

    /* Sample window = 3 sec (43 Hz IRQ rate at default 22050 stereo,
     * 86 Hz at our 11025 mono / 256 frames; 200-260 IRQs in 3 sec --
     * plenty for a meaningful mean). */
    SDL_Delay(3000);

    uint32_t irq1     = dos_port_audio_irq_count;
    uint32_t wall_us1 = dos_port_audio_irq_wall_us;
    double   t1       = sdlprobe_now_secs();

    SDL_DestroyAudioStream(as);

    uint32_t d_irq  = irq1 - irq0;
    uint32_t d_wall = wall_us1 - wall_us0;
    double   d_t    = t1 - t0;
    double   irq_hz = (d_t > 0.0) ? (double)d_irq / d_t : 0.0;
    double   us_per_irq = (d_irq > 0) ? (double)d_wall / (double)d_irq : 0.0;

    sdlprobe_plog("audio: window=%.2fs irq_count_delta=%lu wall_us_delta=%lu",
                  d_t, (unsigned long)d_irq, (unsigned long)d_wall);
    sdlprobe_plog("audio: irq_rate=%.1f Hz  per_irq_wall=%.2f us  (W20A2 anchor: ~150-200 us/irq)",
                  irq_hz, us_per_irq);

    /* Synthesize a 1-sample "DONE" so the scenario tally accounts for it.
     * Use the per-IRQ wall-us value as the synthetic cycles count;
     * cycles_to_us conversion will be off (since this is already a
     * derived value) but the log lines above carry the accurate number. */
    if (d_irq > 0) {
        static uint32_t synth_sample[1];
        synth_sample[0] = (uint32_t)us_per_irq;
        sdlprobe_emit_done_stats("audio_callback_entry_overhead",
                                 synth_sample, 1, 0, NULL);
    } else {
        sdlprobe_emit_done_stats("audio_callback_entry_overhead",
                                 NULL, 0, 0, "no_irq_fired_in_window");
    }
}

/* ============================================================ */
/* Scenario 10: dos_port_audio_irq_count++ baseline              */
/*                                                                */
/* The IRQ handler's actual increment instruction sequence cost. */
/* Use a probe-local volatile counter to avoid interfering with  */
/* the real shared counter.                                       */
/* ============================================================ */

static volatile uint32_t g_baseline_counter = 0;

static void body_irq_count_increment(void *ud)
{
    (void)ud;
    g_baseline_counter++;
    g_sink_u32 = g_baseline_counter;  /* prevent dead-code elision */
}

/* ============================================================ */
/* Auxiliary signals (section 3): PR #15377 untried-knob status  */
/* ============================================================ */

static void emit_aux_pr15377_hints_status(void)
{
    sdlprobe_plog("");
    sdlprobe_plog("=== Auxiliary section 3: PR #15377 hint status (research-only) ===");
    sdlprobe_plog("Hint values shown are read AFTER setup_state ran (which sets");
    sdlprobe_plog("DOS_ALLOW_DIRECT_FRAMEBUFFER=1). 'unset' = SDL3 default.");

    /* SDL_GetHint returns NULL if hint isn't set. */
    const char *h_drv  = SDL_GetHint("SDL_RENDER_DRIVER");
    const char *h_vsy  = SDL_GetHint("SDL_RENDER_VSYNC");
    const char *h_dbl  = SDL_GetHint("SDL_VIDEO_DOUBLE_BUFFER");
    const char *h_aud  = SDL_GetHint("SDL_AUDIO_DRIVER");
    const char *h_dfb  = SDL_GetHint("SDL_DOS_ALLOW_DIRECT_FRAMEBUFFER");
    const char *h_lfb  = SDL_GetHint("SDL_DOS_PREFER_LFB");
    const char *h_pin  = SDL_GetHint("SDL_DOS_PIN_WINDOW_TO_NATIVE_MODE");
    const char *h_smp  = SDL_GetHint("SDL_AUDIO_DEVICE_SAMPLE_FRAMES");

    sdlprobe_plog("  RENDER_DRIVER             = %s", h_drv ? h_drv : "unset");
    sdlprobe_plog("  RENDER_VSYNC              = %s", h_vsy ? h_vsy : "unset");
    sdlprobe_plog("  VIDEO_DOUBLE_BUFFER       = %s", h_dbl ? h_dbl : "unset");
    sdlprobe_plog("  AUDIO_DRIVER              = %s", h_aud ? h_aud : "unset");
    sdlprobe_plog("  DOS_ALLOW_DIRECT_FRAMEBUFFER = %s", h_dfb ? h_dfb : "unset");
    sdlprobe_plog("  DOS_PREFER_LFB            = %s", h_lfb ? h_lfb : "unset");
    sdlprobe_plog("  DOS_PIN_WINDOW_TO_NATIVE_MODE = %s", h_pin ? h_pin : "unset");
    sdlprobe_plog("  AUDIO_DEVICE_SAMPLE_FRAMES = %s", h_smp ? h_smp : "unset (SDL default 512 @ 22050)");
    sdlprobe_plog("");
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    sdlprobe_open_log();
    sdlprobe_plog("%s", SDLPROBE_BANNER);
    sdlprobe_plog("DJGPP + libSDL3 (post-slot-0048) build");
    sdlprobe_plog("");
    sdlprobe_plog("Per docs/PHASE11-SDLPROBE-CONTRACT.md section 1 + section 3.");
    sdlprobe_plog("Companion sdlprob2.c (probe-engineer task #24) covers composites section 2;");
    sdlprobe_plog("both binaries share sdlprobe_common.h scaffolding.");
    sdlprobe_plog("");
    sdlprobe_plog("Forensic protocol: BEGIN/DONE markers per-line fsync; cap-trip emits");
    sdlprobe_plog("ABORT_PARTIAL with median-of-captured + skips next; whole-probe 8 min cap.");
    sdlprobe_plog("");

    sdlprobe_init_timer_log_banner();
    sdlprobe_plog("");

    if (sdlprobe_setup_state(&g_state) != 0) {
        sdlprobe_plog("FATAL: SDL state setup failed; exiting probe.");
        sdlprobe_teardown_state(&g_state);
        sdlprobe_close_log();
        return 2;
    }
    sdlprobe_plog("");

    /* Auxiliary section 3: emit PR #15377 hint status block. */
    emit_aux_pr15377_hints_status();

    /* Total scenario count = 1 (GetTicks) + 2 (PumpEvents) + 12 (Blit
     * 3 sizes x +/-CK x +/-alpha) + 1 (CreateTex+Destroy) + 2 (RenderTex
     * 1:1 + 2x) + 2 (ColorMod + BlendMode) + 1 (RenderPresent) + 1 (Delay1)
     * + 1 (audio_callback) + 1 (irq_count_baseline) = 24 sub-cells. */
    sdlprobe_set_total_scenarios(24);
    sdlprobe_g_whole_t0 = sdlprobe_now_secs();

    /* ---- Scenario 1: SDL_GetTicks() ---- */
    sdlprobe_run_body_scenario("get_ticks",
                               SDLPROBE_WARMUP_CHEAP, SDLPROBE_DEFAULT_N,
                               body_get_ticks, NULL);

    /* ---- Scenario 2: SDL_PumpEvents() ---- */
    sdlprobe_run_body_scenario("pump_events_no_pending",
                               SDLPROBE_WARMUP_CHEAP, SDLPROBE_DEFAULT_N,
                               body_pump_events_no_pending, NULL);
    sdlprobe_run_body_scenario("pump_events_with_pushed",
                               SDLPROBE_WARMUP_CHEAP, SDLPROBE_DEFAULT_N,
                               body_pump_events_with_pushed, NULL);

    /* ---- Scenario 3: SDL_BlitSurface (12 sub-cells) ---- */
    blit_run_subcell(&g_state, 0, false, false, "blit_8x8_noCK_a255");
    blit_run_subcell(&g_state, 0, true,  false, "blit_8x8_CK_a255");
    blit_run_subcell(&g_state, 0, false, true,  "blit_8x8_noCK_a128");
    blit_run_subcell(&g_state, 0, true,  true,  "blit_8x8_CK_a128");

    blit_run_subcell(&g_state, 1, false, false, "blit_32x32_noCK_a255");
    blit_run_subcell(&g_state, 1, true,  false, "blit_32x32_CK_a255");
    blit_run_subcell(&g_state, 1, false, true,  "blit_32x32_noCK_a128");
    blit_run_subcell(&g_state, 1, true,  true,  "blit_32x32_CK_a128");

    blit_run_subcell(&g_state, 2, false, false, "blit_320x240_noCK_a255");
    blit_run_subcell(&g_state, 2, true,  false, "blit_320x240_CK_a255");
    blit_run_subcell(&g_state, 2, false, true,  "blit_320x240_noCK_a128");
    blit_run_subcell(&g_state, 2, true,  true,  "blit_320x240_CK_a128");

    /* ---- Scenario 4: CreateTextureFromSurface + DestroyTexture ---- */
    {
        create_destroy_ud_t cd = { g_state.renderer, g_state.src_32x32 };
        sdlprobe_run_body_scenario("create_destroy_texture_32x32",
                                   SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                                   body_create_destroy_texture, &cd);
    }

    /* ---- Scenario 5: SDL_RenderTexture (2 sub-cells) ---- */
    if (g_state.tex_32x32_cached) {
        render_tex_ud_t rt1 = { g_state.renderer, g_state.tex_32x32_cached, &g_frect_1to1_32 };
        sdlprobe_run_body_scenario("render_texture_1to1",
                                   SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                                   body_render_texture, &rt1);

        render_tex_ud_t rt2 = { g_state.renderer, g_state.tex_32x32_cached, &g_frect_2x_32 };
        sdlprobe_run_body_scenario("render_texture_2x_stretch",
                                   SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                                   body_render_texture, &rt2);
    } else {
        sdlprobe_plog("");
        sdlprobe_plog("=== SCENARIO N/A: render_texture_* SKIP (cached texture absent) ===");
    }

    /* ---- Scenario 6: SetSurfaceColorMod / SetSurfaceBlendMode ---- */
    sdlprobe_run_body_scenario("set_surface_color_mod",
                               SDLPROBE_WARMUP_CHEAP, SDLPROBE_DEFAULT_N,
                               body_set_color_mod, g_state.src_32x32);
    sdlprobe_run_body_scenario("set_surface_blend_mode",
                               SDLPROBE_WARMUP_CHEAP, SDLPROBE_DEFAULT_N,
                               body_set_blend_mode, g_state.src_32x32);

    /* ---- Scenario 7: SDL_RenderPresent ---- */
    sdlprobe_run_body_scenario("render_present_direct_vesa",
                               SDLPROBE_WARMUP_DEFAULT, 256,
                               body_render_present, g_state.renderer);

    /* ---- Scenario 8: SDL_Delay(1) ---- */
    /* N=200 reps = 200ms wall (well within 5s cap; doesn't dominate runtime). */
    sdlprobe_run_body_scenario("delay_1ms_granularity",
                               SDLPROBE_WARMUP_CHEAP, 200,
                               body_delay_1ms, NULL);

    /* ---- Scenario 9: audio callback entry overhead (custom-shape) ---- */
    scenario_audio_callback_entry();

    /* ---- Scenario 10: dos_port_audio_irq_count++ baseline ---- */
    sdlprobe_run_body_scenario("irq_count_increment_baseline",
                               SDLPROBE_WARMUP_CHEAP, SDLPROBE_DEFAULT_N,
                               body_irq_count_increment, NULL);

    /* Sanity: confirm the volatile-sink reads actually happened (so the
     * compiler didn't elide scenarios with no observable side effect). */
    sdlprobe_plog("");
    sdlprobe_plog("sanity: g_sink_u64=%llu g_sink_u32=%lu g_baseline_counter=%lu",
                  (unsigned long long)g_sink_u64,
                  (unsigned long)g_sink_u32,
                  (unsigned long)g_baseline_counter);

    sdlprobe_emit_summary();

    sdlprobe_plog("");
    sdlprobe_plog("=== Decision interpretation (per contract section 5) ===");
    sdlprobe_plog("  PumpEvents no-pending > 50 us       -> events-throttle candidate");
    sdlprobe_plog("  Hidden alloc (mem_delta != 0)       -> alloc-elimination patch");
    sdlprobe_plog("  RenderPresent ~1.1 ms anchor (W18)  -> regression check");
    sdlprobe_plog("  Cross-anchor: any primitive < DPMI thunk floor (~30 us per dpmithn.exe)");
    sdlprobe_plog("                = SDL-layer-unfixable; pursue engine-side instead.");
    sdlprobe_plog("");
    sdlprobe_plog("=== SDLPROB1 done ===");

    sdlprobe_teardown_state(&g_state);
    sdlprobe_close_log();
    return 0;
}
