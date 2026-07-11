#ifndef SETUP_CS_GUSMIDI_H
#define SETUP_CS_GUSMIDI_H

/*
 * cs_gusmidi.h -- SETUP.EXE game-accurate GUS music preview (task #4 v2).
 *
 * Plain-C reimplementation of the game's NXE::Sound::MidiBackendGus
 * (vendor/nxengine-evo/src/sound/MidiBackendGus.cpp) -- real GM .pat wavetable
 * MIDI rendered on the GF1, so the SETUP "Test music" sounds like the game and
 * RESPECTS the configured GUS_VOICES (GetState().num_voices), instead of the
 * synthetic sine stand-in. Follows the cs_opl3midi / cs_opl2midi port pattern:
 * a cs_smf_sink of dispatch callbacks driven by cs_smf on the SETUP main loop
 * (NOT the game's PIT/IRQ-0 pump -- so FPU is fine and there is no ISR-context
 * discipline). SETUP opens the GF1 fresh with no Pixtone SFX bank, so this owns
 * the whole voice pool + DRAM for the preview.
 *
 * MIDDLE cut (v1): single nearest-middle-C sample per instrument + instant
 * note-off (the 0254 release-ramp + 0255 multisample fidelity levers are a
 * later fast-follow). Not multisample, no software release.
 *
 * DOS/DJGPP: fopen(path, "rb") for the binary .pat; ASCII-only. The dispatch
 * runs on the main thread (no ISR), so double FPU pitch math is fine.
 */

#include "cs_smf.h"    /* cs_smf, cs_smf_sink */

/* Install a printf-style trace hook (SETUP's own SETUP.LOG writer). Optional;
 * NULL-safe if never set. Call once before cs_gusmidi_init. */
void cs_gusmidi_set_trace(void (*fn)(const char *msg));

/* Prepare the GF1 GM synth. Call AFTER SDL_DOSGusInit succeeded (device_open).
 * Reads GetState for num_voices (== the configured GUS_VOICES) + DRAM size,
 * builds the note-frequency table, clears voice/instrument state. Idempotent.
 * Returns 1 if the GF1 is up + ready, 0 otherwise. */
int  cs_gusmidi_init(void);

/* Pre-scan the parsed song (walked once via cs_smf) for the GM programs + drum
 * notes it references, then upload exactly those .pat instruments from ULTRADIR
 * into GF1 DRAM. MAIN-LOOP context (file I/O). Rewinds the DRAM bump allocator
 * first, so it is safe to re-run per song. Returns the number of instruments
 * that became resident (0 -> nothing uploaded, caller should fall back to the
 * sine preview). */
int  cs_gusmidi_song_prescan(cs_smf *m);

/* Reset per-channel MIDI controller state + stop all voices (on_song_start
 * tail). Call after the prescan, before playback. */
void cs_gusmidi_song_start(void);

/* Fill `out` with the note_on/note_off/control_change/program_change callbacks
 * for cs_smf_set_sink(). Voices are allocated per note against the configured
 * pool (num_voices); finished one-shots are reaped inside the note path only
 * (never on a pump spin -- keeps the driver's start-race poll-grace intact). */
void cs_gusmidi_get_sink(cs_smf_sink *out);

/* Stop every voice (panic; e.g. on ESC / test end). */
void cs_gusmidi_all_notes_off(void);

/* Tear down synth bookkeeping (the GF1 itself is quiesced by device_close's
 * SDL_DOSGusStopAllVoices + SDL_DOSGusShutdown). */
void cs_gusmidi_shutdown(void);

#endif /* SETUP_CS_GUSMIDI_H */
