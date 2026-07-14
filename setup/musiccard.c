/* musiccard.c -- the music TYPE + music CARD tables and resolvers (SETUP UX v2).
 * See musiccard.h for the model. Pure: no TUI, no cfg object, no globals. */
#include <string.h>

#include "musiccard.h"

const music_type_t MUSIC_TYPES[] = {
  { "organya", MCARD_SUB_ORGANYA, "Organya",
    "Built-in Cave Story music. No sound card needed. First launch pre-renders "
    "all songs to disk (one-time wait)." },
  { NULL,      MCARD_SUB_NONE,    "MIDI",
    "Music played by a synth on a sound card. Pick the card below." },
  { "none",    MCARD_SUB_NONE,    "No Music",
    "Music off. Sound effects still play." }
};

/* The restored FLAT hardware picker, in operator-locked order. This list names
 * HARDWARE -- which is what "Select Music Card" always implied. The Music Type
 * row above it supplies the music-vs-hardware context the old flat list lacked,
 * so "Sound Blaster" is honest again.
 *
 * The parentheticals earn their keep: the picker POPUP covers the screen, so at
 * the instant of choosing, the Type row is NOT visible -- and a bare "Sound
 * Blaster" is exactly what an operator ON a Sound Blaster once misread as naming
 * their hardware rather than the OPL3 FM music path. Cards whose name is already
 * unambiguous (Auto-detect, Gravis UltraSound) get none.
 *
 * AUDIO_BACKEND=wb is shared by TWO cards (WaveBlaster daughterboard vs an
 * external General MIDI module); the SETUP-only MIDI_DEV discriminator tells them
 * apart (the engine ignores MIDI_DEV). */
const music_card_t MUSIC_CARDS[] = {
  { "auto",  NULL,          MCARD_SUB_SB,   "Auto-detect",
    "Auto-detect",
    "Detect the installed sound hardware." },
  { "opl3",  NULL,          MCARD_SUB_SB,   "Sound Blaster",
    "Sound Blaster (OPL3 FM)",
    "OPL3 FM synth on a Sound Blaster 16/Pro." },
  { "adlib", NULL,          MCARD_SUB_NONE, "AdLib",
    "AdLib (OPL2, music only)",
    "OPL2 FM synth. AdLib or OPL2 card. Music only." },
  { "wb",    "waveblaster", MCARD_SUB_SB,   "WaveBlaster",
    "WaveBlaster daughterboard",
    "Wavetable daughterboard on the SB MIDI header." },
  { "wb",    "genmidi",     MCARD_SUB_SB,   "General MIDI",
    "General MIDI (external module)",
    "MPU-401 to an external General MIDI module." },
  { "gus",   NULL,          MCARD_SUB_GUS,  "Gravis UltraSound",
    "Gravis UltraSound",
    "GF1 wavetable. Gravis UltraSound / PicoGUS." }
};

const int MUSIC_NCARDS =
    (int)(sizeof(MUSIC_CARDS) / sizeof(MUSIC_CARDS[0]));

int music_type_of(const char *backend)
{
  if (!backend) return MTYPE_MIDI;
  if (strcmp(backend, "organya") == 0) return MTYPE_ORGANYA;
  if (strcmp(backend, "none") == 0)    return MTYPE_NONE;
  return MTYPE_MIDI;
}

int music_card_index(const char *backend, const char *midi_dev)
{
  int i, wb_fallback = -1;
  if (music_type_of(backend) != MTYPE_MIDI) return -1;
  for (i = 0; i < MUSIC_NCARDS; ++i)
  {
    if (strcmp(MUSIC_CARDS[i].value, backend) != 0) continue;
    if (MUSIC_CARDS[i].midi_dev == NULL) return i;  /* non-wb: one row per value */
    if (midi_dev && strcmp(MUSIC_CARDS[i].midi_dev, midi_dev) == 0) return i;
    if (strcmp(MUSIC_CARDS[i].midi_dev, "waveblaster") == 0) wb_fallback = i;
  }
  if (wb_fallback >= 0) return wb_fallback;
  return 0; /* unknown/empty backend -> Auto-detect */
}

const char *music_card_short(const char *backend, const char *midi_dev)
{
  int i = music_card_index(backend, midi_dev);
  return (i >= 0) ? MUSIC_CARDS[i].name : "";
}

const char *music_card_label(const char *backend, const char *midi_dev)
{
  int t = music_type_of(backend);
  if (t != MTYPE_MIDI) return MUSIC_TYPES[t].label;
  return music_card_short(backend, midi_dev);
}
