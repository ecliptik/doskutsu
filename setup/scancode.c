/*
 * scancode.c -- BIOS-scancode <-> SDL3-keycode table (see scancode.h).
 *
 * ASCII-only, pure C, host-testable. No SDL or DJGPP headers: the SDL keycode
 * numeric values are reproduced here as plain constants so the table compiles
 * and unit-tests on the host without the SDL3 stack.
 */

#include "scancode.h"
#include <stddef.h>
#include <stdio.h> /* snprintf -- format the numeric keycode token */

/* SDL3 keycodes. Printable keys ARE their ASCII codepoint (letters lowercase,
 * digits + punctuation the unshifted glyph). Non-printable + nav keys carry the
 * SDL scancode mask (1<<30). Reproduced as literals to keep this TU SDL-free. */
#define SK_MASK    0x40000000L
#define SK_RIGHT   (SK_MASK + 79)
#define SK_LEFT    (SK_MASK + 80)
#define SK_DOWN    (SK_MASK + 81)
#define SK_UP      (SK_MASK + 82)
/* Modifier keycodes (SDL_SCANCODE_* + the keycode mask). 0040:0017 has no
 * left/right split for Ctrl/Alt, so only the LEFT variants are captured; the
 * RIGHT-Shift variant IS distinguished by the BIOS byte (bit0). */
#define SK_LCTRL   (SK_MASK + 224)
#define SK_LSHIFT  (SK_MASK + 225)
#define SK_LALT    (SK_MASK + 226)
#define SK_RSHIFT  (SK_MASK + 229)

/* The remappable key set: arrows, letters, digits, common punctuation, the
 * spelled-out keys (Space/Tab/Backspace), and the modifiers (Shift/Ctrl/Alt).
 * Modifiers cannot arrive through the BIOS getch() path (it never reports a lone
 * modifier), so they are captured from the BIOS shift-flag byte instead (see
 * tui_capture_key_mod + scancode_modifier) -- they are in this table only so the
 * UI can DISPLAY a modifier binding and parse one back from a BIND_* value.
 * ENTER ('\r') is deliberately EXCLUDED (issue 2): it is the row-confirm key of
 * the Configure-Keyboard screen, so it must not be bindable. Function + nav keys
 * are excluded too (the engine reserves F1-F12 for debug); only keys SETUP can
 * round-trip are listed, keeping the bind/display path closed. */
static const setup_key_t KEYS[] =
{
  /* arrows */
  { SK_LEFT,  "Left",  "Left Arrow"  },
  { SK_RIGHT, "Right", "Right Arrow" },
  { SK_UP,    "Up",    "Up Arrow"    },
  { SK_DOWN,  "Down",  "Down Arrow"  },

  /* modifiers (captured via the BIOS shift byte, not getch) */
  { SK_LSHIFT, "Left Shift",  "L Shift" },
  { SK_RSHIFT, "Right Shift", "R Shift" },
  { SK_LCTRL,  "Left Ctrl",   "Ctrl"    },
  { SK_LALT,   "Left Alt",    "Alt"     },

  /* spelled-out keys */
  { ' ',  "Space",     "Space"     },
  { '\t', "Tab",       "Tab"       },
  { '\b', "Backspace", "Backspace" },

  /* letters (SDL keycode == lowercase ASCII; SDL_GetKeyName is uppercase) */
  { 'a', "A", "A" }, { 'b', "B", "B" }, { 'c', "C", "C" }, { 'd', "D", "D" },
  { 'e', "E", "E" }, { 'f', "F", "F" }, { 'g', "G", "G" }, { 'h', "H", "H" },
  { 'i', "I", "I" }, { 'j', "J", "J" }, { 'k', "K", "K" }, { 'l', "L", "L" },
  { 'm', "M", "M" }, { 'n', "N", "N" }, { 'o', "O", "O" }, { 'p', "P", "P" },
  { 'q', "Q", "Q" }, { 'r', "R", "R" }, { 's', "S", "S" }, { 't', "T", "T" },
  { 'u', "U", "U" }, { 'v', "V", "V" }, { 'w', "W", "W" }, { 'x', "X", "X" },
  { 'y', "Y", "Y" }, { 'z', "Z", "Z" },

  /* digits */
  { '0', "0", "0" }, { '1', "1", "1" }, { '2', "2", "2" }, { '3', "3", "3" },
  { '4', "4", "4" }, { '5', "5", "5" }, { '6', "6", "6" }, { '7', "7", "7" },
  { '8', "8", "8" }, { '9', "9", "9" },

  /* common punctuation (SDL keycode + name == the unshifted glyph) */
  { '-',  "-",  "-"  }, { '=',  "=",  "="  }, { '[',  "[",  "["  },
  { ']',  "]",  "]"  }, { '\\', "\\", "\\" }, { ';',  ";",  ";"  },
  { '\'', "'",  "'"  }, { '`',  "`",  "`"  }, { ',',  ",",  ","  },
  { '.',  ".",  "."  }, { '/',  "/",  "/"  },
};
#define KEYS_N ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

