/*
 * bindings.c -- SETUP-side control-binding session model (see bindings.h).
 *
 * ASCII-only, pure C, host-testable.
 */

#include "bindings.h"
#include "scancode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h> /* snprintf -- compose the BIND_* value string */

/* On-disk base for the b:<n> joystick-button token. The engine stores 0-based
 * buttons (SDL evt.jbutton.button); whether the CFG token is 0- or 1-based is
 * input-eng's parser contract. PROVISIONAL: 0-based (write the raw button so no
 * +/-1 conversion is needed in the loader). Change this ONE constant to match
 * the published contract. The UI always shows 1-based ("Button 1".."Button 4")
 * regardless -- that is purely a display affordance, computed at render time. */
#define BIND_JBUT_DISK_BASE 0

/* Action table: cfg_key + display name, in engine INPUTS-enum order (0..10).
 * The cfg_key spellings track INPUT-REMAP-PLAN Part E; centralizing them here
 * means re-pointing to input-eng's published names is a one-table edit. */
static const struct { const char *cfg_key; const char *name; const char *defkey; }
ACTIONS[BIND_COUNT] =
{
  { "BIND_LEFT",      "Left",      "Left"  },
  { "BIND_RIGHT",     "Right",     "Right" },
  { "BIND_UP",        "Up",        "Up"    },
  { "BIND_DOWN",      "Down",      "Down"  },
  { "BIND_JUMP",      "Jump",      "Z"     },
  { "BIND_FIRE",      "Fire",      "X"     },
  { "BIND_STRAFE",    "Strafe",    "C"     },
  { "BIND_PREVWPN",   "Wpn Prev",  "A"     },
  { "BIND_NEXTWPN",   "Wpn Next",  "S"     },
  { "BIND_INVENTORY", "Inventory", "Q"     },
  { "BIND_MAP",       "Map",       "W"     },
};

/* Q-B2 default joystick buttons: Jump/Fire/WpnPrev/WpnNext = buttons 0..3,
 * everything else <None> (-1). Indices are into ACTIONS (= INPUTS enum). */
static int default_jbut(int action)
{
  switch (action)
  {
    case 4: return 0; /* Jump     -> Button 1 */
    case 5: return 1; /* Fire     -> Button 2 */
    case 7: return 2; /* Wpn Prev -> Button 3 */
    case 8: return 3; /* Wpn Next -> Button 4 */
    default: return -1;
  }
}

void bindings_defaults(binding_t *b)
{
  int i;
  for (i = 0; i < BIND_COUNT; ++i)
  {
    const setup_key_t *k = scancode_by_name(ACTIONS[i].defkey);
    b[i].cfg_key = ACTIONS[i].cfg_key;
    b[i].name    = ACTIONS[i].name;
    b[i].keycode = k ? k->keycode : -1;
    b[i].jbut    = default_jbut(i);
  }
}

/* Parse a BIND_* value "k:<name>[,b:<n>]" into b[i] (already defaulted). */
static void parse_bind(binding_t *bi, const char *val)
{
  const char *p = val;
  while (*p)
  {
    if ((p[0] == 'k' || p[0] == 'K') && p[1] == ':')
    {
      /* k:<sdlkeycode> -- a DECIMAL SDL3 keycode (engine contract, patch 0227
       * _parse_cfg_bind reads it with strtol). Store the value directly; the
       * display looks it up in the scancode table. */
      char *end = NULL;
      long v = strtol(p + 2, &end, 10);
      if (end != p + 2) bi->keycode = v;
      p = (end && end > p + 2) ? end : p + 2;
      while (*p && *p != ',') ++p;
    }
    else if ((p[0] == 'b' || p[0] == 'B') && p[1] == ':')
    {
      int v;
      p += 2;
      v = atoi(p) - BIND_JBUT_DISK_BASE;
      if (v >= 0 && v < BIND_NJBUT) bi->jbut = v;
      else                          bi->jbut = -1;
      while (*p && *p != ',') ++p;
    }
    else
    {
      ++p;
    }
    if (*p == ',') ++p;
  }
}

void bindings_load(binding_t *b, const scfg_t *c)
{
  int i;
  bindings_defaults(b);
  for (i = 0; i < BIND_COUNT; ++i)
  {
    int idx = scfg_index(b[i].cfg_key);
    if (idx >= 0)
    {
      const char *v = scfg_get(c, idx);
      if (v && v[0])
      {
        /* A present BIND_ line is authoritative for BOTH fields: SETUP always
         * writes b: when a button is assigned, so the absence of b: means the
         * action has no joystick button. Clear it before parsing so a "k:120"
         * line reads back as <None>, not the factory default button. */
        b[i].jbut = -1;
        parse_bind(&b[i], v);
      }
    }
  }
}

int bindings_save(const binding_t *b, scfg_t *c)
{
  int i, written = 0;
  for (i = 0; i < BIND_COUNT; ++i)
  {
    int idx = scfg_index(b[i].cfg_key);
    char val[40], tok[24];
    const setup_key_t *k;
    if (idx < 0) continue; /* registry has not (yet) defined this BIND_* key */
    k = scancode_by_keycode(b[i].keycode);
    scancode_bind_token(tok, sizeof(tok), k);
    if (b[i].jbut >= 0)
      snprintf(val, sizeof(val), "k:%s,b:%d", tok, b[i].jbut + BIND_JBUT_DISK_BASE);
    else
      snprintf(val, sizeof(val), "k:%s", tok);
    scfg_set(c, idx, val);
    ++written;
  }
  return written;
}

int bindings_find_key(const binding_t *b, long keycode, int except)
{
  int i;
  for (i = 0; i < BIND_COUNT; ++i)
    if (i != except && b[i].keycode == keycode)
      return i;
  return -1;
}

int bindings_find_jbut(const binding_t *b, int jbut, int except)
{
  int i;
  if (jbut < 0) return -1;
  for (i = 0; i < BIND_COUNT; ++i)
    if (i != except && b[i].jbut == jbut)
      return i;
  return -1;
}
