#ifndef SETUP_MIDISET_H
#define SETUP_MIDISET_H

/*
 * midiset.h -- MIDI music-set discovery for SETUP.EXE (backlog #39).
 *
 * The engine's music backend (WaveBlaster / OPL3 GM) plays Standard MIDI
 * Files from a set directory under data/. Which set it uses is resolved once
 * at init from the SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE hint (the MIDI_SET
 * config key). SETUP lets the user pick the set from the Music screen.
 *
 * IMPORTANT (verified vendor/nxengine-evo/src/sound/SoundManager.cpp:545-640):
 * the engine does NOT treat the hint value as a directory name. It maps a
 * CLOSED SET of LOGICAL values to fixed directories:
 *
 *   hint value            -> data subdir   ; meaning
 *   "" / "wiimidi"        -> data/midi/    ; WiiWare arrangements (default)
 *   "orgmid"              -> data/orgmid/  ; ORGMID note-for-note transcription
 *   "orgmid" + GM_VARIANT -> data/orgmid1/ , data/orgmid2/  (dev A/B; NOT
 *                                            exposed by SETUP -- Q-A2)
 *   any other value       -> data/midi/    ; unrecognized -> fallback + warn
 *
 * So SETUP offers only the KNOWN logical sets whose directory is present on
 * disk. Arbitrary user "drop-in" directories are NOT reachable without an
 * engine change (a passthrough in that else-branch) and are deliberately not
 * offered here. The known-set table (midiset.c) is the single point to extend
 * if that engine passthrough later lands.
 *
 * ASCII-only, C89-friendly (DJGPP). No SDL dependency. Uses POSIX
 * opendir/readdir/stat, available on both DJGPP and the host test compiler.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MIDISET_MAX        4   /* wiimidi + orgmid + headroom            */
#define MIDISET_VALUE_MAX  16  /* hint value, e.g. "wiimidi"             */
#define MIDISET_DIR_MAX    16  /* data subdir, e.g. "midi"               */
#define MIDISET_LABEL_MAX  24  /* friendly label, e.g. "WiiWare"         */

typedef struct
{
  char value[MIDISET_VALUE_MAX]; /* MIDI_SET / hint value ("wiimidi"/"orgmid") */
  char dir[MIDISET_DIR_MAX];     /* data subdir name ("midi"/"orgmid")         */
  char label[MIDISET_LABEL_MAX]; /* friendly display label                     */
  int  mid_count;                /* number of .mid files in the set dir (>=1)  */
} midiset_t;

/* Scan <data_dir> (e.g. "data") for the known MIDI sets whose directory is
 * present and holds >=1 .mid file. Fills sets[0..return) in known-table order;
 * never lists a directory the engine cannot load. Returns the count (0..max).
 * data_dir NULL/"" defaults to "data". */
int midiset_scan(const char *data_dir, midiset_t *sets, int max);

/* Index into sets[] whose value matches `value` (case-insensitive), treating
 * an empty/NULL value as the default "wiimidi". Returns -1 if not present. */
int midiset_index_by_value(const midiset_t *sets, int n, const char *value);

/* The default MIDI_SET value (the engine's byte-neutral default). */
#define MIDISET_DEFAULT_VALUE "wiimidi"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SETUP_MIDISET_H */