/* BIOS extended scancode -> SDL keycode, for the arrow keys (the only extended
 * keys in the remappable set). getch() returns 0x00/0xE0 then one of these. */
static const struct { int scan; long keycode; } EXT[] =
{
  { 0x48, SK_UP }, { 0x50, SK_DOWN }, { 0x4B, SK_LEFT }, { 0x4D, SK_RIGHT },
};
#define EXT_N ((int)(sizeof(EXT) / sizeof(EXT[0])))

/* US-layout shifted-punctuation glyph -> its base (unshifted) ASCII key, so a
 * Shift+key press resolves to the same SDL keycode the engine matches. */
static int shift_to_base(int c)
{
  switch (c)
  {
    case '!': return '1'; case '@': return '2'; case '#': return '3';
    case '$': return '4'; case '%': return '5'; case '^': return '6';
    case '&': return '7'; case '*': return '8'; case '(': return '9';
    case ')': return '0';
    case '_': return '-'; case '+': return '=';
    case '{': return '['; case '}': return ']'; case '|': return '\\';
    case ':': return ';'; case '"': return '\'';
    case '~': return '`'; case '<': return ','; case '>': return '.';
    case '?': return '/';
    default:  return c;
  }
}

const setup_key_t *scancode_by_keycode(long keycode)
{
  int i;
  for (i = 0; i < KEYS_N; ++i)
    if (KEYS[i].keycode == keycode)
      return &KEYS[i];
  return NULL;
}

const setup_key_t *scancode_modifier(int mod)
{
  long kc;
  switch (mod)
  {
    case SETUP_MOD_RSHIFT: kc = SK_RSHIFT; break;
    case SETUP_MOD_LSHIFT: kc = SK_LSHIFT; break;
    case SETUP_MOD_CTRL:   kc = SK_LCTRL;  break; /* no L/R split in 0040:0017 */
    case SETUP_MOD_ALT:    kc = SK_LALT;   break;
    default:               return NULL;           /* SETUP_MOD_NONE / invalid */
  }
  return scancode_by_keycode(kc);
}

static int ascii_ieq(const char *a, const char *b)
{
  for (; *a && *b; ++a, ++b)
  {
    char ca = *a, cb = *b;
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return *a == '\0' && *b == '\0';
}

const setup_key_t *scancode_by_name(const char *sdl_name)
{
  int i;
  if (!sdl_name) return NULL;
  for (i = 0; i < KEYS_N; ++i)
    if (ascii_ieq(KEYS[i].sdl_name, sdl_name))
      return &KEYS[i];
  return NULL;
}

const setup_key_t *scancode_decode(int ext, int code)
{
  if (ext)
  {
    int i;
    for (i = 0; i < EXT_N; ++i)
      if (EXT[i].scan == code)
        return scancode_by_keycode(EXT[i].keycode);
    return NULL;
  }

  /* printable byte from getch() */
  if (code >= 'A' && code <= 'Z')          /* uppercase letter -> lowercase key */
    code = code - 'A' + 'a';
  else
    code = shift_to_base(code);            /* shifted punctuation -> base key   */

  return scancode_by_keycode((long)code);
}

const setup_key_t *scancode_table(int *count)
{
  if (count) *count = KEYS_N;
  return KEYS;
}

const char *scancode_bind_token(char *buf, int cap, const setup_key_t *k)
{
  if (cap <= 0) return buf;
  buf[0] = '\0';
  if (!k) return buf;
  /* Contract (team-lead decision, final 2026-06-16): the token after "k:" is
   * the SDL3 keycode as a DECIMAL integer (e.g. 122 = SDLK_Z,
   * 1073741904 = SDLK_LEFT). The engine parser (patch 0227 _parse_cfg_bind)
   * reads it with strtol -- dependency-free, no SDL_GetKeyFromName. SETUP
   * stays SDL-free (AUDIOTEST=0 does not link SDL), so the self-contained
   * scancode table emits the numeric keycode directly. */
  snprintf(buf, (size_t)cap, "%ld", k->keycode);
  return buf;
}
