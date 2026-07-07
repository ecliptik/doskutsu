#ifndef SETUP_CS_OPL2MIDI_H
#define SETUP_CS_OPL2MIDI_H

/*
 * cs_opl2midi.h -- OPL2 (AdLib / YM3812) MIDI voice backend for SETUP's audio
 * test.
 *
 * Plain-C port of the engine's MidiBackendOpl2 (vendor/nxengine-evo/src/sound/
 * MidiBackendOpl2.cpp, Campaign 2 / patches 0234-0236): a 9-voice single-bank
 * LRU allocator, the same 8-patch GM family bucket, the same MIDI note ->
 * (fnum, block) table, and note-on/off/control-change/program-change dispatch.
 * It is the OPL2 twin of setup/cs_opl3midi.c and is modelled on that file
 * verbatim, differing only in:
 *   - 9 voices (OPL2 primary bank) instead of 18 (OPL3 dual bank);
 *   - the OPL2-specific chip lifecycle helpers (SDL_DOSOpl2Detect / InitChip /
 *     Shutdown) -- NO OPL3-mode enable (0x105) and NO secondary bank
 *     (0x38A/0x38B); voices 0-8 reuse the bank-agnostic SDL_DOSOpl3* transport.
 *
 * Loads the same data/opl3bank.dat 128-program DMXOPL bank the game's OPL2
 * backend loads, so the SETUP preview sounds like the game (not a thinner
 * placeholder). The built-in 8-patch placeholder remains the fallback when the
 * bank file is absent or short, so an incomplete install still previews. AdLib
 * is MUSIC-ONLY: a DAC-less OPL chip has no PCM sound effects.
 *
 * Single static instance (SETUP plays one title theme at a time). The sink
 * callbacks ignore the `user` pointer. ASCII-only.
 */

#include "cs_smf.h"

/* Detect + initialize the OPL2 chip (direct port I/O at 0x388; needs no
 * SDL_INIT_AUDIO and no SDL3_mixer) and load data/opl3bank.dat. Returns 1 if an
 * OPL chip is present and the backend is ready, 0 otherwise (caller falls back). */
int  cs_opl2midi_init(void);

/* Number of GM programs loaded from data/opl3bank.dat (0 = the file was
 * absent/short and the 8-patch placeholder is in use). For the SETUP log so the
 * g2k trace confirms which bank the AdLib preview played. Valid after init. */
int  cs_opl2midi_bank_programs(void);

/* Reset all voices + per-channel GM state (engine on_song_start). Call before
 * starting the scheduler. */
void cs_opl2midi_song_start(void);

/* Fill `out` with the note dispatch callbacks for cs_smf_set_sink(). */
void cs_opl2midi_get_sink(cs_smf_sink *out);

/* Silence every active voice (engine on_song_stop). */
void cs_opl2midi_all_notes_off(void);

/* All-notes-off + 0xBD rhythm/tremolo latch clear + SDL_DOSOpl2Shutdown. */
void cs_opl2midi_shutdown(void);

#endif /* SETUP_CS_OPL2MIDI_H */
