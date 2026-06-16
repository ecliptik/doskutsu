/*
 * midiset.c -- MIDI music-set discovery for SETUP.EXE. See midiset.h.
 * ASCII-only, no SDL. POSIX dirent/stat (DJGPP + host).
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "midiset.h"

/* The KNOWN logical sets the engine can load, in display order. Each maps a
 * hint value (what MIDI_SET stores) to its fixed data subdir + a friendly
 * label. This is the ONLY place to extend if the engine ever gains an
 * arbitrary-directory passthrough (then a disk scan can append unknown dirs
 * as {value=dir, label="Custom (<dir>)"} entries). */
static const struct
{
  const char *value;
  const char *dir;
  const char *label;
} MIDISET_KNOWN[] =
{
  { "wiimidi", "midi",   "WiiWare" },
  { "orgmid",  "orgmid", "OrgMIDI" }, /* display name only; engine hint value stays "orgmid" */
};
#define MIDISET_KNOWN_N ((int)(sizeof(MIDISET_KNOWN) / sizeof(MIDISET_KNOWN[0])))

/* Case-insensitive ".mid" suffix test. */
static int ends_with_mid(const char *name)
{
  size_t n = strlen(name);
  if (n < 4) return 0;
  {
    const char *s = name + (n - 4);
    return s[0] == '.' &&
           (s[1] == 'm' || s[1] == 'M') &&
           (s[2] == 'i' || s[2] == 'I') &&
           (s[3] == 'd' || s[3] == 'D');
  }
}

/* Count .mid files directly in `path`. Returns 0 if the dir is absent/unreadable. */
static int count_mid_files(const char *path)
{
  DIR *d;
  struct dirent *e;
  int count = 0;

  d = opendir(path);
  if (!d) return 0;
  while ((e = readdir(d)) != NULL)
  {
    if (e->d_name[0] == '.') continue; /* skip ., .., dotfiles */
    if (ends_with_mid(e->d_name)) ++count;
  }
  closedir(d);
  return count;
}

int midiset_scan(const char *data_dir, midiset_t *sets, int max)
{
  int i, n = 0;
  char path[256];

  if (!data_dir || !data_dir[0]) data_dir = "data";

  for (i = 0; i < MIDISET_KNOWN_N && n < max; ++i)
  {
    int mc;
    snprintf(path, sizeof(path), "%s/%s", data_dir, MIDISET_KNOWN[i].dir);
    mc = count_mid_files(path);
    if (mc < 1) continue; /* dir absent or no playable .mid -- never offer it */

    snprintf(sets[n].value, sizeof(sets[n].value), "%s", MIDISET_KNOWN[i].value);
    snprintf(sets[n].dir,   sizeof(sets[n].dir),   "%s", MIDISET_KNOWN[i].dir);
    snprintf(sets[n].label, sizeof(sets[n].label), "%s", MIDISET_KNOWN[i].label);
    sets[n].mid_count = mc;
    ++n;
  }
  return n;
}

int midiset_index_by_value(const midiset_t *sets, int n, const char *value)
{
  int i;
  if (!value || !value[0]) value = MIDISET_DEFAULT_VALUE;
  for (i = 0; i < n; ++i)
  {
    /* exact ASCII case-insensitive match */
    const char *a = sets[i].value, *b = value;
    int eq = 1;
    while (*a && *b)
    {
      char ca = *a, cb = *b;
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
      if (ca != cb) { eq = 0; break; }
      ++a; ++b;
    }
    if (eq && *a == '\0' && *b == '\0') return i;
  }
  return -1;
}
