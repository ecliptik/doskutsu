#ifndef DOSKUTSU_CONFIG_KEYS_H
#define DOSKUTSU_CONFIG_KEYS_H

/*
 * doskutsu_config_keys.h -- single source of truth for the user-facing
 * runtime settings that SETUP.EXE writes to DOSKUTSU.CFG and that
 * DOSKUTSU.EXE loads at startup (via the config-file setenv shim,
 * patch nxengine-evo/0216).
 *
 * Each entry maps a short, human-readable key as written in DOSKUTSU.CFG
 * (cfg_key) to the EXACT environment / SDL-hint name the engine already
 * reads (env_name). The engine reads these via getenv / SDL_getenv /
 * SDL_GetHint; in every case SDL_GetHint falls back to the env var of the
 * same name, so the shim can satisfy ALL of them with a single setenv of
 * env_name. The config file is therefore a NEW SOURCE for the SAME keys --
 * no existing lever changes.
 *
 * Precedence is env > file > built-in default: the shim uses
 * setenv(env_name, value, 0) (overwrite=0), so a real DOS `SET` still wins.
 *
 * The ~110 diagnostic / instrumentation / TAS / warp env vars are
 * DELIBERATELY not listed here -- only options a player should touch.
 *
 * ASCII-only; compiles as both C and C++ (shared by the engine and by the
 * standalone SETUP.EXE, which is C).
 */

#include <stddef.h> /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/* The header-only helpers below are used by some including TUs and not
 * others; suppress -Wunused-function where the compiler supports it. */
#if defined(__GNUC__)
#define DKT_MAYBE_UNUSED __attribute__((unused))
#else
#define DKT_MAYBE_UNUSED
#endif

typedef enum
{
  DKT_BOOL = 0, /* "0" / "1"                          */
  DKT_INT,      /* integer in [imin, imax]            */
  DKT_ENUM,     /* one of the strings in enum_vals[]  */
  DKT_STR       /* free-form string (e.g. BLASTER);   *
                 * SETUP composes/validates it, the   *
                 * loader passes it through verbatim   */
} dkt_type_t;

typedef enum
{
  DKC_SOUND = 0,
  DKC_INPUT,
  DKC_PERF,
  DKC_COMPAT,
  DKC_HARDWARE  /* sound-hardware (BLASTER) -- edited by SETUP's dedicated
                 * Sound Hardware screen, NOT the generic category editor */
} dkt_category_t;

/* Bit flags for dkt_key_t.flags. */
#define DKT_F_PRESENCE      0x1u /* engine PRESENCE-tests env_name (getenv()
                                  * != NULL is ON regardless of value); the
                                  * loader must NOT setenv a "0"/empty value
                                  * or it would wrongly enable the feature. */
#define DKT_F_AUTHORITATIVE 0x2u /* loader setenv's with overwrite=1 so the
                                  * config value WINS over an ambient env/
                                  * AUTOEXEC SET (file > env). Used for
                                  * BLASTER per operator decision: SETUP's
                                  * sound-hardware choice is authoritative.
                                  * All tuning keys leave this unset and
                                  * stay env > file (a real SET still wins). */

typedef struct
{
  const char    *cfg_key;   /* short key written in DOSKUTSU.CFG          */
  const char    *env_name;  /* exact env / hint name the engine reads     */
  dkt_type_t      type;
  dkt_category_t  category;
  const char    *def;       /* production default value (string)          */
  int             imin;     /* DKT_INT lower bound                        */
  int             imax;     /* DKT_INT upper bound                        */
  const char    **enum_vals;/* NULL-terminated; DKT_ENUM only             */
  const char    *label;     /* short UI label                            */
  const char    *help;      /* one-line help string                      */
  unsigned        flags;    /* bitmask of DKT_F_* (presence / authoritative);
                             * 0 = normal value-checked, env > file key.
                             * The loader (doskutsu_config.h) honors these. */
} dkt_key_t;

/* Enum value tables. "auto" means "let the engine auto-detect" -- SETUP
 * omits the key from DOSKUTSU.CFG in that case so the auto-detect chain
 * runs unchanged. */
static const char *dkt_backend_vals[] = { "auto", "wb", "opl3", "organya", NULL };

