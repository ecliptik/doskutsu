/*
 * main.c -- DOSKUTSU SETUP.EXE entry point.
 *
 * Profiles the host, recommends settings (user confirms/overrides), edits
 * the user-facing options, tests SFX/music, and writes DOSKUTSU.CFG, which
 * DOSKUTSU.EXE loads at startup (patch nxengine-evo/0210). Classic CP437
 * full-screen TUI. ASCII-only source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tui.h"
#include "setupcfg.h"
#include "profile.h"
#include "recommend.h"
#include "audiotest.h"

#define CFG_PATH "DOSKUTSU.CFG"

static scfg_t      g_cfg;
static sysprofile_t g_prof;
static int          g_dirty; /* unsaved changes */

/* ---- value formatting / editing ------------------------------------- */

static void fmt_value(int idx, char *out, int cap)
{
  const dkt_key_t *k = &DKT_KEYS[idx];
  const char *v = scfg_get(&g_cfg, idx);
  if (k->type == DKT_BOOL)
    snprintf(out, (size_t)cap, "%s", strcmp(v, "1") == 0 ? "On" : "Off");
  else
    snprintf(out, (size_t)cap, "%s", v);
  out[cap - 1] = '\0';
}

static void cycle_value(int idx, int dir)
{
  const dkt_key_t *k = &DKT_KEYS[idx];
  if (k->type == DKT_BOOL)
  {
    scfg_set(&g_cfg, idx, strcmp(scfg_get(&g_cfg, idx), "1") == 0 ? "0" : "1");
  }
  else if (k->type == DKT_INT)
  {
    int v = atoi(scfg_get(&g_cfg, idx)) + dir;
    char b[16];
    if (v < k->imin) v = k->imax;
    if (v > k->imax) v = k->imin;
    snprintf(b, sizeof(b), "%d", v);
    scfg_set(&g_cfg, idx, b);
  }
  else if (k->type == DKT_ENUM && k->enum_vals)
  {
    int n = 0, cur = 0, i;
    while (k->enum_vals[n]) ++n;
    for (i = 0; i < n; ++i)
      if (strcmp(k->enum_vals[i], scfg_get(&g_cfg, idx)) == 0) cur = i;
    cur = (cur + dir + n) % n;
    scfg_set(&g_cfg, idx, k->enum_vals[cur]);
  }
  g_dirty = 1;
}

/* Status lines (T44 session model): edits apply LIVE to the in-memory session
 * config on every screen. T62 interaction model: Space / Left / Right cycle the
 * highlighted value; Enter commits the row (advances the ESC revert baseline)
 * and moves to the next row; ESC asks "Save setting?" only when the screen
 * changed since the last commit, then returns to the menu. Persistence happens
 * only at the main menu -- "Save and exit" writes the file, "Quit without
 * saving" discards. There is no per-screen save (the old F10-per-screen save was
 * the operator's confusion). F10 stays only on the main menu as the
 * Save-and-exit accelerator. Home = top. */
#define EDIT_STATUS "Space/Left-Right Change   Enter Save+Next   ESC Back"
#define MENU_STATUS "Enter Select   F10 Save and Exit   ESC Quit without Saving"

/* Write the in-memory config to disk and show a modal result. Used by the
 * F10 "save settings and return to the menu" action (T17) and the main-menu
 * "Save and exit" item. */
static void cfg_write_toast(void)
{
  tui_clear();
  tui_titlebar("DOSKUTSU SETUP"); /* round-7 item 2: title bar on the toast too */
  if (scfg_save(&g_cfg, CFG_PATH) == 0)
  {
    const char *lines[2];
    lines[0] = "Settings written to " CFG_PATH;
    lines[1] = "DOSKUTSU.EXE will load them on next launch.";
    g_dirty = 0;
    tui_message("Saved", lines, 2);
  }
  else
  {
    const char *lines[1];
    lines[0] = "ERROR: could not write " CFG_PATH " (disk full / read-only?)";
    tui_message("Save failed", lines, 1);
  }
}

/* Extended, first-timer-friendly help per config key (T17). Falls back to the
 * registry one-liner for any key not given a longer description here. */
static const char *ui_help(const char *cfg_key, const char *fallback)
{
#define UI_K(s) dkt_key_ieq(cfg_key, s, (int)sizeof(s) - 1)
  if (UI_K("USE_JOYSTICK"))
    return "Enable a joystick or gamepad on the PC game port. Leave this Off "
           "if you play on the keyboard -- polling an absent game port can "
           "cost time every frame on some BIOSes.";
  if (UI_K("PERF_MODE"))
    return "How hard the game works to look its best. 0 = faithful (all "
           "detail). 1 = smooth (drops some decorative detail for speed). "
           "2 = fast (most aggressive). Raise it if the game feels sluggish.";
  if (UI_K("FIXED_TIMESTEP"))
    return "On = the game always runs at its authored 50 Hz pace even when the "
           "display cannot keep up (recommended). Off = game speed follows the "
           "frame rate. Leave this On.";
  if (UI_K("AUDIO_WB_DIRECT_PORT"))
    return "WaveBlaster music only. On = write the MPU-401 MIDI port directly "
           "(default, fastest). Off = route through the Sound Blaster DSP as a "
           "fallback. Change only if WaveBlaster music misbehaves.";
  if (UI_K("DIRTY_RECTS"))
    return "On = redraw only the parts of the screen that changed (default, "
           "faster). Off = redraw everything each frame. Turn Off only if you "
           "see leftover graphics on screen.";
  if (UI_K("PIXEL_FORMAT_8"))
    return "On = 256-color indexed video (default, fastest on period hardware). "
           "Off = a truecolor fallback path. Turn Off only if the colors look "
           "wrong.";
  if (UI_K("FORCE_PUMP_YIELD"))
    return "Off = default audio scheduling. On = restore the older per-chunk "
           "cooperative yield, which can smooth out music stutter on some "
           "machines at a small speed cost.";
  if (UI_K("THRASH_FULLCOVER"))
    return "On = fully repaint the parallax backdrop while the camera moves "
           "(default, prevents black bands). Off = the older partial-cover "
           "behavior. Leave this On.";
#undef UI_K
  return fallback;
}

/* A keyed help/finding line: a key/label + a description. The key is drawn in
 * the palette's `value` role so it matches the value shown in the setting row
 * it describes (T22 item 3 -- single shared role, not per-option colors). */
typedef struct { const char *key; const char *desc; } helprow_t;

/* Per-line help for the Music backend row (replaces the paragraph blob).
 * Each description starts with a capital letter; no ";" (round-6 item 2). */
static const helprow_t BACKEND_HELP[] = {
  { "Auto",        "Detect the best your card supports." },
  { "WaveBlaster", "Wavetable MIDI daughterboard, best quality." },
  { "OPL3",        "FM synth on any Sound Blaster, classic sound." },
  { "Organya",     "Cave Story's own synth, most faithful (more demanding on CPU)." }
};

/* Per-line help for the Performance mode row (round-8 item 9 wording:
 * standardized "more/less/least demanding on CPU"). */
static const helprow_t PERF_HELP[] = {
  { "0 Faithful", "Full detail (more demanding on CPU)." },
  { "1 Smooth",   "Drops some decorative detail (less demanding on CPU)." },
  { "2 Fast",     "Least detail (least demanding on CPU)." }
};

/* ---- per-line option help for the boolean / enum editor rows (item 3) ----
 * The operator's review-3 wants EVERY multi-option row to show one option per
 * line (the 70-advanced "blob" complaint), matching the backend list above --
 * not a prose paragraph. Each table is the value choices the row cycles
 * through, key in the palette value color, one-sentence description after it.
 * Int rows (volumes) have no discrete options and stay wrapped prose. */
