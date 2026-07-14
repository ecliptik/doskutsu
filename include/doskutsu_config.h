#ifndef DOSKUTSU_CONFIG_H
#define DOSKUTSU_CONFIG_H

/*
 * doskutsu_config.h -- DOSKUTSU.CFG loader (engine side).
 *
 * Parses a line-based KEY=VALUE config file and publishes each recognized
 * key into the process environment via setenv(env_name, value, 0). Because
 * overwrite=0, a real DOS `SET` already in the environment is never
 * clobbered -- precedence stays env > file > built-in default.
 *
 * Called once, as the very first thing in main(), BEFORE TAS::init /
 * SDL_Init / any getenv / SDL_GetHint read of a config key. With no file
 * present the function is a no-op and the engine behaves byte-identically
 * to the no-config build (the built-in defaults already encode the shipped
 * production config).
 *
 * Header-only so it is unit-testable outside the patch and shares the key
 * registry with SETUP.EXE. ASCII-only; C / C++ compatible.
 *
 * Returns the number of keys applied, or -1 if the file could not be
 * opened (no config present -- the normal first-run case).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doskutsu_config_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* setenv() is POSIX, not ISO C/C++. DJGPP's <stdlib.h> guards it behind
 * !__STRICT_ANSI__; the engine compiles -std=gnu++11 (non-strict) so it is
 * already visible there. Declare it defensively so this shared header also
 * compiles under a strict -std=c++11 / -std=c11 TU (e.g. a unit test). */
#if defined(__STRICT_ANSI__)
extern int setenv(const char *, const char *, int);
#endif

/* DJGPP / DOS: binary mode is mandatory (the text-mode CRLF rule). We strip
 * a trailing CR ourselves so a CRLF-authored file still parses. */
#define DKT_CFG_DEFAULT_PATH "DOSKUTSU.CFG"
#define DKT_CFG_LINE_MAX     256

static inline int dkt_is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* ---- patch 0279: env-override reporting --------------------------------
 * The env > file precedence above is deliberate, but it is SILENT: setenv()
 * with overwrite=0 returns 0 (SUCCESS) even when it does nothing, so a key
 * whose file value was discarded still counts toward the "loaded (N keys)"
 * banner. A stale DOS `SET` therefore defeats a SETUP-saved setting with no
 * trace anywhere in the log. That cost us a real-HW iter (2026-07-13): the
 * CFG said MIDI_SET=orgmid2, a leftover session SET said wiimidi, the engine
 * obeyed the env, and nothing said so.
 *
 * doskutsu_cfg_load_ex() optionally records each such key so a caller with a
 * logger (the engine) can narrate it. The header itself stays plain C and
 * logger-free -- SETUP's unit test compiles it under -Wall -Wextra -Werror.
 *
 * key / env_name point into the static key registry (stable for the process
 * lifetime, no copy needed). The VALUES are copied: getenv() returns a
 * pointer into the environment block, which a later setenv() may relocate. */
#define DKT_OVR_VAL_MAX 64

/* Suggested caller buffer size. 8 is far above the realistic case (an
 * operator with one or two stale SETs); a caller that overflows it still
 * learns the true total from *ovr_count and can say so. */
#define DKT_CFG_OVERRIDE_MAX 8

typedef struct
{
  const char *key;      /* CFG key name, e.g. "MIDI_SET"                  */
  const char *env_name; /* env var that won, e.g. "SDL_HINT_..._SOURCE"   */
  char file_val[DKT_OVR_VAL_MAX]; /* the value in DOSKUTSU.CFG (ignored)  */
  char env_val[DKT_OVR_VAL_MAX];  /* the pre-existing env value (in force) */
} dkt_override_t;

static inline void dkt_copy_trunc(char *dst, int dstsz, const char *src)
{
  int n = 0;
  if (dstsz <= 0)
    return;
  while (src[n] != '\0' && n < dstsz - 1)
  {
    dst[n] = src[n];
    ++n;
  }
  dst[n] = '\0';
}

/* Full loader. ovr/ovr_max/ovr_count may be NULL/0 -- then this behaves
 * exactly like the historic doskutsu_cfg_load() (which is now a wrapper).
 * *ovr_count reports the number of overrides DETECTED, which may exceed
 * ovr_max; at most ovr_max are recorded, so a caller can report the drop
 * instead of silently truncating. */
