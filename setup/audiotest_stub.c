/*
 * audiotest_stub.c -- scaffold audio-test backend (no SDL linkage).
 *
 * Compiled into SETUP.EXE until the real SDL3 + SDL3_mixer + synth backend
 * (audiotest_sdl.c) is wired in. Every play call is a clearly-labelled
 * no-op so the TUI can be built and exercised today without the heavy
 * link. See audiotest.h. ASCII-only.
 */

#include "audiotest.h"

#if !AUDIOTEST_LINKED

static const char *g_msg = "audio test backend not yet linked (scaffold build)";

int  audiotest_available(void)            { return 0; }
int  audiotest_init(const scfg_t *c)      { (void)c; return 1; }
void audiotest_shutdown(void)             { }
int  audiotest_play_sfx(void)             { return 1; }
int  audiotest_play_music(void)           { return 1; }
void audiotest_stop_music(void)           { }
const char *audiotest_error(void)         { return g_msg; }

const char *audiotest_about(int phase)
{
  return phase ? "Plays a test tone"
               : "Plays a test sound";
}

#endif /* !AUDIOTEST_LINKED */
