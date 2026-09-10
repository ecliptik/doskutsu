/*
 * probe_sdl_stubs.c -- shared engine-symbol stubs for SDL-linked probes.
 *
 * The SDL3 DOS audio backend (vendor/SDL/src/audio/dos/SDL_dosaudio_sb.c, as
 * of patch SDL/0071 "Lever 3 SfxSynth IRQ-mix") references engine-owned externs.
 * The engine (NXEngine-evo) defines them; standalone DJGPP probes link
 * libSDL3.a WITHOUT any engine object files, so any probe that pulls
 * SDL_dosaudio_sb.obj (i.e. opens an audio device) gets an unresolved-symbol
 * link error. This single shared TU supplies those definitions so the whole
 * SDL-audio probe fleet (audbuf / idleprob / mpusdl / sdlprob1 / qhexit / ...)
 * links independently of the engine.
 *
 * Same cross-link-unit-symbol class as the SDL/0070 #118 gate: a symbol
 * referenced by one link unit must be defined within that link unit when the
 * defining compilation unit (here, the engine's Pixtone.cpp) is absent.
 *
 * This TU is linked into every PROBES_SDL_* probe via PROBES_SDL_LDLIBS (see
 * Makefile). Add ONE line here for each future engine->SDL extern a Lever
 * patch introduces -- that keeps the seam in one place instead of re-stubbing
 * per probe.
 *
 * Values: held at the engine's power-on default. None of these is meaningfully
 * read by a probe (no probe starts a SfxSynth channel), so the value only has
 * to make the link resolve.
 *
 * NOT linked into the production game binary -- the engine provides the real
 * definitions there; this TU is probe-only (gitignored, like all probe sources).
 *
 * License: MIT.
 */

#include <stdint.h>

/* SDL/0071 Lever-3 SfxSynth IRQ-mix: engine-owned active-source counter.
 * Engine def (C linkage): vendor/nxengine-evo/src/sound/Pixtone.cpp:51
 *   volatile std::uint32_t g_dos_sfx_synth_active_count = 0;
 * SDL ref: vendor/SDL/src/audio/dos/SDL_dosaudio_sb.c:234 (extern). */
volatile uint32_t g_dos_sfx_synth_active_count = 0;
