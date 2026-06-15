#ifndef SETUP_SETUPCFG_H
#define SETUP_SETUPCFG_H

/*
 * setupcfg.h -- SETUP.EXE side of DOSKUTSU.CFG: an in-memory value set
 * keyed by the shared registry (include/doskutsu_config_keys.h), with
 * load / save / get / set helpers. The engine LOADS this file (via the
 * patch-0210 shim); SETUP WRITES it. Both agree on keys through the one
 * shared registry, so names can never drift.
 *
 * ASCII-only, C89-friendly (DJGPP). No SDL dependency.
 */

#include "doskutsu_config_keys.h"

#define SCFG_VAL_MAX 64

typedef struct
{
  /* values[i] is the current string value for DKT_KEYS[i]; initialized to
   * the registry default, overwritten by load, edited by the UI. */
  char values[DKT_KEY_COUNT][SCFG_VAL_MAX];
} scfg_t;

/* Reset every key to its registry default. */
void scfg_defaults(scfg_t *c);

/* Load DOSKUTSU.CFG at path into c (defaults first, then file overrides).
 * Returns number of recognized keys read, or -1 if the file is absent. */
int scfg_load(scfg_t *c, const char *path);

/* Write c to DOSKUTSU.CFG at path. An AUDIO_BACKEND of "auto" is omitted
 * so the engine's auto-detect chain runs unchanged. Returns 0 on success,
 * -1 on a write error. */
int scfg_save(const scfg_t *c, const char *path);

/* Get / set by registry index (0..DKT_KEY_COUNT-1). */
const char *scfg_get(const scfg_t *c, int idx);
void        scfg_set(scfg_t *c, int idx, const char *val);

/* Convenience: index of a key by cfg_key, or -1. */
int scfg_index(const char *cfg_key);

/* 1 if any key's value differs between a and b, else 0. Compares the VALUE
 * STRINGS (not raw bytes), so it is robust against stale post-NUL buffer
 * content after a shrink. Used by the SETUP screens to detect whether the
 * session changed on a screen (T52 ESC "Save setting?" prompt) without false
 * positives from a change-then-revert. */
int scfg_differs(const scfg_t *a, const scfg_t *b);

#endif /* SETUP_SETUPCFG_H */