static const helprow_t H_ONOFF_WB[] = {
  { "On",  "Write the MPU-401 MIDI port directly (default, fastest)." },
  { "Off", "Route through the Sound Blaster DSP as a fallback." }
};
static const helprow_t H_ONOFF_DIRTY[] = {
  { "On",  "Redraw only the parts that changed (less demanding on CPU)." },
  { "Off", "Redraw the whole screen each frame (more demanding on CPU)." }
};
static const helprow_t H_ONOFF_8BPP[] = {
  { "On",  "256-color indexed video (less demanding on CPU)." },
  { "Off", "A truecolor fallback (more demanding on CPU)." }
};
static const helprow_t H_ONOFF_PUMP[] = {
  { "On",  "Restore the older per-chunk yield, smooths some stutter." },
  { "Off", "Default audio scheduling." }
};
static const helprow_t H_ONOFF_BACKDROP[] = {
  { "On",  "Fully repaint the backdrop while the camera moves (default)." },
  { "Off", "The older partial-cover behavior." }
};
static const helprow_t H_ONOFF_JOY[] = {
  { "On",  "Read a joystick or gamepad on the game port." },
  { "Off", "Keyboard only (less demanding on CPU)." }
};
/* round-6 item 10 wording (round-7 item 1: "game speed", two words). */
static const helprow_t H_ONOFF_FIXED[] = {
  { "On",  "Adjust game speed to match 50Hz pace (recommended)." },
  { "Off", "Game speed matches frame rate." }
};
static const helprow_t H_ONOFF_SOUND[] = {
  { "Enabled",  "Music and sound effects play." },
  { "Disabled", "Silent (less demanding on CPU)." }
};
static const helprow_t H_ONOFF_PRERENDER[] = {
  { "On",  "Save songs to a disk cache (less demanding on CPU, longer load times)." },
  { "Off", "Music plays without caching (more demanding on CPU)." }
};
/* T45: keyed by the displayed sample rate (matching the row value). */
static const helprow_t H_ONOFF_QUALITY[] = {
  { "11025Hz", "Half sample rate (less demanding on CPU)." },
  { "22050Hz", "Original 22050Hz sample rate (more demanding on CPU)." }
};

/* The per-line option list for a config key, or NULL if the row is single-
 * concept (wrapped prose). keyw is the leader width so the descriptions align. */
static const helprow_t *opt_help_for(const char *cfg_key, int *n, int *keyw)
{
#define OPT_K(s, tbl, kw)                                          \
  if (dkt_key_ieq(cfg_key, s, (int)sizeof(s) - 1))                 \
  { *n = (int)(sizeof(tbl) / sizeof(tbl[0])); *keyw = (kw); return tbl; }
  OPT_K("AUDIO_BACKEND",        BACKEND_HELP,      13)
  OPT_K("PERF_MODE",            PERF_HELP,         12)
  OPT_K("AUDIO_WB_DIRECT_PORT", H_ONOFF_WB,         6)
  OPT_K("DIRTY_RECTS",          H_ONOFF_DIRTY,      6)
  OPT_K("PIXEL_FORMAT_8",       H_ONOFF_8BPP,       6)
  OPT_K("FORCE_PUMP_YIELD",     H_ONOFF_PUMP,       6)
  OPT_K("THRASH_FULLCOVER",     H_ONOFF_BACKDROP,   6)
  OPT_K("USE_JOYSTICK",         H_ONOFF_JOY,        6)
  OPT_K("FIXED_TIMESTEP",       H_ONOFF_FIXED,      6)
  OPT_K("AUDIO_OFF",            H_ONOFF_SOUND,     11)
  OPT_K("ORG_PRERENDER",        H_ONOFF_PRERENDER,  6)
  OPT_K("AUDIO_TIER2",          H_ONOFF_QUALITY,    9)
#undef OPT_K
  *n = 0; *keyw = 0;
  return NULL;
}

/* Draw a keyed-list help inside a box interior at (x,y): each entry's key in
 * the palette value color (matching the row value), its description WRAPPED
 * into descw columns starting at x+keyw. Continuation lines indent to that
 * same desc column so they line up, and the text stays inside the box
 * (round-6 item 4 overflow fix -- the old one-line-per-entry version ran long
 * descriptions past the right border). Each entry advances by however many
 * lines it wrapped to (up to maxper). Returns total rows drawn. */
static int help_list(int x, int y, int keyw, int descw, int maxper,
                     const helprow_t *rows, int n)
{
  int i, used = 0;
  for (i = 0; i < n; ++i)
  {
    int lines;
    tui_at(x, y + used, PAL->value, PAL->bg, rows[i].key);
    lines = tui_wrap(x + keyw, y + used, descw, maxper, PAL->desc, PAL->bg,
                     rows[i].desc);
    used += (lines > 0) ? lines : 1;
  }
  return used;
}

/* Overwrite a w x h cell rectangle at 1-based (x,y) with blanks in the body
 * color -- a LOCALIZED clear (not a full-screen tui_clear, so no VGA flash).
 * Used to erase a conditional box (e.g. the Sound NOTE) when it is not shown,
 * now that the interactive loops repaint in place rather than full-clearing
 * every keypress (T45 flicker fix). */
static void blank_region(int x, int y, int w, int h)
{
  char sp[82];
  int r, i;
  if (w > 80) w = 80;
  if (w < 0)  w = 0;
  for (i = 0; i < w; ++i) sp[i] = ' ';
  sp[w] = '\0';
  for (r = 0; r < h; ++r) tui_at(x, y + r, PAL->body, PAL->bg, sp);
}

/* Is a category key currently selectable? Some keys are only relevant in
 * certain states; otherwise they render greyed and navigation skips them
 * (T17 greying, the same mechanism the T16 Sound screen uses). */
static int cat_active(int idx)
{
  if (dkt_key_ieq(DKT_KEYS[idx].cfg_key, "AUDIO_WB_DIRECT_PORT", 20))
  {
    const char *b = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
    /* Only meaningful when WaveBlaster MIDI is (or may be) the backend. */
    return strcmp(b, "wb") == 0 || strcmp(b, "auto") == 0;
  }
  return 1;
}

/* Edit every key in a category in place. Space/Left/Right change the value,
 * edits apply live to the session config; ESC returns to the menu keeping them
 * (T44). Greyed rows are skipped by navigation. */
