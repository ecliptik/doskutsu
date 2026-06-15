/*
 * audiotest_progress.c -- progress-callback dispatch for the audio-test popup.
 *
 * Tiny, backend-agnostic seam (see audiotest.h). The UI (setup/main.c)
 * registers a callback; the audio backend (audiotest_sdl.c pump_service)
 * calls audiotest_progress(permille) as it services the SB16 ring during a
 * bounded test. Kept in its own translation unit so it links into BOTH the
 * stub build (AUDIOTEST_LINKED=0) and the live SDL build (=1) -- the popup
 * shell compiles + the seam resolves regardless of which backend is selected.
 *
 * The dispatch is plain main-thread function-pointer indirection: the SDL DOS
 * backend is cooperatively scheduled and pump_service() runs on the main
 * thread, so a callback that pokes VRAM (the bar redraw) is safe here -- no
 * ISR / no second thread. ASCII-only source.
 */

#include "audiotest.h"

static audiotest_progress_cb g_cb   = 0;
static void                 *g_user = 0;

void audiotest_set_progress(audiotest_progress_cb cb, void *user)
{
  g_cb   = cb;
  g_user = user;
}

void audiotest_progress(int permille)
{
  if (permille < 0)    permille = 0;
  if (permille > 1000) permille = 1000;
  if (g_cb) g_cb(permille, g_user);
}
