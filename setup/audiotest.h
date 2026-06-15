#ifndef SETUP_AUDIOTEST_H
#define SETUP_AUDIOTEST_H

/*
 * audiotest.h -- audio test interface for SETUP.EXE.
 *
 * The operator chose the standalone-SETUP-links-the-audio-stack design:
 * SETUP plays test SFX / music itself through the same SDL3 + SDL3_mixer +
 * Organya/OPL3/WaveBlaster stack the game uses. That linkage (and its
 * real-HW validation) is the remaining build-side step; this header is the
 * stable seam between the TUI and that backend so the UI is built and
 * wired now and the backend drops in behind it without UI churn.
 *
 * AUDIOTEST_LINKED is 0 in the current scaffold build (the TUI shows a
 * "not yet wired" notice) and becomes 1 once audiotest_sdl.c is compiled
 * in against the static libs. ASCII-only.
 */

#include "setupcfg.h"

#ifndef AUDIOTEST_LINKED
#define AUDIOTEST_LINKED 0
#endif

/* 1 if a real audio backend is compiled in, 0 for the scaffold. */
int audiotest_available(void);

/* Bring up the audio device using the backend / mixer settings in c.
 * Returns 0 on success, non-zero on failure (message in audiotest_error). */
int  audiotest_init(const scfg_t *c);
void audiotest_shutdown(void);

/* Play a representative effect / a short music loop in the current backend.
 * No-ops (return non-zero) in the scaffold build. */
int  audiotest_play_sfx(void);
int  audiotest_play_music(void);
void audiotest_stop_music(void);

/* Last error / status string for the UI. */
const char *audiotest_error(void);

/* One-line description of a single test for the chooser ABOUT box, resolved to
 * what will actually play (real sound vs. fallback tone). phase 0 = SFX test,
 * 1 = music test. Returns a static string (<= ~110 chars, fits a 60x3 box);
 * never NULL. In the scaffold build it returns a generic per-phase sentence. */
const char *audiotest_about(int phase);

/* ---- progress callback seam (T24 popup) ----------------------------- *
 * The audio-test popup (setup/main.c) animates a progress bar over each
 * bounded test. The backend owns "how far through the whole test am I";
 * the UI just renders. The UI registers a callback with
 * audiotest_set_progress(); the backend invokes audiotest_progress() with
 * OVERALL test progress in permille [0,1000] as it services the ring
 * (audiotest_sdl.c pump_service). A NULL callback (the default) animates
 * nothing -- the popup simply fills the bar when the (blocking) play call
 * returns. Storage + clamping/dispatch live in audiotest_progress.c, linked
 * into both the stub and the SDL build, so the seam exists regardless of
 * which backend is compiled. */
typedef void (*audiotest_progress_cb)(int permille, void *user);
void audiotest_set_progress(audiotest_progress_cb cb, void *user);
void audiotest_progress(int permille);

#endif /* SETUP_AUDIOTEST_H */