static void edit_category(dkt_category_t cat, const char *title)
{
  int idxs[DKT_KEY_COUNT], n = 0, sel = 0, i;
  scfg_t snap;        /* T52: entry snapshot for the ESC "revert this screen" path */
  int dirty_snap;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (DKT_KEYS[i].category == cat) idxs[n++] = i;
  if (n == 0) return;
  snap = g_cfg;
  dirty_snap = g_dirty;
  while (sel < n && !cat_active(idxs[sel])) ++sel; /* start on a live row */
  if (sel >= n) sel = 0;

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place (no flash) */
  for (;;)
  {
    int k, j;
    tui_titlebar(title); /* round-7 item 2: every screen shows its page title */
    tui_box(8, 3, 64, n + 4, title);
    for (i = 0; i < n; ++i)
    {
      char row[80], val[24];
      int  active = cat_active(idxs[i]);
      int  selrow = (active && i == sel);
      int  lblfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
      int  bg = selrow ? PAL->sel_bg : PAL->bg;
      int  valfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->value);
      fmt_value(idxs[i], val, (int)sizeof(val));
      /* item 1: the highlight covers the WHOLE line (full box interior width).
       * Paint the full-width label bar, then overpaint the value in its column.
       * Box x=8 w=64 -> interior 62 cells from col 9. */
      snprintf(row, sizeof(row), " %-61.61s", DKT_KEYS[idxs[i]].label);
      tui_at(9, 4 + i, lblfg, bg, row);
      tui_at(9 + 28, 4 + i, valfg, bg, val);
    }
    /* per-item help: multi-option keys get a per-line keyed list (item 3);
     * single-concept keys stay wrapped prose. */
    tui_box(8, n + 8, 64, 7, "HELP");
    {
      int oh_n, oh_kw;
      const helprow_t *oh = opt_help_for(DKT_KEYS[idxs[sel]].cfg_key, &oh_n, &oh_kw);
      if (oh)
        help_list(10, n + 9, oh_kw, 60 - oh_kw, 2, oh, oh_n);
      else
        tui_wrap(10, n + 9, 60, 4, PAL->desc, PAL->bg,
                 ui_help(DKT_KEYS[idxs[sel]].cfg_key, DKT_KEYS[idxs[sel]].help));
    }
    tui_status(EDIT_STATUS);

    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN)
    {
      int dir = (k == TUI_KEY_UP) ? -1 : +1;
      for (j = 0; j < n; ++j)
      {
        sel = (sel + dir + n) % n;
        if (cat_active(idxs[sel])) break;
      }
    }
    else if (k == TUI_KEY_HOME)
    {
      for (sel = 0; sel < n && !cat_active(idxs[sel]); ++sel) {}
      if (sel >= n) sel = 0;
    }
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      /* T62: only Space / Left / Right cycle the highlighted row's value. */
      if (cat_active(idxs[sel]))
        cycle_value(idxs[sel], (k == TUI_KEY_LEFT) ? -1 : +1);
    }
    else if (k == TUI_KEY_ENTER)
    {
      /* T62: Enter commits the current row -- advance the revert baseline so a
       * later ESC "Save setting?" never offers to undo it -- then moves to the
       * next live row. No prompt. */
      snap = g_cfg; dirty_snap = g_dirty;
      for (j = 0; j < n; ++j)
      {
        sel = (sel + 1) % n;
        if (cat_active(idxs[sel])) break;
      }
    }
    else if (k == TUI_KEY_ESC)
    {
      /* T52/T62: if rows changed since the last commit, ask Save setting? Y =
       * keep in the session, N = revert to the baseline. Unchanged -> silent
       * back. The baseline starts at entry and advances on each Enter. */
      if (scfg_differs(&g_cfg, &snap) &&
          !tui_yesno("Save setting?", "Save setting?", 0))
      {
        g_cfg = snap; g_dirty = dirty_snap; /* N -> revert to baseline */
      }
      return;
    }
    /* F10 is intentionally inert on subscreens (T44): saving is a main-menu
     * decision only, never a per-screen action. */
  }
}

/* ---- Sound Setup screen (operator redesign, T16) -------------------- *
 * A dedicated editor (not the generic category loop) so it can: put Sound
 * Enabled/Disabled first and grey the rest out when disabled; show the music
 * backend under friendly names; grey Organya pre-render unless the backend is
 * Organya; and give each row first-timer help with pros/cons. Greying = a
 * non-selectable, dimmed row that navigation skips (the mechanism T17 reuses).
 *
 * NOTE (operator spec item 4 -- MIDI instrument set): the engine's
 * SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE / _GM_VARIANT are diagnostic A/B levers,
 * not a shipped user choice -- only data/midi/ (the default "wiimidi" set) is
 * populated by the standard asset fetch (docs/ASSETS.md Step 4.5); orgmid /
 * orgmid1 / orgmid2 come from a separate dev build script and are not present
 * in a normal install. Per the spec's "if only one/none, omit" branch the row
 * is omitted so a first-timer is never pointed at a directory their disk lacks.
 */

enum
{
  SND_ENABLED = 0, /* AUDIO_OFF, shown inverted as Enabled/Disabled */
  SND_BACKEND,     /* AUDIO_BACKEND, friendly names                 */
  SND_PRERENDER,   /* ORG_PRERENDER, active only for Organya        */
  SND_QUALITY,     /* AUDIO_TIER2                                    */
  SND_VOICEVOL,    /* SB16_VOICE_VOL                                 */
  SND_FMVOL,       /* SB16_FM_VOL                                    */
  SND_NROWS
};

static int snd_key_idx(int row)
{
  switch (row)
  {
    case SND_ENABLED:   return scfg_index("AUDIO_OFF");
    case SND_BACKEND:   return scfg_index("AUDIO_BACKEND");
    case SND_PRERENDER: return scfg_index("ORG_PRERENDER");
    case SND_QUALITY:   return scfg_index("AUDIO_TIER2");
    case SND_VOICEVOL:  return scfg_index("SB16_VOICE_VOL");
    case SND_FMVOL:     return scfg_index("SB16_FM_VOL");
    default:            return -1;
  }
}

static const char *snd_label(int row)
{
  switch (row)
  {
    case SND_ENABLED:   return "Sound";
    case SND_BACKEND:   return "Music backend";
    case SND_PRERENDER: return "Organya pre-render";
    case SND_QUALITY:   return "Audio quality";
    case SND_VOICEVOL:  return "SFX volume";
    case SND_FMVOL:     return "Music volume";
    default:            return "";
  }
}

static const char *snd_backend_name(const char *v)
{
  if (strcmp(v, "wb") == 0)      return "MIDI (WaveBlaster)";
  if (strcmp(v, "opl3") == 0)    return "MIDI (OPL3)";
  if (strcmp(v, "organya") == 0) return "Organya";
  return "Auto (detect)"; /* "auto" or anything unrecognized */
}

static void snd_value(int row, char *out, int cap)
{
  int idx = snd_key_idx(row);
  const char *v = scfg_get(&g_cfg, idx);
  if (row == SND_ENABLED)
    snprintf(out, (size_t)cap, "%s", strcmp(v, "1") == 0 ? "Disabled" : "Enabled");
  else if (row == SND_BACKEND)
    snprintf(out, (size_t)cap, "%s", snd_backend_name(v));
  else if (row == SND_QUALITY)
    /* T45: show the actual sample rate, not On/Off. AUDIO_TIER2=1 is the
     * default half rate (11025Hz), =0 the original 22050Hz. */
    snprintf(out, (size_t)cap, "%s", strcmp(v, "1") == 0 ? "11025Hz" : "22050Hz");
  else
    fmt_value(idx, out, cap); /* On/Off for bool, number for int */
  out[cap - 1] = '\0';
}

static const char *snd_help(int row)
{
  switch (row)
  {
    case SND_ENABLED:
      return "Turn all game audio on or off. Disabled means no music and no "
             "sound effects (less demanding on CPU). Most players want this "
             "Enabled.";
    case SND_BACKEND:
      return "How music is played. MIDI (WaveBlaster) = wavetable music from a "
             "daughterboard: best quality, needs that hardware. MIDI (OPL3) = "
             "FM synth built into most Sound Blasters: works everywhere, the "
             "classic sound. Organya = Cave Story's original synth: most "
             "faithful (more demanding on CPU). Auto = pick the best your card "
             "supports.";
    case SND_PRERENDER:
      return "Organya only. The first time each song plays it is rendered to a "
             "disk cache so playback stays smooth afterward. Costs a few seconds "
             "and some disk space per song, less demanding on CPU at playback.";
    case SND_QUALITY:
      return "On = 11025 Hz mono mixing (default, lighter on the CPU). Off = a "
             "higher legacy sample rate (heavier, only a marginal gain). Leave "
             "On unless you have CPU headroom to spare.";
    case SND_VOICEVOL:
      return "Sound Blaster 16 mixer level for digital sound effects (0-31). "
             "Lower it if the effects drown out the music.";
    case SND_FMVOL:
      return "Sound Blaster 16 mixer level for OPL3 FM music (0-31). Lower it "
             "if the music is too loud next to the sound effects.";
    default:
      return "";
  }
}

/* A row is selectable when its preconditions hold; others render dimmed and
 * are skipped by navigation (operator: grey out, do not let the cursor land). */
static int snd_active(int row)
{
  int sound_on = strcmp(scfg_get(&g_cfg, scfg_index("AUDIO_OFF")), "1") != 0;
  int organya  = strcmp(scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND")),
                        "organya") == 0;
  if (row == SND_ENABLED) return 1;
  if (!sound_on) return 0;
  if (row == SND_PRERENDER) return organya;
  /* T57: Audio quality (the device sample rate) only affects the Organya PCM
   * music path; for the MIDI backends (OPL3 / WaveBlaster) the music is chip-
   * synthesized, so grey it out there. */
  if (row == SND_QUALITY) return organya;
  return 1;
}

