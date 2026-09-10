/*
 * sdlprob2.c -- SDL3-DOS composite-scenario cost decomposition (section 2 A-D)
 * (Phase 11 iter L, task #24 deliverable).
 *
 * Per docs/PHASE11-SDLPROBE-CONTRACT.md section 2: 4 composite scenarios that
 * mirror engine drawcall patterns using DIRECT SDL primitives, NOT engine
 * wrappers (probe is self-contained; engine wrapper costs are separately
 * measured by Renderer.cpp instrumentation).
 *
 *   A: tilemap drawcall pre-slot-0117  (SetClipRect + Blit-colorkey + counter_bump)
 *   B: tilemap drawcall post-slot-0117 (drawTileFast direct path: pure Blit-colorkey)
 *   C: sprite full path                 (SetClipRect + Blit + ColorMod + RenderTexture)
 *   D: menu INDEX8+alpha+colorkey       (SetTextureAlphaMod + RenderTexture; engine
 *                                        shape per Q3 from sdl-engine -- pre-cached
 *                                        texture, NOT per-call CreateTexture)
 *
 * (A - B) = empirical post-0117 savings -- the load-bearing measurement
 * from the contract. C = full sprite cost. D = menu slow-path lift per
 * `sdl3_index8_alpha_colorkey_bug.md`.
 *
 * Forensic protocol per section 4: lives in `sdlprobe_common.h`. This .c just
 * supplies the 4 composite body functions + main() driver.
 *
 * Companion: `sdlprob1.c` (sdl-engine task #23) covers per-primitive section 1
 * + auxiliary section 3. Same `sdlprobe_common.h` header; same LOG-format
 * conventions; iter L bundle ships both binaries alongside iter L's
 * engine binary + flush-instr's SDLPROB1.BAT + SDLPROB2.BAT drivers.
 *
 * Per dosbox_not_perf_proxy.md: DOSBox-X smoke = correctness only (probe
 * runs end-to-end, BEGIN/DONE markers fire, watchdog clean exit, LOG
 * file written, structural sanity). Real-HW iter L is the data gate.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/sdlprob2.c
 *   Binary:   SDLPROB2.EXE  (8+3, fits)
 *   Log:      SDLPROB2.LOG  (8+3, fits)
 *   BAT:      SDLPROB2.BAT  (8+3, fits) -- flush-instr authors per iter L bundling
 *
 * License: MIT.
 */

#define SDLPROBE_LOG_PATH "SDLPROB2.LOG"
#define SDLPROBE_BANNER   "=== SDLPROB2 iter L composites starting ==="

#include "sdlprobe_common.h"

/* ============================================================ */
/* Composite-scenario helpers + body functions                   */
/* ============================================================ */

static sdlprobe_state_t g_state;

/* Engine-shape mirror: a "stat counter" that the pre-slot-0117 path
 * bumped per drawcall. Volatile to prevent the optimizer from eliding
 * the increment. */
static volatile uint32_t g_compA_counter = 0;
static SDL_Rect  g_clip_full = { 0, 0, 320, 240 };
static SDL_FRect g_rt_dst_1to1 = { 0.0f, 0.0f, 32.0f, 32.0f };

/* ---- Composite A: tilemap drawcall PRE-slot-0117 ---- */
/* Mirrors engine's pre-fast-path tilemap drawcall: SetClipRect (boundary
 * check), BlitSurface with colorkey, drawcall stat counter bump. The
 * SetClipRect + counter are what slot-0117 was supposed to bypass. */

static void body_composite_A(void *ud)
{
    (void)ud;
    if (!g_state.dst || !g_state.src_32x32) return;
    SDL_SetSurfaceColorKey(g_state.src_32x32, true, 0);
    SDL_SetSurfaceClipRect(g_state.dst, &g_clip_full);
    SDL_BlitSurface(g_state.src_32x32, NULL, g_state.dst, NULL);
    g_compA_counter++;
}

/* ---- Composite B: tilemap drawcall POST-slot-0117 ---- */
/* Mirrors engine's drawTileFast direct path: pure BlitSurface with
 * colorkey, no SetClipRect, no counter. (A - B) = the per-call savings
 * slot-0117 delivers when the engine fast-path engages. */

static void body_composite_B(void *ud)
{
    (void)ud;
    if (!g_state.dst || !g_state.src_32x32) return;
    SDL_SetSurfaceColorKey(g_state.src_32x32, true, 0);
    SDL_BlitSurface(g_state.src_32x32, NULL, g_state.dst, NULL);
}

/* ---- Composite C: sprite full path ---- */
/* Mirrors engine's sprite drawcall: SetClipRect + Blit + ColorMod +
 * SetTextureAlphaMod + RenderTexture. Texture is PRE-CACHED at probe
 * setup (engine-shape per Q3); per-call only mutates state + draws. */

static void body_composite_C(void *ud)
{
    (void)ud;
    if (!g_state.dst || !g_state.src_32x32 || !g_state.renderer ||
        !g_state.tex_32x32_cached) return;
    SDL_SetSurfaceClipRect(g_state.dst, &g_clip_full);
    SDL_BlitSurface(g_state.src_32x32, NULL, g_state.dst, NULL);
    SDL_SetSurfaceColorMod(g_state.src_32x32, 200, 100, 50);
    SDL_SetTextureAlphaMod(g_state.tex_32x32_cached, 255);
    SDL_RenderTexture(g_state.renderer, g_state.tex_32x32_cached, NULL,
                      &g_rt_dst_1to1);
}