static const dkt_key_t DKT_KEYS[] =
{
  /* ---- Sound -------------------------------------------------------- */
  { "AUDIO_BACKEND", "SDL_HINT_DOSKUTSU_AUDIO_BACKEND",
    DKT_ENUM, DKC_SOUND, "auto", 0, 0, dkt_backend_vals,
    "Music backend",
    "auto=detect, wb=WaveBlaster MIDI, opl3=FM synth, organya=Organya synth", 0 },

  { "AUDIO_OFF", "DOSKUTSU_NO_AUDIO",
    DKT_BOOL, DKC_SOUND, "0", 0, 0, NULL,
    "Sound disabled",
    "1 disables all audio (no SFX, no music)", DKT_F_PRESENCE /* engine
      reads getenv("DOSKUTSU_NO_AUDIO") for mere presence, so AUDIO_OFF=0
      must NOT setenv it (would disable audio) */ },

  { "AUDIO_TIER2", "SDL_HINT_DOSKUTSU_AUDIO_TIER2",
    DKT_BOOL, DKC_SOUND, "1", 0, 0, NULL,
    "Audio quality",
    "1=11025 Hz mono master rate (default); 0=legacy higher rate", 0 },

  { "SB16_VOICE_VOL", "SDL_HINT_DOSKUTSU_SB16_VOICE_VOL",
    DKT_INT, DKC_SOUND, "28", 0, 31, NULL,
    "SB16 voice volume",
    "SB16 mixer voice (PCM/SFX) level, 0-31", 0 },

  { "SB16_FM_VOL", "SDL_HINT_DOSKUTSU_SB16_FM_VOL",
    DKT_INT, DKC_SOUND, "28", 0, 31, NULL,
    "SB16 FM volume",
    "SB16 mixer FM (OPL3 music) level, 0-31", 0 },

  { "ORG_PRERENDER", "SDL_HINT_DOSKUTSU_ORG_PRERENDER",
    DKT_BOOL, DKC_SOUND, "0", 0, 0, NULL,
    "Organya pre-render",
    "1 pre-renders Organya music to a disk PCM cache (less demanding on CPU at playback)", 0 },

  /* ---- Input -------------------------------------------------------- */
  { "USE_JOYSTICK", "DOSKUTSU_USE_JOYSTICK",
    DKT_BOOL, DKC_INPUT, "0", 0, 0, NULL,
    "Joystick / gamepad",
    "1 enables the gameport (costs ~80 ms/frame BIOS poll if no stick attached)", 0 },

  /* ---- Performance / display --------------------------------------- */
  { "PERF_MODE", "SDL_HINT_DOSKUTSU_PERF_MODE",
    DKT_INT, DKC_PERF, "0", 0, 2, NULL,
    "Performance mode",
    "0=faithful, 1=smooth (drop decorative detail), 2=fast", 0 },

  { "FIXED_TIMESTEP", "SDL_HINT_DOSKUTSU_FIXED_TIMESTEP",
    DKT_BOOL, DKC_PERF, "1", 0, 0, NULL,
    "Fixed 50 Hz timestep",
    "1=game runs at authored 50 Hz regardless of render fps (default)", 0 },

  /* ---- Compatibility / troubleshooting (Advanced submenu) ---------- */
  { "AUDIO_WB_DIRECT_PORT", "SDL_HINT_DOSKUTSU_AUDIO_WB_DIRECT_PORT",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "WaveBlaster direct port",
    "1=direct MPU-401 port writes (default); 0=DSP-mediated fallback", 0 },

  { "DIRTY_RECTS", "SDL_HINT_DOSKUTSU_DIRTY_RECTS",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "Dirty-rect rendering",
    "0 force-disables dirty-rect rendering (only if you see artifacts)", 0 },

  { "PIXEL_FORMAT_8", "SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "8bpp indexed mode",
    "0 force-disables indexed 8bpp mode (only if you see color regressions)", 0 },

  { "FORCE_PUMP_YIELD", "SDL_HINT_DOSKUTSU_FORCE_PUMP_YIELD",
    DKT_BOOL, DKC_COMPAT, "0", 0, 0, NULL,
    "Force per-pump yield",
    "1 restores the original per-pump cooperative yield (only if audio stutters)", 0 },

  { "THRASH_FULLCOVER", "SDL_HINT_DOSKUTSU_THRASH_FULLCOVER",
    DKT_BOOL, DKC_COMPAT, "1", 0, 0, NULL,
    "Backdrop full-cover",
    "1=fixed backdrop coverage on motion (default); 0=legacy clip", 0 },

  /* ---- Sound hardware (BLASTER) ------------------------------------ */
  /* The whole sound-hardware selection (base port / IRQ / 8-bit DMA /
   * 16-bit DMA / MPU-401 MIDI port / card type) rides on the one standard
   * BLASTER variable the SDL3-DOS backend already parses at init
   * (A/I/D/H/P/T fields; P = MPU-401 MIDI port). SETUP's Sound Hardware
   * screen composes this string from its fields; the loader passes it
   * through. AUTHORITATIVE (overwrite=1): a SETUP-written BLASTER overrides
   * an ambient AUTOEXEC `SET BLASTER` (operator decision: the setup config
   * is the authoritative source, BLASTER is merely the auto-detect default).
   * Default "" -> SETUP omits the line so the ambient SET BLASTER is used. */
  { "BLASTER", "BLASTER",
    DKT_STR, DKC_HARDWARE, "", 0, 0, NULL,
    "Sound hardware",
    "SB base/IRQ/DMA/MIDI-port (A/I/D/H/P/T); SETUP's value overrides AUTOEXEC SET BLASTER",
    DKT_F_AUTHORITATIVE },
};

#define DKT_KEY_COUNT ((int)(sizeof(DKT_KEYS) / sizeof(DKT_KEYS[0])))

/* Case-insensitive ASCII compare of a NUL-terminated key against a
 * possibly-not-NUL-terminated token of length n. Returns 1 on match. */
static DKT_MAYBE_UNUSED int dkt_key_ieq(const char *key, const char *tok, int n)
{
  int i;
  for (i = 0; i < n; ++i)
  {
    char a = key[i];
    char b = tok[i];
    if (a == '\0') return 0;
    if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
    if (a != b) return 0;
  }
  return key[n] == '\0';
}

/* Look up a registry entry by its cfg_key (case-insensitive). tok is the
 * key token from the config line; n is its length. Returns NULL if the
 * key is not a recognized user-facing setting. */
static DKT_MAYBE_UNUSED const dkt_key_t *dkt_lookup(const char *tok, int n)
{
  int i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (dkt_key_ieq(DKT_KEYS[i].cfg_key, tok, n))
      return &DKT_KEYS[i];
  return NULL;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOSKUTSU_CONFIG_KEYS_H */