/* Move the selection by dir, skipping inactive (greyed) rows. */
static int snd_step(int sel, int dir)
{
  int i, r = sel;
  for (i = 0; i < SND_NROWS; ++i)
  {
    r = (r + dir + SND_NROWS) % SND_NROWS;
    if (snd_active(r)) return r;
  }
  return sel;
}

static void screen_sound(void)
{
  int sel = snd_active(SND_BACKEND) ? SND_BACKEND : SND_ENABLED;
  scfg_t snap = g_cfg;          /* T52: entry snapshot for the ESC revert path */
  int dirty_snap = g_dirty;

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place (no flash) */
  for (;;)
  {
    int i, k, hy;
    tui_titlebar("SOUND SETUP");
    tui_box(8, 3, 64, SND_NROWS + 2, "AUDIO");
    for (i = 0; i < SND_NROWS; ++i)
    {
      char row[80], val[28];
      int active = snd_active(i);
      int selrow = (active && i == sel);
      int lblfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
      int bg = selrow ? PAL->sel_bg : PAL->bg;
      int valfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->value);
      snd_value(i, val, (int)sizeof(val));
      /* item 1: highlight the WHOLE line (full box interior width). Full-width
       * label bar, then overpaint the value in its column. */
      snprintf(row, sizeof(row), " %-61.61s", snd_label(i));
      tui_at(9, 4 + i, lblfg, bg, row);
      tui_at(9 + 26, 4 + i, valfg, bg, val);
    }

    /* per-item help: multi-option rows get a per-line keyed list, each desc
     * wrapped + clipped inside the box with indented continuation (item 4);
     * single-concept rows stay wrapped prose. */
    hy = SND_NROWS + 6; /* = 12 */
    tui_box(8, hy, 64, 8, "HELP");
    {
      int oh_n, oh_kw;
      const helprow_t *oh =
        opt_help_for(DKT_KEYS[snd_key_idx(sel)].cfg_key, &oh_n, &oh_kw);
      if (oh)
        help_list(10, hy + 1, oh_kw, 60 - oh_kw, 2, oh, oh_n);
      else
        tui_wrap(10, hy + 1, 60, 6, PAL->desc, PAL->bg, snd_help(sel));
    }

    /* NOTE box -- Organya-context advisories, content follows the highlighted
     * row. On the Audio quality row it explains the SETUP Organya test will not
     * reflect the rate (T45 item 3); on the other rows, the slow-CPU pre-render
     * advice. Shown only for the Organya backend; when no note applies the
     * region is blanked (the loop no longer full-clears -- T45 flicker fix). */
    {
      int ny = hy + 9; /* row 21, below the taller HELP box */
      const char *backend = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
      int is_org = strcmp(backend, "organya") == 0;
      const char *note = NULL;
      /* T67: the WaveBlaster freeze-risk NOTE was removed -- WB is fixed + ships
       * default-on (cold-init reorder nx 0218/0220 + SDL pacing 0097/0098), so
       * there is no freeze advisory to show. The remaining notes are Organya-
       * only. */
      if (is_org && sel == SND_QUALITY)
        note = "Audio quality applies in the game. The SETUP Organya test "
               "plays a pre-rendered clip, so it will not sound different.";
      else if (is_org && g_prof.cpu_class != CPU_586)
        note = (sel == SND_PRERENDER)
          ? "Pre-render writes each song to a disk cache - recommended for "
            "slower CPUs, increases load times."
          : "Organya is demanding on this CPU - enabling Organya pre-render "
            "is recommended.";
      if (note)
      {
        tui_box(8, ny, 64, 4, "NOTE");
        tui_wrap(10, ny + 1, 60, 2, PAL->warn_fg, PAL->warn_bg, note);
      }
      else
        blank_region(8, ny, 64, 4);
    }
    tui_status(EDIT_STATUS);

    k = tui_getkey();
    if (k == TUI_KEY_UP)        sel = snd_step(sel, -1);
    else if (k == TUI_KEY_DOWN) sel = snd_step(sel, +1);
    else if (k == TUI_KEY_HOME) sel = SND_ENABLED; /* always selectable */
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      /* T62: only Space / Left / Right cycle the highlighted row's value. */
      if (snd_active(sel))
      {
        cycle_value(snd_key_idx(sel), (k == TUI_KEY_LEFT) ? -1 : +1);
        /* T8 auto-suggest: choosing Organya on a sub-Pentium CPU enables the
         * v1.0.8 PCM pre-render cache (the user can toggle it back off). The
         * explanatory Note box renders from the same condition (T22 item 4). */
        if (sel == SND_BACKEND)
          recommend_org_prerender(&g_cfg, &g_prof);
      }
    }
    else if (k == TUI_KEY_ENTER)
    {
      /* T62: Enter commits the current row (advance the revert baseline) and
       * moves to the next live row. No prompt. */
      snap = g_cfg; dirty_snap = g_dirty;
      sel = snd_step(sel, +1);
    }
    else if (k == TUI_KEY_ESC)
    {
      /* T52/T62: changed since the last commit -> Save setting? (Y keep / N
       * revert to baseline); else silent back. */
      if (scfg_differs(&g_cfg, &snap) &&
          !tui_yesno("Save setting?", "Save setting?", 0))
      {
        g_cfg = snap; g_dirty = dirty_snap;
      }
      return;
    }
    /* F10 inert on subscreens (T44): save is a main-menu decision only. */
  }
}

/* ---- Sound Hardware screen (BLASTER A/I/D/H/P/T) -------------------- */

/* Allowed values per BLASTER field; index 0 is the conventional default.
 * A and P are hex (i/o ports); I/D/H/T are decimal. P value -1 == "none"
 * (omit the MPU-401 MIDI field entirely). */
typedef struct
{
  char        letter;  /* BLASTER field letter */
  const char *label;
  const int  *vals;
  int         nvals;
  int         hex;     /* 1 -> render/emit as hex */
} hwfield_t;

static const int hw_port_vals[] = { 0x220, 0x240, 0x260, 0x280 };
static const int hw_irq_vals[]  = { 5, 7, 2, 10 };
static const int hw_dma_vals[]  = { 1, 0, 3 };
static const int hw_hdma_vals[] = { 5, 6, 7 };
static const int hw_midi_vals[] = { -1, 0x330, 0x300 };
static const int hw_type_vals[] = { 6, 4, 3, 2, 1 };

static const hwfield_t HW_FIELDS[] = {
  { 'A', "I/O port",          hw_port_vals, 4, 1 },
  { 'I', "IRQ",               hw_irq_vals,  4, 0 },
  { 'D', "8-bit DMA",         hw_dma_vals,  3, 0 },
  { 'H', "16-bit DMA (HDMA)", hw_hdma_vals, 3, 0 },
  { 'P', "MPU-401 MIDI port", hw_midi_vals, 3, 1 },
  { 'T', "Card type",         hw_type_vals, 5, 0 }
};
#define HW_NFIELDS ((int)(sizeof(HW_FIELDS) / sizeof(HW_FIELDS[0])))

static int hw_find_index(const hwfield_t *f, int val)
{
  int i;
  for (i = 0; i < f->nvals; ++i)
    if (f->vals[i] == val) return i;
  return 0;
}

static void hw_fmt(const hwfield_t *f, int idx, char *out, int cap)
{
  int v = f->vals[idx];
  if (f->letter == 'P' && v < 0) { snprintf(out, (size_t)cap, "none"); return; }
  if (f->letter == 'T')
  {
    /* Traditional Sound Blaster family names (review-3 item 10). */
    const char *nm = (v == 6) ? "Sound Blaster 16"
                   : (v == 4) ? "Sound Blaster Pro"
                   : (v == 3) ? "Sound Blaster Pro 2.0"
                   : (v == 2) ? "Sound Blaster 2.0"
                   : (v == 1) ? "Sound Blaster" : "?";
    snprintf(out, (size_t)cap, "T%d (%s)", v, nm);
    return;
  }
  if (f->hex) snprintf(out, (size_t)cap, "0x%X", v);
  else        snprintf(out, (size_t)cap, "%d", v);
}

