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

static int dkt_is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int doskutsu_cfg_load(const char *path)
{
  FILE *f;
  char  line[DKT_CFG_LINE_MAX];
  int   applied = 0;

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

    /* Presence-checked keys (DKT_F_PRESENCE): the engine treats the mere
     * PRESENCE of the env var as ON (e.g. getenv("DOSKUTSU_NO_AUDIO") !=
     * NULL disables audio regardless of value). For these, a "0" or empty
     * value must leave the var ABSENT -- otherwise a saved config with
     * KEY=0 would wrongly enable the feature. Skip the setenv so "off"
     * stays absent; a real env SET still wins because we never touch it. */
    if ((entry->flags & DKT_F_PRESENCE) &&
        (valbuf[0] == '\0' || (valbuf[0] == '0' && valbuf[1] == '\0')))
      continue;

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
  return applied;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOSKUTSU_CONFIG_H */
