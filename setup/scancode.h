#ifndef SETUP_SCANCODE_H
#define SETUP_SCANCODE_H

/*
 * scancode.h -- BIOS-scancode <-> SDL3-keycode table for SETUP's keyboard
 * remap screen (Phase 3, plan SETUP-DF-UX 3.5a / INPUT-REMAP-PLAN Part S).
 *
 * SETUP reads keystrokes through the BIOS (INT 16h via conio getch): a
 * printable key yields its ASCII byte; an extended key (arrows, etc.) yields
 * a 0x00/0xE0 prefix then a BIOS scancode. The engine, however, binds SDL3
 * KEYCODES (input.cpp: mappings[i].key compared against SDL_KeyboardEvent.key).
 * This module is the bridge: it decodes a BIOS getch() result into the SDL key
 * that the engine will match, and formats the token SETUP writes into the
 * BIND_<ACTION> config value.
 *
 * The table is built on FIXED SDL3 facts (keycode numeric values + the
 * canonical SDL_GetKeyName strings), so it is independent of the engine's
 * BIND_* value grammar -- only scancode_bind_token() depends on the exact
 * contract (input-eng owns include/doskutsu_config_keys.h + the loader parser).
 *
 * Pure C89-friendly, no SDL/DJGPP dependency, host-testable. ASCII-only.
 */

/* A remappable key, named in two registers: the engine-facing SDL identity and
 * the player-facing display string. */
typedef struct
{
  long        keycode;  /* SDL3 numeric keycode (stable ABI value)            */
  const char *sdl_name; /* canonical SDL_GetKeyName string ("Z","Left",...)   */
  const char *display;  /* human label for the UI ("Z","Left Arrow","Space") */
} setup_key_t;

/* Modifier identities captured from the BIOS shift-flag byte (0040:0017) during
 * the keyboard-remap capture loop (issue 1). The BIOS keystroke path (INT 16h
 * getch) never delivers a LONE modifier, so a bare Shift/Ctrl/Alt press is read
 * straight from the shift byte instead. Note 0040:0017 carries a single Ctrl and
 * a single Alt bit (no left/right split), so Ctrl folds to Left Ctrl and Alt to
 * Left Alt -- the engine-side decision (team-lead 2026-06-16). */
enum
{
  SETUP_MOD_NONE = 0,
  SETUP_MOD_RSHIFT,   /* 0040:0017 bit0 -> SDLK_RSHIFT */
  SETUP_MOD_LSHIFT,   /* 0040:0017 bit1 -> SDLK_LSHIFT */
  SETUP_MOD_CTRL,     /* 0040:0017 bit2 -> SDLK_LCTRL  */
  SETUP_MOD_ALT       /* 0040:0017 bit3 -> SDLK_LALT   */
};

/* Decode a BIOS getch() result into an SDL key.
 *   ext  0 -> `code` is the ASCII byte getch() returned for a printable key;
 *        1 -> `code` is the BIOS extended scancode (the byte after a 0x00/0xE0
 *             prefix) for an arrow / nav / function key.
 * Shifted input is normalized to its physical key (US layout): an uppercase
 * letter folds to its lowercase keycode, and a shifted punctuation glyph folds
 * to its base key, matching the SDL keycode the engine sees (SDL3 reports the
 * unmodified base keycode in SDL_KeyboardEvent.key). Returns a pointer to a
 * static setup_key_t, or NULL if the key is not in the remappable set (the
 * caller then shows "unsupported key -- try another"). */
const setup_key_t *scancode_decode(int ext, int code);

/* Look up a key by its SDL numeric keycode -- used to render the current
 * binding of an action. NULL if not in the table. */
const setup_key_t *scancode_by_keycode(long keycode);

/* Resolve a captured modifier (a SETUP_MOD_* value, non-NONE) into its key.
 * Returns NULL for SETUP_MOD_NONE or an out-of-range value. The keycode is the
 * SDL3 modifier keycode the engine matches (Ctrl/Alt fold to the LEFT side). */
const setup_key_t *scancode_modifier(int mod);

/* Look up a key by its canonical SDL name (case-insensitive) -- used to parse
 * a BIND_* value back into a display row. NULL if unknown. */
const setup_key_t *scancode_by_name(const char *sdl_name);

/* The remappable-key table + its length (for tests / enumeration). */
const setup_key_t *scancode_table(int *count);

/* Format the key token that goes after "k:" in a BIND_<ACTION> value, e.g.
 * "z" or "left". The exact spelling is governed by the engine parser contract
 * (input-eng); this is the single place that depends on it, so re-pointing to
 * the published contract is a one-function change. Writes into buf (cap bytes)
 * and returns buf. */
const char *scancode_bind_token(char *buf, int cap, const setup_key_t *k);

#endif /* SETUP_SCANCODE_H */
