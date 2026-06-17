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

/* True if <data_dir>/<name> is a directory. */
static int is_subdir(const char *data_dir, const char *name)
{
  char path[256];
  struct stat st;
  snprintf(path, sizeof(path), "%s/%s", data_dir, name);
  if (stat(path, &st) != 0) return 0;
  return (st.st_mode & S_IFDIR) ? 1 : 0;
}

/* True if `dir` is one of the KNOWN-table data subdir names (case-insensitive). */
static int is_known_dir(const char *dir)
{
  int i;
  for (i = 0; i < MIDISET_KNOWN_N; ++i)
  {
    const char *a = MIDISET_KNOWN[i].dir, *b = dir;
    int eq = 1;
    while (*a && *b)
    {
      char ca = *a, cb = *b;
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
      if (ca != cb) { eq = 0; break; }
      ++a; ++b;
    }
    if (eq && *a == '\0' && *b == '\0') return 1;
  }
  return 0;
}

/* Append discovered CUSTOM drop-in dirs (#39b): any data/ subdir holding >=1
 * .mid that is not a known set. The engine's #39b passthrough (patch 0226)
 * loads data/<dir>/<name>.mid when MIDI_SET=<dir>; here we surface them so the
 * user can pick their own set. Friendly label "Custom (<dir>)". Returns the
 * new total count. */
static int scan_custom_dirs(const char *data_dir, midiset_t *sets, int n, int max)
{
  DIR *d;
  struct dirent *e;
  char path[256];

  d = opendir(data_dir);
  if (!d) return n;
  while ((e = readdir(d)) != NULL && n < max)
  {
    int mc;
    if (e->d_name[0] == '.') continue;       /* skip ., .., dotfiles      */
    /* Reject names that cannot round-trip a DOS 8.3 dir / the value field.
     * Real-HW readdir returns <=8-char dir names; this also keeps the
     * fixed-width copies below truncation-free. */
    if (strlen(e->d_name) >= MIDISET_DIR_MAX) continue;
    if (is_known_dir(e->d_name)) continue;    /* known sets handled above  */
    if (!is_subdir(data_dir, e->d_name)) continue;

    snprintf(path, sizeof(path), "%s/%.*s",
             data_dir, (int)(MIDISET_DIR_MAX - 1), e->d_name);
    mc = count_mid_files(path);
    if (mc < 1) continue;                     /* not a playable MIDI set   */

    snprintf(sets[n].value, sizeof(sets[n].value), "%.*s",
             (int)(sizeof(sets[n].value) - 1), e->d_name);
    snprintf(sets[n].dir,   sizeof(sets[n].dir),   "%.*s",
             (int)(sizeof(sets[n].dir) - 1), e->d_name);
    /* "Custom (" (8) + dir + ")" (1) must fit MIDISET_LABEL_MAX-1; bound the
     * dir portion so the label never truncates mid-name. */
    snprintf(sets[n].label, sizeof(sets[n].label), "Custom (%.*s)",
             (int)(MIDISET_LABEL_MAX - 1 - 9), e->d_name);
    sets[n].mid_count = mc;
    ++n;
  }
  closedir(d);
  return n;
}

int midiset_scan(const char *data_dir, midiset_t *sets, int max)
{
  int i, n = 0;

  if (!data_dir || !data_dir[0]) data_dir = "data";

  for (i = 0; i < MIDISET_KNOWN_N && n < max; ++i)
  {
    char path[256];
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

  /* #39b: append any user drop-in dirs after the known sets. */
  n = scan_custom_dirs(data_dir, sets, n, max);
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