/* ---- Composite D: menu INDEX8 + alpha + colorkey slow path ---- */
/* Per sdl3_index8_alpha_colorkey_bug.md: this combo falls through both
 * SDL fast paths into SDL_RenderTexture's per-pixel slow path which
 * doesn't honor colorkey (transparent pixels render opaque black).
 *
 * Per Q3 from sdl-engine: bug-trigger is INSIDE SDL_RenderTexture's
 * software-renderer path when alpha<255 + colorkey set on source.
 * Engine shape = pre-cached texture + per-call SetTextureAlphaMod +
 * RenderTexture. NOT per-call texture create + Blit (that was my Q3
 * working-assumption error sdl-engine corrected).
 *
 * Pre-cache setup: g_state.tex_32x32_cached is created at probe init
 * from g_state.src_32x32 which has colorkey=0 set BEFORE the
 * CreateTextureFromSurface call here (so the texture inherits the
 * colorkey property). */

static void body_composite_D(void *ud)
{
    (void)ud;
    if (!g_state.renderer || !g_state.tex_32x32_cached) return;
    /* Engine per-call: only the alpha-mod + render. Colorkey is baked
     * into the texture at creation time. */
    SDL_SetTextureAlphaMod(g_state.tex_32x32_cached, 128);
    SDL_RenderTexture(g_state.renderer, g_state.tex_32x32_cached, NULL,
                      &g_rt_dst_1to1);
}

/* ============================================================ */
/* Pre-flight: ensure cached texture has colorkey baked in       */
/* (composite D contract: pre-cached texture inherits colorkey)  */
/* ============================================================ */

static void prep_composite_D_texture(void)
{
    if (!g_state.src_32x32 || !g_state.renderer) return;
    /* Set colorkey on the source surface BEFORE re-creating the cached
     * texture. SDL3 texture inherits surface colorkey at creation time
     * but does NOT re-read source surface mutations after. */
    SDL_SetSurfaceColorKey(g_state.src_32x32, true, 0);
    if (g_state.tex_32x32_cached) {
        SDL_DestroyTexture(g_state.tex_32x32_cached);
        g_state.tex_32x32_cached = NULL;
    }
    g_state.tex_32x32_cached =
        SDL_CreateTextureFromSurface(g_state.renderer, g_state.src_32x32);
    if (!g_state.tex_32x32_cached) {
        sdlprobe_plog("WARN: composite D pre-flight CreateTexture failed: %s",
                      SDL_GetError());
    }
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
    sdlprobe_plog("Per docs/PHASE11-SDLPROBE-CONTRACT.md section 2 (composites A-D).");
    sdlprobe_plog("Companion sdlprob1.c (sdl-engine task #23) covers per-primitive section 1");
    sdlprobe_plog("+ auxiliary section 3; both binaries share sdlprobe_common.h scaffolding.");
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

    /* 4 composite scenarios -- set total for ledger. */
    sdlprobe_set_total_scenarios(4);
    sdlprobe_g_whole_t0 = sdlprobe_now_secs();

    /* A and B (pre/post-slot-0117) -- paired measurement; B - A = savings. */
    sdlprobe_run_body_scenario("composite_A_tile_pre_0117",
                               SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                               body_composite_A, NULL);
    sdlprobe_run_body_scenario("composite_B_tile_post_0117",
                               SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                               body_composite_B, NULL);

    /* C -- sprite full path. */
    sdlprobe_run_body_scenario("composite_C_sprite_full",
                               SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                               body_composite_C, NULL);

    /* D -- menu slow path. Pre-flight: re-create cached texture with
     * colorkey baked in (engine-shape per Q3). */
    prep_composite_D_texture();
    sdlprobe_run_body_scenario("composite_D_menu_INDEX8_alpha_CK_slow",
                               SDLPROBE_WARMUP_DEFAULT, SDLPROBE_DEFAULT_N,
                               body_composite_D, NULL);

    /* Composite-A counter sanity: confirms scenarios actually executed
     * (volatile counter not elided by the optimizer). */
    sdlprobe_plog("");
    sdlprobe_plog("composite_A_counter final = %lu (non-zero confirms scenarios ran)",
                  (unsigned long)g_compA_counter);

    sdlprobe_emit_summary();

    sdlprobe_plog("");
    sdlprobe_plog("=== Decision interpretation (per contract section 5) ===");
    sdlprobe_plog("  A - B > 30 us/call -> slot 0117 confirmed; sibling fast paths for sprite/Font");
    sdlprobe_plog("  C > 100 us/call    -> sprite-atlas refactor candidate");
    sdlprobe_plog("  D > 2x C           -> ship _blit_indexed_alpha (deepdive section 12)");
    sdlprobe_plog("  Cross-anchor: any scenario < DPMI thunk floor (~30 us per dpmithn.exe)");
    sdlprobe_plog("                = SDL-layer-unfixable; pursue engine-side optimization.");
    sdlprobe_plog("");
    sdlprobe_plog("=== SDLPROB2 done ===");

    sdlprobe_teardown_state(&g_state);
    sdlprobe_close_log();
    return 0;
}