/* Seed the per-field selection indices from an existing BLASTER string. */
static void hw_seed_from_str(const char *bl, int *cur)
{
  const char *p = bl;
  while (*p)
  {
    char L;
    int  base, v, fi;
    char *endp;
    if (*p == ' ' || *p == '\t') { ++p; continue; }
    L = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
    ++p;
    base = (L == 'A' || L == 'P') ? 16 : 10;
    v = (int)strtol(p, &endp, base);
    p = endp;
    for (fi = 0; fi < HW_NFIELDS; ++fi)
      if (HW_FIELDS[fi].letter == L) { cur[fi] = hw_find_index(&HW_FIELDS[fi], v); break; }
  }
}

/* Compose "A220 I5 D1 H5 P330 T6" from the selection indices (P omitted when
 * "none"). */
static void hw_compose(const int *cur, char *out, int cap)
{
  int port = HW_FIELDS[0].vals[cur[0]];
  int irq  = HW_FIELDS[1].vals[cur[1]];
  int dma  = HW_FIELDS[2].vals[cur[2]];
  int hdma = HW_FIELDS[3].vals[cur[3]];
  int midi = HW_FIELDS[4].vals[cur[4]];
  int type = HW_FIELDS[5].vals[cur[5]];
  int n = snprintf(out, (size_t)cap, "A%X I%d D%d H%d", port, irq, dma, hdma);
  if (midi >= 0 && n < cap) n += snprintf(out + n, (size_t)(cap - n), " P%X", midi);
  if (n < cap)              snprintf(out + n, (size_t)(cap - n), " T%d", type);
}

/* T39: the override row (row 0) describes two options, so its help renders as
 * the standard per-line keyed On/Off list (not inline prose) -- same scheme as
 * every other multi-option help. The single-concept field rows below stay
 * one-clause prose (they describe one field, not a set of options). */
static const helprow_t H_ONOFF_OVERRIDE[] = {
  { "On",  "Settings below replace the BLASTER line from AUTOEXEC.BAT." },
  { "Off", "Keep the values from AUTOEXEC.BAT." }
};

/* First-timer help for the highlighted Sound Hardware FIELD row (sel >= 1) --
 * one short clause each (round-6 item 5: tighten, no ";"). Row 0 (override)
 * uses H_ONOFF_OVERRIDE via help_list, not this. */
static const char *hw_help(int sel)
{
  switch (HW_FIELDS[sel - 1].letter)
  {
    case 'A': return "Sound Blaster I/O port, almost always 0x220.";
    case 'I': return "Interrupt (IRQ) line, usually 5 or 7.";
    case 'D': return "8-bit DMA channel, usually 1.";
    case 'H': return "16-bit (high) DMA channel on SB16 cards, usually 5.";
    case 'P': return "MPU-401 MIDI port (usually 0x330), or none. Needed for "
                     "WaveBlaster music.";
    case 'T': return "Sound card type. Sound Blaster 16 works for most cards.";
    default:  return "";
  }
}

static void screen_hardware(void)
{
  int cur[HW_NFIELDS];
  int entry_cur[HW_NFIELDS]; /* T54: entry snapshot for change detection */
  int entry_override;
  int sel = 0, i;
  int override_on;
  const char *cfgbl = scfg_get(&g_cfg, scfg_index("BLASTER"));

  /* Seed from the profiler, then override from any existing cfg BLASTER. */
  cur[0] = hw_find_index(&HW_FIELDS[0], g_prof.snd_base ? g_prof.snd_base : 0x220);
  cur[1] = hw_find_index(&HW_FIELDS[1], g_prof.snd_irq  ? g_prof.snd_irq  : 5);
  cur[2] = hw_find_index(&HW_FIELDS[2], g_prof.snd_dma);
  cur[3] = hw_find_index(&HW_FIELDS[3], g_prof.snd_hdma ? g_prof.snd_hdma : 5);
  cur[4] = g_prof.has_waveblaster ? hw_find_index(&HW_FIELDS[4], 0x330) : 0;
  cur[5] = hw_find_index(&HW_FIELDS[5], g_prof.snd_type ? g_prof.snd_type : 6);
  if (cfgbl[0]) hw_seed_from_str(cfgbl, cur);

  /* Override defaults ON when a card was detected or a BLASTER is already
   * configured; the user can flip it off to fall back to AUTOEXEC SET. */
  override_on = (cfgbl[0] != '\0') || g_prof.snd_detected;

  /* T54: snapshot the SEEDED entry state. The screen seeds cur[] from the
   * detected card and defaults override_on ON, which composes to a non-empty
   * BLASTER even when g_cfg's BLASTER was empty (auto) -- so comparing the
   * composed value against g_cfg would falsely flag a change on a no-edit
   * browse. Compare the current screen state to THIS entry snapshot instead, so
   * the ESC "Save setting?" prompt fires only on a real user edit. */
  memcpy(entry_cur, cur, sizeof(cur));
  entry_override = override_on;

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place (no flash) */
  for (;;)
  {
    int k;
    char composed[SCFG_VAL_MAX];
    tui_titlebar("SOUND HARDWARE");
    tui_box(8, 3, 64, HW_NFIELDS + 6, "BLASTER");

    /* Row 0: the override toggle (always selectable). item 1: highlight the
     * WHOLE line (full box interior width), value overpainted in its column. */
    {
      int sr = (sel == 0);
      int fg = sr ? PAL->sel_fg : PAL->body;
      int bg = sr ? PAL->sel_bg : PAL->bg;
      int vfg = sr ? PAL->sel_fg : PAL->value;
      char row[80];
      /* T37: label renamed to "Override AUTOEXEC.BAT"; the value simplifies to a
       * plain On/Off (the label + help now carry the meaning). */
      const char *ov = override_on ? "On" : "Off";
      snprintf(row, sizeof(row), " %-61.61s", "Override AUTOEXEC.BAT");
      tui_at(9, 4, fg, bg, row);
      tui_at(9 + 28, 4, vfg, bg, ov);
    }
    /* Rows 1..N: the BLASTER fields (greyed + skipped when override Off). */
    for (i = 0; i < HW_NFIELDS; ++i)
    {
      char row[80], val[28];
      int  selrow = (override_on && sel == i + 1);
      int  fg = !override_on ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
      int  bg = selrow ? PAL->sel_bg : PAL->bg;
      int  vfg = !override_on ? PAL->dim : (selrow ? PAL->sel_fg : PAL->value);
      hw_fmt(&HW_FIELDS[i], cur[i], val, (int)sizeof(val));
      snprintf(row, sizeof(row), " %-61.61s", HW_FIELDS[i].label);
      tui_at(9, 5 + i, fg, bg, row);
      tui_at(9 + 28, 5 + i, vfg, bg, val);
    }

    /* per-item help ABOVE the resulting line. Row 0 (override) describes two
     * options -> per-line keyed On/Off list (T39); the field rows are single-
     * concept prose. Both stay inside the box. */
    tui_box(8, HW_NFIELDS + 10, 64, 5, "HELP");
    if (sel == 0)
      help_list(10, HW_NFIELDS + 11, 6, 60 - 6, 2, H_ONOFF_OVERRIDE,
                (int)(sizeof(H_ONOFF_OVERRIDE) / sizeof(H_ONOFF_OVERRIDE[0])));
    else
      tui_wrap(10, HW_NFIELDS + 11, 60, 3, PAL->desc, PAL->bg, hw_help(sel));

    hw_compose(cur, composed, (int)sizeof(composed));
    {
      char pv[80];
      snprintf(pv, sizeof(pv), "BLASTER=%s", override_on ? composed : "(omitted)");
      tui_box(8, HW_NFIELDS + 15, 64, 3, "RESULTING CONFIG LINE");
      tui_at(10, HW_NFIELDS + 16, PAL->value, PAL->bg, pv);
    }
    tui_status(EDIT_STATUS);

    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN)
    {
      /* sel 0 (override) is always live; the fields only when override on. */
      int dir = (k == TUI_KEY_UP) ? -1 : +1, rows = HW_NFIELDS + 1, j;
      for (j = 0; j < rows; ++j)
      {
        sel = (sel + dir + rows) % rows;
        if (sel == 0 || override_on) break;
      }
    }
    else if (k == TUI_KEY_HOME) sel = 0; /* override row is always live */
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      /* T62: only Space / Left / Right cycle the highlighted field's value. */
      int dir = (k == TUI_KEY_LEFT) ? -1 : +1;
      if (sel == 0) override_on = !override_on;
      else if (override_on)
      {
        const hwfield_t *f = &HW_FIELDS[sel - 1];
        cur[sel - 1] = (cur[sel - 1] + dir + f->nvals) % f->nvals;
      }
    }
    else if (k == TUI_KEY_ENTER)
    {
      /* T62: Enter commits the staged BLASTER to the session and advances the
       * revert baseline (only when the user actually changed something since the
       * last commit -- avoids a spurious * UNSAVED on a no-edit Enter, T54),
       * then moves to the next live row. No prompt. */
      int changed = (override_on != entry_override) ||
                    memcmp(cur, entry_cur, sizeof(cur)) != 0;
      int rows = HW_NFIELDS + 1, j;
      if (changed)
      {
        if (override_on) hw_compose(cur, composed, (int)sizeof(composed));
        else             composed[0] = '\0';
        scfg_set(&g_cfg, scfg_index("BLASTER"), composed);
        g_dirty = 1;
        entry_override = override_on;
        memcpy(entry_cur, cur, sizeof(cur));
      }
      for (j = 0; j < rows; ++j)
      {
        sel = (sel + 1) % rows;
        if (sel == 0 || override_on) break;
      }
    }
    else if (k == TUI_KEY_ESC)
    {
      /* T52/T54/T62: only when the USER changed the screen since the last commit
       * (a field or the override toggle, vs the baseline) ask Save setting? Y =
       * commit the composed BLASTER to the session, N = leave the baseline value.
       * A no-edit browse backs out silently -- no spurious prompt. No disk write
       * here. The baseline starts at the seeded entry state and advances on each
       * Enter. */
      int changed = (override_on != entry_override) ||
                    memcmp(cur, entry_cur, sizeof(cur)) != 0;
      if (changed && tui_yesno("Save setting?", "Save setting?", 0))
      {
        if (override_on) hw_compose(cur, composed, (int)sizeof(composed));
        else             composed[0] = '\0';
        scfg_set(&g_cfg, scfg_index("BLASTER"), composed);
        g_dirty = 1;
      }
      return;
    }
    /* F10 inert on subscreens (T44). */
  }
}

