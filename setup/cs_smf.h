#ifndef SETUP_CS_SMF_H
#define SETUP_CS_SMF_H

/*
 * cs_smf.h -- standalone Standard MIDI File parser + tick scheduler for
 * SETUP.EXE's audio test (plain C).
 *
 * Plain-C port of the engine's MidiScheduler parser + non-ISR tick()
 * (vendor/nxengine-evo/src/sound/MidiScheduler.cpp). SMF format 0/1, VLQ,
 * running status, tempo meta, end-of-track; aftertouch / channel-pressure /
 * pitch-bend / SysEx are parsed-and-skipped (same Stage-2 simplification as
 * the engine). The heavy engine machinery (L2b ISR tick, DPMI locks, preload
 * cache) is intentionally omitted -- SETUP ticks on the main loop exactly like
 * the engine's non-ISR tick().
 *
 * Backend-agnostic: parsed events are dispatched through a cs_smf_sink of
 * function pointers, so the same player drives either the WaveBlaster MPU-401
 * byte path or the OPL3 voice path. Reads the user's installed
 * data/midi/<name>.mid at RUNTIME; nothing Cave-Story-derived is committed or
 * shipped.
 *
 * DOS/DJGPP: fopen(path, "rb"); no threads; static; the double tempo math runs
 * on the SETUP main thread (no ISR / FPU constraint). ASCII-only.
 */

#include <stdint.h>

/* Backend sink -- the player invokes these per dispatched event. Channel is
 * 0-15. Any callback may be NULL (the event is then ignored). */
typedef struct
{
  void (*note_on)(void *user, int channel, int note, int velocity);
  void (*note_off)(void *user, int channel, int note, int velocity);
  void (*control_change)(void *user, int channel, int controller, int value);
  void (*program_change)(void *user, int channel, int program);
  /* T56 optional dispatch witness: if non-NULL, called for EVERY dispatched
   * event just before the typed callback, with the event's scheduled time
   * (event_us = abs_tick * us_per_tick) vs the scheduler's current position_us.
   * Lets the caller log per-note on-schedule-ness (event_us vs position_us =
   * dispatch lag) on real HW. NULL = no overhead. */
  void (*dispatch)(void *user, int type, uint64_t event_us, uint64_t position_us,
                   int channel, int data1, int data2);
  void  *user;
} cs_smf_sink;

typedef struct cs_smf cs_smf;

/* Parse an SMF file (format 0/1). Returns NULL on open / parse failure
 * (caller falls back to a tone). */
cs_smf  *cs_smf_open(const char *path);
void     cs_smf_close(cs_smf *m);

/* Bind the backend sink (copied by value). */
void     cs_smf_set_sink(cs_smf *m, const cs_smf_sink *sink);

/* Arm playback from the song start at wall-clock now_ms (ms). */
void     cs_smf_start(cs_smf *m, uint64_t now_ms);

/* Advance playback to now_ms, dispatching every event whose time has elapsed
 * to the bound sink. Loops from the start at end-of-song. MAIN-LINE only (uses
 * double FPU + calls the optional dispatch witness). */
void     cs_smf_tick(cs_smf *m, uint64_t now_ms);

/* T56 route A: ISR-SAFE variant of cs_smf_tick -- integer fixed-point math (no
 * FPU), no alloc, no dispatch-witness callback. Drive this from the SB16 IRQ-5
 * MIDI tick (SDL_DOSMidiTickRegister) so the preview dispatches at ~43 Hz like
 * the in-game MidiScheduler instead of the cooperative TUI loop's ~2-3 Hz.
 * CONTRACT (caller/SETUP-side wiring): DPMI-lock m + m->events + the cb; the
 * bound sink callbacks must be ISR-safe (no FPU/alloc/IO); drive a given cs_smf
 * with EITHER cs_smf_tick OR cs_smf_tick_isr, never both. See cs_smf.c. */
void     cs_smf_tick_isr(cs_smf *m, uint64_t now_ms);

/* T56 route A: DPMI-lock the cs_smf struct + parsed events array (everything
 * cs_smf_tick_isr touches in-ISR). Call AFTER cs_smf_open, BEFORE registering
 * the ISR cb. Returns 1 on success / no-op off DJGPP; 0 on lock failure (then
 * do NOT register the ISR cb -- fall back to main-line cs_smf_tick). The caller
 * owns locking the cb code + the g_title_smf pointer. */
int      cs_smf_isr_lock(cs_smf *m);

/* Parsed event count (0 if not loaded) -- for build/test witnesses. */
uint32_t cs_smf_event_count(const cs_smf *m);

/* T42 tempo witness: cumulative channel events dispatched since cs_smf_start,
 * and the scheduler's current song position in ms (position_us / 1000). Used to
 * cross-check effective tempo against an independent wall clock on real HW. */
uint64_t cs_smf_dispatched(const cs_smf *m);
uint64_t cs_smf_position_ms(const cs_smf *m);

/* T56 tempo witness: the LIVE scheduling scale -- microseconds per MIDI tick
 * (tempo_us_per_quarter / division_ppq, x1000 to keep the fraction in an int),
 * and the active tempo (us per quarter) + division (PPQ). For curly.mid these
 * should read 787916 (us_per_tick x1000) / 378210 / 480 once the tick-0 tempo
 * event has dispatched; a default 1041666 / 500000 means the tempo event never
 * applied. This is the instrument that distinguishes a SCHEDULING mis-scale
 * from an OUTPUT-layer cause (d_wall-vs-d_real only catches a clock stall). */
uint64_t cs_smf_us_per_tick_x1000(const cs_smf *m);
uint32_t cs_smf_tempo_us(const cs_smf *m);
uint16_t cs_smf_division(const cs_smf *m);

#endif /* SETUP_CS_SMF_H */
