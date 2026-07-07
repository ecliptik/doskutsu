#ifndef SETUP_CS_OPL3MIDI_H
#define SETUP_CS_OPL3MIDI_H

/*
 * cs_opl3midi.h -- OPL3 (YMF262) MIDI voice backend for SETUP's audio test.
 *
 * Plain-C port of the engine's MidiBackendOpl3 (vendor/nxengine-evo/src/sound/
 * MidiBackendOpl3.cpp): 18-voice LRU allocator, 8-patch GM family bucket, MIDI
 * note -> (fnum, block) table, note-on/off/control-change/program-change. It
 * drives the real OPL3 chip through the SDL DOS core's SDL_DOSOpl3* helpers
 * (the same primitives audiotest_sdl.c already uses for the single-voice
 * arpeggio). Loads the same data/opl3bank.dat 128-program DMXOPL bank the game's
 * OPL3 backend loads, so the SETUP preview matches the game; the built-in
 * 8-patch placeholder remains the fallback when the bank file is absent/short.
 *
 * Single static instance (SETUP plays one title theme at a time). The sink
 * callbacks ignore the `user` pointer. ASCII-only.
 */

#include "cs_smf.h"

/* Detect + initialize the OPL3 chip and load data/opl3bank.dat. Returns 1 if a
 * chip is present and the backend is ready, 0 otherwise (caller falls back). */
int  cs_opl3midi_init(void);

/* Number of GM programs loaded from data/opl3bank.dat (0 = the file was
 * absent/short and the 8-patch placeholder is in use). For the SETUP log so the
 * g2k trace confirms which bank the OPL3 preview played. Valid after init. */
int  cs_opl3midi_bank_programs(void);

/* Reset all voices + per-channel GM state (engine on_song_start). Call before
 * starting the scheduler. */
void cs_opl3midi_song_start(void);

/* Fill `out` with the note dispatch callbacks for cs_smf_set_sink(). */
void cs_opl3midi_get_sink(cs_smf_sink *out);

/* Silence every active voice (engine on_song_stop). */
void cs_opl3midi_all_notes_off(void);

/* All-notes-off + SDL_DOSOpl3Shutdown (restores Adlib/OPL2 mode). */
void cs_opl3midi_shutdown(void);

#endif /* SETUP_CS_OPL3MIDI_H */