/* ---- screens -------------------------------------------------------- */

/* X-Wing-style system-info PANEL pinned at the top of the main menu (T25).
 * Always visible above the menu, NON-selectable -- the profile is fixed at
 * startup, so this is a pure display region. key:value lines for CPU / Memory
 * / Sound / Synth / Video. Keys carry a dotted leader (10 cols) so the values
 * line up in a column; keys render in the title role, values in the value
 * role (or the warn role for a fatal "no FPU"). Draws a box of height 7 at
 * (x,y) of width w. */
/* Comma-list of the detected music synths, OPL3 first (review-3 item 6):
 * "OPL3, WaveBlaster", "OPL3", "WaveBlaster", or "none". */
static void synth_list_str(char *out, int cap)
{
  int opl3 = g_prof.has_opl3, wb = g_prof.has_waveblaster;
  if (opl3 && wb)   snprintf(out, (size_t)cap, "OPL3, WaveBlaster");
  else if (opl3)    snprintf(out, (size_t)cap, "OPL3");
  else if (wb)      snprintf(out, (size_t)cap, "WaveBlaster");
  else              snprintf(out, (size_t)cap, "none");
}

/* The configured MPU-401 MIDI port from the cfg BLASTER P-field, or -1 if no
 * P-field is set. Used to surface the WaveBlaster MIDI port in the profile
 * panel (round-6 item 11 amendment). The field letters are single uppercase
 * and values are hex/decimal digits, so a plain scan for 'P' is unambiguous. */
static int cfg_mpu_port(void)
{
  const char *p = scfg_get(&g_cfg, scfg_index("BLASTER"));
  for (; p && *p; ++p)
    if (*p == 'P' || *p == 'p')
      return (int)strtol(p + 1, NULL, 16);
  return -1;
}

static void draw_profile_panel(int x, int y, int w)
{
  char v[96];
  int  kx = x + 2, vw = 11, row = y + 1;

  /* round-7 item 3b: the key labels (CPU/Memory/Sound/Synth/Video) render in
   * the body role (white) so they read DISTINCT from the values, which stay in
   * the value role (light-cyan). Both go through palette roles, no literals. */
  tui_box(x, y, w, 7, "SYSTEM PROFILE");

  snprintf(v, sizeof(v), "%s%s", g_prof.cpu_desc,
           g_prof.has_fpu ? "" : "  [NO FPU - will not run!]");
  tui_kv(kx, row++, vw, PAL->body,
         g_prof.has_fpu ? PAL->value : PAL->warn_fg, PAL->bg, "CPU ......", v);

  if (g_prof.phys_ram_kb > 0)
    snprintf(v, sizeof(v), "%ld MB RAM (%ld KB physical)",
             g_prof.phys_ram_kb / 1024L, g_prof.phys_ram_kb);
  else
    snprintf(v, sizeof(v), "unknown");
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Memory ...", v);

  if (g_prof.snd_detected)
    /* round-6 item 11: commas + capitalized "Type". HDMA is omitted from this
     * at-a-glance summary (operator refinement -- it is an advanced detail,
     * almost always 5, and lives editable on the Sound Hardware screen). */
    snprintf(v, sizeof(v), "Port 0x%X, IRQ %d, DMA %d, Type T%d",
             g_prof.snd_base, g_prof.snd_irq, g_prof.snd_dma,
             g_prof.snd_type);
  else
    snprintf(v, sizeof(v), "no BLASTER variable set");
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Sound ....", v);

  /* round-6 item 11 amendment: surface the MPU-401 MIDI port on the Synth row
   * when a WaveBlaster is present (configured P-field, else the detected
   * standard 0x330) so it is visible at a glance. */
  synth_list_str(v, (int)sizeof(v));
  if (g_prof.has_waveblaster)
  {
    int mpu = cfg_mpu_port();
    char t[24];
    snprintf(t, sizeof(t), " (MPU 0x%X)", mpu >= 0 ? mpu : 0x330);
    strncat(v, t, sizeof(v) - strlen(v) - 1);
  }
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Synth ....", v);

  snprintf(v, sizeof(v), "%s  VBE %x.%x %s", g_prof.video_desc,
           (g_prof.vbe_version >> 8) & 0xff, g_prof.vbe_version & 0xff,
           g_prof.vbe_lfb ? "(LFB)" : "");
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Video ....", v);
}

