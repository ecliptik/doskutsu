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
 *   "" / "orgmid2"        -> data/orgmid2/ ; org2mid v2, native GM (DEFAULT)
 *   "orgmid1"             -> data/orgmid1/ ; org2mid v1
 *   "wiimidi"             -> data/midi/    ; external WiiWare arrangements
 *   "orgmid" (+ GM_VARIANT) -> data/orgmid/ (legacy) / orgmid1 / orgmid2
 *   any other value       -> data/<dir>/ if it holds .mid, else wiimidi + warn
 *
 * SETUP offers the KNOWN logical sets whose directory is present on disk
 * (WiiWare/OrgMIDI), and -- since #39b (engine patch 0226) -- ALSO any other
 * data/ subdir holding >=1 .mid as a user "drop-in" set labelled
 * "Custom (<dir>)". MIDI_SET then stores the bare dir name and the engine's
 * #39b passthrough loads data/<dir>/<name>.mid (else-branch of the source
 * resolver in vendor/nxengine-evo/src/sound/SoundManager.cpp). The known-set
 * table (midiset.c) remains the place to promote a dir to a friendly label.
 *
 * ASCII-only, C89-friendly (DJGPP). No SDL dependency. Uses POSIX
 * opendir/readdir/stat, available on both DJGPP and the host test compiler.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MIDISET_MAX        8   /* wiimidi + orgmid + custom drop-ins (#39b) */
#define MIDISET_VALUE_MAX  16  /* hint value / dir name, e.g. "wiimidi"     */
#define MIDISET_DIR_MAX    16  /* data subdir, e.g. "midi"                  */
#define MIDISET_LABEL_MAX  24  /* friendly label, e.g. "Custom (mymidi)"    */

typedef struct
{
  char value[MIDISET_VALUE_MAX]; /* MIDI_SET / hint value ("wiimidi"/"orgmid") */
  char dir[MIDISET_DIR_MAX];     /* data subdir name ("midi"/"orgmid")         */
  char label[MIDISET_LABEL_MAX]; /* friendly display label                     */
  int  mid_count;                /* number of .mid files in the set dir (>=1)  */
} midiset_t;

/* Scan <data_dir> (e.g. "data") for MIDI sets whose directory is present and
 * holds >=1 .mid file. Fills sets[0..return): the known sets first in
 * known-table order (WiiWare/OrgMIDI), then any other data/ subdir as a
 * "Custom (<dir>)" drop-in (#39b). Never lists a directory the engine cannot
 * load. Returns the count (0..max). data_dir NULL/"" defaults to "data". */
int midiset_scan(const char *data_dir, midiset_t *sets, int max);

/* Index into sets[] whose value matches `value` (case-insensitive), treating
 * an empty/NULL value as the default "orgmid2". Returns -1 if not present. */
int midiset_index_by_value(const midiset_t *sets, int n, const char *value);

/* The default MIDI_SET value -- MUST match the engine default in
 * SoundManager.cpp (nx0262). "orgmid2" = our org2mid v2 native-GM set; the
 * engine falls back to wiimidi if data/orgmid2/ was not generated. */
#define MIDISET_DEFAULT_VALUE "orgmid2"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SETUP_MIDISET_H */