static inline int doskutsu_cfg_load_ex(const char *path, dkt_override_t *ovr,
                                int ovr_max, int *ovr_count)
{
  FILE *f;
  char  line[DKT_CFG_LINE_MAX];
  int   applied = 0;
  int   n_ovr   = 0;

  if (ovr_count != NULL)
    *ovr_count = 0;

  if (path == NULL || path[0] == '\0')
    path = DKT_CFG_DEFAULT_PATH;

  f = fopen(path, "rb"); /* DOS-PORT: always "rb" -- never text mode */
  if (f == NULL)
    return -1;

  while (fgets(line, (int)sizeof(line), f) != NULL)
  {
    char *p = line;
    char *key_start, *key_end;
    char *val_start, *val_end;
    char *eq;
    const dkt_key_t *entry;
    char  valbuf[DKT_CFG_LINE_MAX];
    int   vlen;

    /* skip leading whitespace */
    while (*p && dkt_is_space(*p)) ++p;
    /* comment or blank line */
    if (*p == '\0' || *p == ';' || *p == '#')
      continue;

    eq = strchr(p, '=');
    if (eq == NULL)
      continue; /* malformed -- no '=' */

    /* key token = [key_start, key_end) with trailing space trimmed */
    key_start = p;
    key_end   = eq;
    while (key_end > key_start && dkt_is_space(key_end[-1]))
      --key_end;
    if (key_end == key_start)
      continue; /* empty key */

    entry = dkt_lookup(key_start, (int)(key_end - key_start));
    if (entry == NULL)
      continue; /* unknown / non-user-facing key: ignore (forward-compat) */

    /* SETUP-only keys carry a NULL env_name (e.g. SPEED_CLASS -- a provenance
     * record for the System Speed preset). The engine never reads them, so
     * skip here, and crucially never setenv(NULL, ...). Behavior-neutral for
     * every engine-consumed key (all of those have a non-NULL env_name). */
    if (entry->env_name == NULL)
      continue;

    /* value = everything after '=', trimmed both ends */
    val_start = eq + 1;
    while (*val_start && dkt_is_space(*val_start)) ++val_start;
    val_end = val_start + strlen(val_start);
    while (val_end > val_start && dkt_is_space(val_end[-1]))
      --val_end;

    vlen = (int)(val_end - val_start);
    if (vlen < 0) vlen = 0;
    if (vlen >= (int)sizeof(valbuf)) vlen = (int)sizeof(valbuf) - 1;
    memcpy(valbuf, val_start, (size_t)vlen);
    valbuf[vlen] = '\0';

    /* Is this key's file value about to lose to a pre-existing env var?
     * (patch 0279 -- detection only; the precedence itself is unchanged.)
     *
     * Checked BEFORE the DKT_F_PRESENCE skip below, because that skip is
     * itself a silent loss: CFG says KEY=0 (feature off) while the env var
     * merely EXISTS (feature on) -- the env wins and the user's "off" is
     * discarded. For a presence key the disagreement is about PRESENCE, not
     * text, so an env value of "0" still contradicts a file value of "0"
     * (present = on). Value-compare only makes sense for the other keys.
     *
     * AUTHORITATIVE keys (BLASTER) setenv with overwrite=1, so the file WINS
     * and nothing is lost -- never reported. Equal values are not reported
     * either: an env var that agrees with the file changes no behavior, and
     * iter BATs routinely re-assert the value they already configured. Only
     * a genuine, behavior-changing divergence is recorded. */
    {
      const char *env_cur    = getenv(entry->env_name);
      const int   presence_off =
          (entry->flags & DKT_F_PRESENCE) &&
          (valbuf[0] == '\0' || (valbuf[0] == '0' && valbuf[1] == '\0'));

      if (env_cur != NULL && !(entry->flags & DKT_F_AUTHORITATIVE) &&
          (presence_off || strcmp(env_cur, valbuf) != 0))
      {
        if (ovr != NULL && n_ovr < ovr_max)
        {
          ovr[n_ovr].key      = entry->cfg_key;
          ovr[n_ovr].env_name = entry->env_name;
          dkt_copy_trunc(ovr[n_ovr].file_val, DKT_OVR_VAL_MAX, valbuf);
          dkt_copy_trunc(ovr[n_ovr].env_val, DKT_OVR_VAL_MAX, env_cur);
        }
        ++n_ovr; /* counts DETECTED, even past ovr_max -- no silent cap */
      }

      /* Presence-checked keys (DKT_F_PRESENCE): the engine treats the mere
       * PRESENCE of the env var as ON (e.g. getenv("DOSKUTSU_NO_AUDIO") !=
       * NULL disables audio regardless of value). For these, a "0" or empty
       * value must leave the var ABSENT -- otherwise a saved config with
       * KEY=0 would wrongly enable the feature. Skip the setenv so "off"
       * stays absent; a real env SET still wins because we never touch it. */
      if (presence_off)
        continue;
    }

    /* Precedence:
     *  - DKT_F_AUTHORITATIVE keys (BLASTER) setenv overwrite=1 -> the config
     *    value WINS over an ambient env / AUTOEXEC SET (file > env). The
     *    operator decided SETUP's sound-hardware choice is authoritative.
     *  - all other keys setenv overwrite=0 -> a real DOS `SET` still wins
     *    (env > file > built-in default), the original tuning-key contract. */
    {
      int overwrite = (entry->flags & DKT_F_AUTHORITATIVE) ? 1 : 0;
      if (setenv(entry->env_name, valbuf, overwrite) == 0)
        ++applied;
    }
  }

  fclose(f);

  if (ovr_count != NULL)
    *ovr_count = n_ovr;
  return applied;
}

/* Historic entry point -- unchanged signature, unchanged behavior. Every
 * existing caller (SETUP's unit test, the engine pre-0279) keeps working;
 * this also keeps _ex "used" so the shared header stays -Wunused-function
 * clean in a TU that only calls the simple form. */
static inline int doskutsu_cfg_load(const char *path)
{
  return doskutsu_cfg_load_ex(path, NULL, 0, NULL);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOSKUTSU_CONFIG_H */