static void screen_autodetect(void)
{
  char cpu[48], snd[64], synth[48], perf[24];
  const char *bval, *pval, *pname;
  int bw = 60, bh = 13, bx, by, x, y;

  recommend_apply(&g_cfg, &g_prof, NULL, 0); /* apply; we render our own lines */
  g_dirty = 1;

  bval = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  pval = scfg_get(&g_cfg, scfg_index("PERF_MODE"));
  pname = (pval[0] == '0') ? "faithful" : (pval[0] == '1') ? "smooth" : "fast";
  snprintf(cpu, sizeof(cpu), "%s",
           g_prof.cpu_desc[0] ? g_prof.cpu_desc : cpu_class_name(g_prof.cpu_class));
  if (g_prof.snd_detected)
    /* HDMA omitted here too (operator: it lives only on the Sound Hardware
     * screen). This also resolves the old "1/5" wording + a width risk. */
    snprintf(snd, sizeof(snd), "Port 0x%X, IRQ %d, DMA %d, Type T%d",
             g_prof.snd_base, g_prof.snd_irq, g_prof.snd_dma,
             g_prof.snd_type);
  else
    snprintf(snd, sizeof(snd), "no BLASTER detected");
  synth_list_str(synth, (int)sizeof(synth));
  snprintf(perf, sizeof(perf), "%s (%s)", pval, pname);

  bx = (80 - bw) / 2 + 1;
  by = (25 - bh) / 2 + 1;
  x = bx + 2;
  tui_clear();
  tui_titlebar("AUTO-DETECT"); /* round-7 item 2: page title on every screen */
  tui_box(bx, by, bw, bh, "AUTO-DETECT");
  y = by + 1;
  tui_at(x, y, PAL->body, PAL->bg, "Recommended for your system:");
  y += 2;
  /* each finding on its own line: label (key) and value in distinct roles. */
  tui_kv(x, y++, 16, PAL->title, PAL->body,  PAL->bg, "CPU", cpu);
  tui_kv(x, y++, 16, PAL->title, PAL->body,  PAL->bg, "Sound card", snd);
  tui_kv(x, y++, 16, PAL->title, PAL->body,  PAL->bg, "Synth", synth);
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg,
         "Music backend", snd_backend_name(bval));
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg, "Performance", perf);
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg, "Fixed timestep", "On (50 Hz)");
  ++y;
  tui_at(x, y, PAL->desc, PAL->bg, "Review/override in the menus, then save.");
  ++y;
  tui_at(x, y, PAL->title, PAL->bg, "Press a key...");
  (void)tui_getkey();
}

/* ---- Audio test: X-Wing-style modal popup (T24) -------------------- *
 * The operator's review (2026-06-08) flagged the old inline test as "too
 * busy, overwrites part of the screen" -- the SDL device-open console banners
 * scrolled the TUI and the test drew inline. The redesign is a clean modal:
 * a centered "Testing ..." window with a progress bar that advances over the
 * (bounded, ring-serviced) backend test, then a "Did you hear it? Y / N"
 * prompt, modeled on X-Wing's setup. The result shows as a badge in the
 * chooser. The progress bar is driven by the audiotest progress seam
 * (audiotest.h / audiotest_progress.c): the backend (audiotest_sdl.c
 * pump_service) calls audiotest_progress() with overall test progress; if it
 * emits none, the bar simply fills when the blocking play returns. */

/* Bar geometry for the active popup, shared with the progress callback. */
static int g_pop_barx, g_pop_bary, g_pop_barw;

static void audio_popup_cb(int permille, void *user)
{
  (void)user;
  tui_progress(g_pop_barx, g_pop_bary, g_pop_barw, permille,
               PAL->value, PAL->dim, PAL->bg);
  tui_present(); /* T51: this runs during the blocking play (no getkey), so
                  * flush the bar update to VRAM here to animate it. */
}

/* Run one bounded test as a modal popup. phase 0 = SFX, 1 = music.
 * Returns 1 = heard, 0 = silent (or ESC at the prompt), -1 = could not play
 * (device busy / no OPL3 / no WaveBlaster). */
static int audiotest_popup(int phase)
{
  const char *title = phase ? "Testing Music" : "Testing Sound Effects";
  /* round-6 item 7: terse play-stage caption. */
  const char *what  = phase ? "Playing Music..." : "Playing Sound Effect...";
  int w = 50, h = 9;
  int x = (80 - w) / 2 + 1;
  int y = (25 - h) / 2 + 1;
  int rc;

  /* Play stage: popup over a clean screen; bar animated by the backend
   * progress callback (it fills on return if the backend emits none). */
  tui_clear();
  tui_titlebar("TEST SFX / MUSIC");
  tui_box(x, y, w, h, title);
  tui_wrap(x + 2, y + 2, w - 4, 2, PAL->body, PAL->bg, what);
  g_pop_barx = x + 2; g_pop_bary = y + 5; g_pop_barw = w - 4;
  tui_progress(g_pop_barx, g_pop_bary, g_pop_barw, 0,
               PAL->value, PAL->dim, PAL->bg);
  tui_status("Playing the test...   ESC to skip");
  tui_popup_decorate(x, y, w, h); /* T55: differentiate from the backdrop */
  tui_present(); /* T51: show the popup before the blocking play (no getkey here) */

  audiotest_set_progress(audio_popup_cb, NULL);
  rc = phase ? audiotest_play_music() : audiotest_play_sfx();
  audiotest_set_progress(NULL, NULL);

  /* Answer stage: full repaint so any SDL console-banner residue is covered
   * (defensive; sdl-engine also suppresses the console log). */
  tui_clear();
  tui_titlebar("TEST SFX / MUSIC");
  if (rc != 0)
  {
    tui_box(x, y, w, h, title);
    tui_wrap(x + 2, y + 2, w - 4, 3, PAL->warn_fg, PAL->bg, audiotest_error());
    tui_at(x + 2, y + h - 2, PAL->title, PAL->bg, "Press a key...");
    tui_status("Could not play the test   Press a key");
    tui_popup_decorate(x, y, w, h); /* T55 */
    (void)tui_getkey();
    return -1;
  }

  /* Success: ask "Did you hear it?" via the standard Yes/No widget (T52 -- one
   * consistent prompt everywhere). Default highlight Yes (a successful test
   * usually played). The "which sound played" info lives in the chooser ABOUT
   * box, not here (round-6 item 8). */
  return tui_yesno(title,
                   phase ? "Did you hear the music?"
                         : "Did you hear the sound effect?", 0);
}

/* Result badge for the chooser (res: -2 untested, -1 error, 0 no, 1 yes).
 * review-3 item 12: the 3-state set Working / Not working / Not tested. A
 * device error (-1) folds into "Not working" -- the reason still shows in the
 * ABOUT THIS TEST box text. */
static const char *audiotest_badge(int res)
{
  switch (res)
  {
    case 1:  return "Working";
    case 0:  return "Not working";
    case -1: return "Not working";
    default: return "Not tested";
  }
}

