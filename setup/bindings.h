#ifndef SETUP_BINDINGS_H
#define SETUP_BINDINGS_H

/*
 * bindings.h -- SETUP-side control-binding session model (Phase 3).
 *
 * Holds the per-action keyboard key + joystick button the Configure Keyboard /
 * Configure Joystick screens edit. Loads from / saves to DOSKUTSU.CFG BIND_*
 * keys via the shared registry (include/doskutsu_config_keys.h) -- but ONLY for
 * BIND_* keys that the registry actually defines. Until input-eng publishes the
 * BIND_* entries + the engine loader, save is a no-op and the model simply
 * carries the session defaults, so the screens are fully developable now and
 * begin persisting the moment the registry gains the keys (no SETUP change).
 *
 * The action list + order mirror the engine's INPUTS enum 0..10
 * (vendor/nxengine-evo/src/input.h LEFTKEY..MAPSYSTEMKEY); display names mirror
 * input_get_name() so SETUP and the in-game remap menu agree.
 *
 * ASCII-only, pure C, host-testable. Depends only on setupcfg + scancode.
 */

#include "setupcfg.h"

#define BIND_COUNT 11   /* the 11 player actions (LEFT..MAP)        */
#define BIND_NJBUT 4    /* SDL3-DOS gameport: 4 buttons (0..3)      */

typedef struct
{
  const char *cfg_key; /* "BIND_LEFT".. ; the DOSKUTSU.CFG key (engine registry) */
  const char *name;    /* UI label ("Left","Jump",..), == input_get_name()       */
  long        keycode; /* current SDL keycode, or -1 = unbound                    */
  int         jbut;    /* current joystick button [0,BIND_NJBUT), or -1 = <None>  */
} binding_t;

/* Reset all bindings to the engine defaults: arrows + Z/X/C/A/S/Q/W (input.cpp)
 * and the Q-B2 joystick defaults Jump/Fire/WpnPrev/WpnNext = buttons 0..3. */
void bindings_defaults(binding_t *b);

/* Populate b from DOSKUTSU.CFG values in c (BIND_* keys), starting from the
 * engine defaults so any key the config omits keeps its default. A malformed or
 * absent BIND_* value leaves that action at its default. */
void bindings_load(binding_t *b, const scfg_t *c);

/* Write b back into c as BIND_<ACTION>=k:<key>[,b:<jbut>] for every action
 * whose cfg_key the registry defines (scfg_index >= 0). A no-op for actions the
 * registry does not yet carry. Returns the number of keys written. */
int bindings_save(const binding_t *b, scfg_t *c);

/* Index of the action currently bound to `keycode` (keyboard conflict check),
 * or -1 if none. Skips index `except` (the row being edited). */
int bindings_find_key(const binding_t *b, long keycode, int except);

/* Index of the action currently assigned joystick button `jbut`, or -1 if none.
 * Skips index `except`. */
int bindings_find_jbut(const binding_t *b, int jbut, int except);

#endif /* SETUP_BINDINGS_H */
