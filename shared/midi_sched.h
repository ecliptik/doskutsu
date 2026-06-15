#ifndef SHARED_MIDI_SCHED_H
#define SHARED_MIDI_SCHED_H

/*
 * midi_sched.h -- shared, reusable Standard MIDI File parser + tick scheduler
 * (plain C). The canonical SMF playback-scheduling module for SDL-DOS ports:
 * SETUP.EXE consumes it for its audio test, and it is intended as the single
 * scheduler any future SDL-DOS port (or the engine itself) reuses, instead of
 * each maintaining a divergent reimplementation.
 *
 * Plain-C port of the engine's proven MidiScheduler parser + non-ISR tick()
 * (vendor/nxengine-evo/src/sound/MidiScheduler.cpp), kept bit-equivalent: SMF
 * format 0/1, VLQ, running status, tempo meta, end-of-track; aftertouch /
 * channel-pressure / pitch-bend / SysEx are parsed-and-skipped (same Stage-2
 * simplification as the engine). The heavy engine machinery (L2b ISR tick,
 * DPMI locks, preload cache) is intentionally omitted -- callers tick on the
 * main loop exactly like the engine's non-ISR tick().
 *
 * SCOPE NOTE (T56): the SMF parse + the tick scheduler here are verified
 * line-for-line equivalent to MidiScheduler, so this module guarantees
 * engine-equivalent SCHEDULING. It does NOT own audio OUTPUT -- the OPL3 /
 * WaveBlaster rendering is the sink's job (see midi_sched_sink); any tempo
 * issue audible despite correct scheduling lives in the sink, not here.
 *
 * Backend-agnostic: parsed events are dispatched through a midi_sched_sink of
 * function pointers, so the same player drives either the WaveBlaster MPU-401
 * byte path or the OPL3 voice path. Reads an SMF file at RUNTIME (no MIDI data
 * is committed or shipped).
 *
 * DOS/DJGPP: fopen(path, "rb"); no threads; static; the double tempo math runs
 * on the main thread (no ISR / FPU constraint). ASCII-only.
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
  void  *user;
} midi_sched_sink;

typedef struct midi_sched midi_sched;

/* Parse an SMF file (format 0/1). Returns NULL on open / parse failure
 * (caller falls back to a tone). */
midi_sched  *midi_sched_open(const char *path);
void     midi_sched_close(midi_sched *m);

/* Bind the backend sink (copied by value). */
void     midi_sched_set_sink(midi_sched *m, const midi_sched_sink *sink);

/* Arm playback from the song start at wall-clock now_ms (ms). */
void     midi_sched_start(midi_sched *m, uint64_t now_ms);

/* Advance playback to now_ms, dispatching every event whose time has elapsed
 * to the bound sink. Loops from the start at end-of-song. */
void     midi_sched_tick(midi_sched *m, uint64_t now_ms);

/* Parsed event count (0 if not loaded) -- for build/test witnesses. */
uint32_t midi_sched_event_count(const midi_sched *m);

/* T42 tempo witness: cumulative channel events dispatched since midi_sched_start,
 * and the scheduler's current song position in ms (position_us / 1000). Used to
 * cross-check effective tempo against an independent wall clock on real HW. */
uint64_t midi_sched_dispatched(const midi_sched *m);
uint64_t midi_sched_position_ms(const midi_sched *m);

/* T56 tempo witness: the LIVE scheduling scale -- microseconds per MIDI tick
 * (tempo_us_per_quarter / division_ppq, x1000 to keep the fraction in an int),
 * and the active tempo (us per quarter) + division (PPQ). For curly.mid these
 * should read 787916 (us_per_tick x1000) / 378210 / 480 once the tick-0 tempo
 * event has dispatched; a default 1041666 / 500000 means the tempo event never
 * applied. This is the instrument that distinguishes a SCHEDULING mis-scale
 * from an OUTPUT-layer cause (d_wall-vs-d_real only catches a clock stall). */
uint64_t midi_sched_us_per_tick_x1000(const midi_sched *m);
uint32_t midi_sched_tempo_us(const midi_sched *m);
uint16_t midi_sched_division(const midi_sched *m);

#endif /* SHARED_MIDI_SCHED_H */