static void screen_audiotest(void)
{
  int res[2]; /* [0] SFX, [1] music; -2 = not yet tested */
  int sel = 0, i, k;

  /* Apply the user's Sound Hardware choice so the live audio test plays
   * through the selected port/IRQ/DMA (the SDL audio backend reads BLASTER
   * at init). overwrite=1 mirrors the engine loader's authoritative
   * semantics; when left empty (auto), the ambient SET BLASTER is used. */
  {
    const char *bl = scfg_get(&g_cfg, scfg_index("BLASTER"));
    if (bl && bl[0]) setenv("BLASTER", bl, 1);
  }
  tui_clear();
  tui_titlebar("TEST SFX / MUSIC");
  if (!audiotest_available())
  {
    const char *lines[3];
    lines[0] = audiotest_error();
    lines[1] = "Save your settings and start the game to hear them,";
    lines[2] = "or rebuild SETUP with the audio backend linked in.";
    tui_message("TEST SFX / MUSIC", lines, 3);
    return;
  }
  if (audiotest_init(&g_cfg) != 0)
  {
    const char *lines[1]; lines[0] = audiotest_error();
    tui_message("TEST SFX / MUSIC", lines, 1);
    return;
  }
  res[0] = res[1] = -2;

  for (;;)
  {
    static const char *items[] = { "Test sound effects", "Test music", "Back" };
    /* T45: no per-key clear (the loop repaints in place); the entry clear above
     * and the post-popup clear below keep the screen clean without flashing. */
    tui_titlebar("TEST SFX / MUSIC");
    tui_box(18, 7, 44, 5, "CHOOSE A TEST");
    for (i = 0; i < 3; ++i)
    {
      int selrow = (i == sel);
      int fg = selrow ? PAL->sel_fg : PAL->body;
      int bg = selrow ? PAL->sel_bg : PAL->bg;
      char row[46];
      /* item 1: highlight the WHOLE line (full box interior). Box x=18 w=44 ->
       * interior 42 cells from col 19. */
      snprintf(row, sizeof(row), " %-41.41s", items[i]);
      tui_at(19, 8 + i, fg, bg, row);
      if (i < 2) /* the two tests carry a result badge; "Back" does not */
      {
        int vfg = selrow ? PAL->sel_fg : PAL->value;
        tui_at(19 + 26, 8 + i, vfg, bg, audiotest_badge(res[i]));
      }
    }
    /* item 6: the ABOUT box shows ONLY the highlighted test's text. The backend
     * resolves real-vs-fallback per phase (audiotest_about), which is where the
     * "which sound played" info now lives (it was dropped from the result popup,
     * item 8). "Back" has no test, so describe the action instead. */
    tui_box(8, 14, 64, 5, "ABOUT THIS TEST");
    tui_wrap(10, 15, 60, 3, PAL->desc, PAL->bg,
             sel < 2 ? audiotest_about(sel) : "Return to the previous menu.");
    tui_status("Enter Play   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP)        sel = (sel + 3 - 1) % 3;
    else if (k == TUI_KEY_DOWN) sel = (sel + 1) % 3;
    else if (k == TUI_KEY_HOME) sel = 0;
    else if (k == TUI_KEY_ENTER)
    {
      if (sel == 0)      res[0] = audiotest_popup(0);
      else if (sel == 1) res[1] = audiotest_popup(1);
      else break; /* Back */
      tui_clear(); /* T45: the test popup overwrote the screen; wipe before repaint */
    }
    else if (k == TUI_KEY_ESC) break;
  }
  audiotest_shutdown();
}

static void screen_save(void)
{
  cfg_write_toast();
}

int main(void)
{
  int loaded;

  /* T50/DOS: redirect stdout + stderr to NUL so libc / SDL_Log writes (e.g. the
   * SB16 device-open banners "SB: port=0x220" etc., which are SDL_Log calls in
   * the SDL DOS audio backend) never reach the DOS console and composite over
   * the TUI on real VGA. SETUP renders only via ScreenPutChar (direct VGA), not
   * stdout, and the audio-test trace + PROFILE.LOG use their own FILE*, so this
   * affects nothing visible or logged. Mirrors the game's main.cpp freopen.
   * Must be the very first thing, before any SDL init or console output. */
  (void)freopen("NUL", "w", stdout);
  (void)freopen("NUL", "w", stderr);

  profile_detect(&g_prof);
  /* Dump the detected profile to LOGS\PROFILE.LOG before anything else so the
   * real-HW iter captures the exact displayed values (MHz-divisor calibration
   * + physical-RAM acceptance become log-witnessed). Best-effort: a write
   * failure must never block SETUP. */
  (void)profile_write_log(&g_prof);
  loaded = scfg_load(&g_cfg, CFG_PATH); /* defaults if absent */

  tui_init();

  /* First-run courtesy: no config -> seed with recommendations. */
  if (loaded < 0)
  {
    char why[160];
    recommend_apply(&g_cfg, &g_prof, why, sizeof(why));
    g_dirty = 1;
  }

  /* Main-menu screen (T25): an X-Wing-style layout -- the always-on system
   * profile PANEL pinned at the top, the menu below it, a DESCRIPTION box at
   * the bottom showing per-item help. The profile is no longer a menu item
   * (it is always visible in the panel). A custom navigation loop replaces
   * tui_menu here so the panel + menu + description can stack; keys are
   * identical (Up/Down/Home/Enter, F10 = Save and Quit, ESC = Quit). */
#define MENU_X 21
#define MENU_W 38
#define MENU_Y 10
  {
    static const char *items[] = {
      "Sound setup",
      "Sound hardware",
      "Test SFX / Music",
      "Input / joystick",
      "Performance",
      "Advanced / troubleshooting",
      "Auto-detect best settings",
      "Save and exit",
      "Quit without saving"
    };
    /* item 7: one short sentence per item (the operator's review-3 example
     * "Choose how music and sound effects play."). */
    static const char *helps[] = {
      "Choose how music and sound effects play.",
      "Set your Sound Blaster's port, IRQ, and DMA.",
      "Play a test sound effect and song to check your audio.",
      "Turn a joystick or gamepad on or off.",
      "Trade visual detail for speed.",
      "Rarely needed compatibility switches.",
      "Let SETUP pick settings for your detected hardware.",
      "Write DOSKUTSU.CFG and leave SETUP.",
      "Leave SETUP without saving changes."
    };
    const int nitems = (int)(sizeof(items) / sizeof(items[0]));

    for (;;)
    {
      int sel = 0, action = -99, i, k;

      /* T45: clear ONCE when (re)entering the menu (e.g. on return from a
       * subscreen); the nav loop below repaints in place with no flash. */
      tui_clear();
      /* Navigate until Enter / F10 / ESC (selection re-seeds to 0 each entry). */
      for (;;)
      {
        tui_titlebar("DOSKUTSU SETUP");
        /* item 5: center the panel on the screen so its SYSTEM PROFILE box
         * title shares the centering axis of the full-width title bar.
         * x = (80 - 72)/2 + 1 = 5 (symmetric 4-col margins). */
        draw_profile_panel(5, 2, 72);
        tui_box(MENU_X, MENU_Y, MENU_W, nitems + 2, "MAIN MENU");
        for (i = 0; i < nitems; ++i)
        {
          char row[MENU_W + 2];
          int fg = (i == sel) ? PAL->sel_fg : PAL->body;
          int bg = (i == sel) ? PAL->sel_bg : PAL->bg;
          /* item 1: highlight the WHOLE line (full menu box interior width). */
          snprintf(row, sizeof(row), " %-*.*s", MENU_W - 3, MENU_W - 3, items[i]);
          tui_at(MENU_X + 1, MENU_Y + 1 + i, fg, bg, row);
        }
        /* round-7 item 3a: the DESCRIPTION box matches the SYSTEM PROFILE box
         * width/position (x=5, w=72) so the two stacked boxes line up. */
        tui_box(5, MENU_Y + nitems + 2, 72, 4, "DESCRIPTION");
        tui_wrap(7, MENU_Y + nitems + 3, 68, 2, PAL->desc, PAL->bg, helps[sel]);
        /* T44: when the session has unsaved edits, flag it in the status bar so
         * the player knows "Save and exit" is needed to keep them. */
        tui_status(g_dirty ? MENU_STATUS "   * UNSAVED" : MENU_STATUS);

        k = tui_getkey();
        if (k == TUI_KEY_UP)         sel = (sel + nitems - 1) % nitems;
        else if (k == TUI_KEY_DOWN)  sel = (sel + 1) % nitems;
        else if (k == TUI_KEY_HOME)  sel = 0;
        else if (k == TUI_KEY_ENTER) { action = sel; break; }
        else if (k == TUI_KEY_F10)   { action = -2;  break; }
        else if (k == TUI_KEY_ESC)   { action = -1;  break; }
      }

      if (action == 0)      screen_sound();
      else if (action == 1) screen_hardware();
      else if (action == 2) screen_audiotest();
      else if (action == 3) edit_category(DKC_INPUT, "INPUT");
      else if (action == 4) edit_category(DKC_PERF, "PERFORMANCE");
      else if (action == 5) edit_category(DKC_COMPAT, "ADVANCED");
      else if (action == 6) screen_autodetect();
      else if (action == 7) { screen_save(); break; }   /* Save and exit       */
      else if (action == -2) { screen_save(); break; }  /* F10 = Save and Exit */
      else /* action == 8 (Quit without saving) or ESC (-1) */
      {
        /* T52: ALWAYS confirm before leaving to DOS (accident guard), even when
         * the session is clean. Default highlight No (destructive). Y = exit
         * without writing; N = stay. */
        if (tui_yesno("Quit", "Quit without saving settings?", 1))
          break;
      }
    }
  }
#undef MENU_X
#undef MENU_W
#undef MENU_Y

  tui_shutdown();
  return 0;
}
