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
#include <unistd.h> /* getcwd -- compose the DOS path a MIDI set loads from (#39) */

#include "tui.h"
#include "setupcfg.h"
#include "profile.h"
#include "recommend.h"
#include "audiotest.h"
#include "midiset.h"

#define CFG_PATH "DOSKUTSU.CFG"

static scfg_t      g_cfg;
static sysprofile_t g_prof;
static int          g_dirty; /* unsaved changes */

/* MIDI music sets discovered on disk (backlog #39). Rescanned on entry to the
 * Music screen; the conditional "MIDI music set" row + its picker read these. */
static midiset_t g_midi_sets[MIDISET_MAX];
static int       g_midi_nsets;

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
#define EDIT_STATUS "Enter Open list   Space/Left-Right Change   ESC Back"
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
  { "Auto",        "Detect the best option the card supports." },
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
 * Int rows (volume levels) have no discrete options and stay wrapped prose. */
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

/* Vertically center a menu/option box of height box_h in the open region
 * between region_top (the first content row -- just under the title bar, or
 * just under a fixed top panel like SYSTEM PROFILE) and desc_top (the top row
 * of the bottom-anchored DESCRIPTION box), so the vertical margin above and
 * below the box is equal. Phase-1 DF-UX layout: every list screen computes its
 * option-box top with this instead of hardcoding a top-anchored row, which
 * removes the big gap between a top-anchored box and the bottom DESCRIPTION
 * box. Clamps to region_top so the box never rides up over the title/panel. */
static int menu_box_top(int region_top, int desc_top, int box_h)
{
  int avail = desc_top - region_top;          /* rows between the two anchors */
  int y = region_top + (avail - box_h) / 2;   /* equal top/bottom margin      */
  if (y < region_top) y = region_top;
  return y;
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

/* R-I: open a modal pick-list for registry key `idx` and apply the chosen value
 * to the session (live). Friendly labels per key (backend names, Enabled/
 * Disabled, sample rates, perf-mode names). Returns 1 if a list was shown
 * (the caller then full-clears -- R-H), 0 if the key has no sensible discrete
 * list (a wide INT like the 0-31 volume levels -- caller keeps Left/Right). */
static int pick_value(int idx)
{
  const dkt_key_t *k = &DKT_KEYS[idx];
  const char *items[8], *raw[8];
  char buf[8][24], rbuf[8][8];
  int n = 0, i, choice, start = 0;
  const char *cur = scfg_get(&g_cfg, idx);

  if (k->type == DKT_ENUM && k->enum_vals)
  {
    int is_backend = dkt_key_ieq(k->cfg_key, "AUDIO_BACKEND", 13);
    for (i = 0; k->enum_vals[i] && n < 8; ++i)
    {
      const char *e = k->enum_vals[i];
      raw[n] = e;
      if (is_backend)
        snprintf(buf[n], sizeof(buf[n]), "%s",
                 strcmp(e, "wb") == 0      ? "MIDI (WaveBlaster)" :
                 strcmp(e, "opl3") == 0    ? "MIDI (OPL3)" :
                 strcmp(e, "organya") == 0 ? "Organya" : "Auto (detect)");
      else
        snprintf(buf[n], sizeof(buf[n]), "%s", e);
      items[n] = buf[n];
      if (strcmp(cur, e) == 0) start = n;
      ++n;
    }
  }
  else if (k->type == DKT_BOOL)
  {
    const char *l0, *v0, *l1, *v1;
    if (dkt_key_ieq(k->cfg_key, "AUDIO_OFF", 9))
      { l0 = "Enabled"; v0 = "0"; l1 = "Disabled"; v1 = "1"; }
    else if (dkt_key_ieq(k->cfg_key, "AUDIO_TIER2", 11))
      { l0 = "11025Hz"; v0 = "1"; l1 = "22050Hz"; v1 = "0"; }
    else
      { l0 = "On"; v0 = "1"; l1 = "Off"; v1 = "0"; }
    snprintf(buf[0], sizeof(buf[0]), "%s", l0); items[0] = buf[0]; raw[0] = v0;
    snprintf(buf[1], sizeof(buf[1]), "%s", l1); items[1] = buf[1]; raw[1] = v1;
    n = 2;
    start = (strcmp(cur, v0) == 0) ? 0 : 1;
  }
  else if (k->type == DKT_INT && (k->imax - k->imin) >= 0 && (k->imax - k->imin) <= 7)
  {
    int is_perf = dkt_key_ieq(k->cfg_key, "PERF_MODE", 9);
    for (i = k->imin; i <= k->imax && n < 8; ++i)
    {
      if (is_perf)
        snprintf(buf[n], sizeof(buf[n]), "%d %s", i,
                 i == 0 ? "Faithful" : i == 1 ? "Smooth" : "Fast");
      else
        snprintf(buf[n], sizeof(buf[n]), "%d", i);
      snprintf(rbuf[n], sizeof(rbuf[n]), "%d", i);
      items[n] = buf[n]; raw[n] = rbuf[n];
      if (atoi(cur) == i) start = n;
      ++n;
    }
  }
  else
    return 0; /* wide INT (e.g. 0-31 volume) -> caller keeps Left/Right */

  choice = tui_picklist(k->label, 0, 0, items, NULL, NULL, n, start,
                        0, NULL, NULL, 0);
  if (choice >= 0 && choice < n &&
      strcmp(scfg_get(&g_cfg, idx), raw[choice]) != 0)
  {
    scfg_set(&g_cfg, idx, raw[choice]);
    g_dirty = 1;
  }
  return 1;
}

/* Edit an explicit list of registry indices in place (the generic row editor).
 * Space/Left/Right change the value AND Enter opens a pick-list (R-I); edits
 * apply live to the session config, ESC returns keeping them (T44); greyed rows
 * are skipped by navigation. The Advanced screen (3.6) builds a two-category
 * index list and calls this. */
static void edit_index_list(const int *idxs, int n, const char *title)
{
  int sel = 0, i;
  scfg_t snap;        /* T52: entry snapshot for the ESC "revert this screen" path */
  int dirty_snap;
  if (n <= 0) return;
  snap = g_cfg;
  dirty_snap = g_dirty;
  while (sel < n && !cat_active(idxs[sel])) ++sel; /* start on a live row */
  if (sel >= n) sel = 0;

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place (no flash) */
  for (;;)
  {
    int k, j;
    /* D: center the option box between the title bar and the DESCRIPTION box. */
    int boxy = menu_box_top(3, TUI_DESC_TOP(7), n + 4);
    tui_titlebar(title); /* round-7 item 2: every screen shows its page title */
    tui_box(9, boxy, 64, n + 4, title); /* R-O: x=9 centers a 64-wide box (8/8 margins) */
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
       * Box x=9 w=64 -> interior 62 cells from col 10. */
      snprintf(row, sizeof(row), " %-61.61s", DKT_KEYS[idxs[i]].label);
      tui_at(10, boxy + 1 + i, lblfg, bg, row);
      tui_at(10 + 28, boxy + 1 + i, valfg, bg, val);
    }
    /* per-item help: multi-option keys get a per-line keyed list (item 3);
     * single-concept keys stay wrapped prose. */
    tui_box(TUI_DESC_X, TUI_DESC_TOP(7), TUI_DESC_W, 7, "DESCRIPTION");
    {
      int oh_n, oh_kw, hy = TUI_DESC_TOP(7) + 1;
      const helprow_t *oh = opt_help_for(DKT_KEYS[idxs[sel]].cfg_key, &oh_n, &oh_kw);
      if (oh)
        help_list(TUI_DESC_TX, hy, oh_kw, TUI_DESC_TW - oh_kw, 2, oh, oh_n);
      else
        tui_wrap(TUI_DESC_TX, hy, TUI_DESC_TW, 4, PAL->desc, PAL->bg,
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
      /* R-I: Enter opens a pick-list for the highlighted row (the discoverable
       * path). R-H: full-clear after it closes so no overlay residue remains.
       * (void j -- the cursor stays put; navigation is Up/Down.) */
      if (cat_active(idxs[sel]) && pick_value(idxs[sel]))
        tui_clear();
      (void)j;
    }
    else if (k == TUI_KEY_ESC)
    {
      /* T52: if rows changed since entry, ask Save setting? Y = keep in the
       * session, N = revert to the entry baseline. Unchanged -> silent back. */
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

/* Advanced / troubleshooting (T47 plan 3.6): the COMPAT keys, plus the two
 * former Performance rows (PERF_MODE, FIXED_TIMESTEP) at the TOP -- this is
 * where "Advanced overrides freely" lives. SETUP-only keys (NULL env_name,
 * e.g. SPEED_CLASS) are excluded so the System Speed provenance record never
 * shows up as an editable row. */
static void screen_advanced(void)
{
  int idxs[DKT_KEY_COUNT], n = 0, i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (DKT_KEYS[i].category == DKC_PERF && DKT_KEYS[i].env_name)
      idxs[n++] = i;
  for (i = 0; i < DKT_KEY_COUNT; ++i)
    if (DKT_KEYS[i].category == DKC_COMPAT && DKT_KEYS[i].env_name)
      idxs[n++] = i;
  edit_index_list(idxs, n, "ADVANCED");
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

/* R-B: the "Music" screen holds only the music-playback choices (backend,
 * Organya pre-render, audio quality). Sound enable/disable + the SFX/Music
 * volume levels moved to Custom setup (the Sound Hardware screen). */
enum
{
  SND_BACKEND = 0, /* AUDIO_BACKEND, friendly names                 */
  SND_MIDISET,     /* MIDI_SET, shown only for a MIDI backend + >=2 sets (#39) */
  SND_PRERENDER,   /* ORG_PRERENDER, active only for Organya        */
  SND_QUALITY,     /* AUDIO_TIER2                                    */
  SND_NROWS
};

static int snd_key_idx(int row)
{
  switch (row)
  {
    case SND_BACKEND:   return scfg_index("AUDIO_BACKEND");
    case SND_MIDISET:   return scfg_index("MIDI_SET");
    case SND_PRERENDER: return scfg_index("ORG_PRERENDER");
    case SND_QUALITY:   return scfg_index("AUDIO_TIER2");
    default:            return -1;
  }
}

static const char *snd_label(int row)
{
  switch (row)
  {
    case SND_BACKEND:   return "Music backend";
    case SND_MIDISET:   return "MIDI music set";
    case SND_PRERENDER: return "Organya pre-render";
    case SND_QUALITY:   return "Audio quality";
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

/* Friendly label of the currently-selected MIDI set (the value stored in
 * MIDI_SET, mapped to its discovered set's label). Falls back to the raw value
 * when the configured set is not present on disk. */
static const char *snd_midiset_name(const char *v)
{
  int si = midiset_index_by_value(g_midi_sets, g_midi_nsets, v);
  if (si >= 0) return g_midi_sets[si].label;
  return (v && v[0]) ? v : MIDISET_DEFAULT_VALUE;
}

static void snd_value(int row, char *out, int cap)
{
  int idx = snd_key_idx(row);
  const char *v = scfg_get(&g_cfg, idx);
  if (row == SND_BACKEND)
    snprintf(out, (size_t)cap, "%s", snd_backend_name(v));
  else if (row == SND_MIDISET)
    snprintf(out, (size_t)cap, "%s", snd_midiset_name(v));
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
    case SND_BACKEND:
      return "How music is played. MIDI (WaveBlaster) = wavetable music from a "
             "daughterboard: best quality, needs that hardware. MIDI (OPL3) = "
             "FM synth built into most Sound Blasters: works everywhere, the "
             "classic sound. Organya = Cave Story's original synth: most "
             "faithful (more demanding on CPU). Auto = pick the best option the "
             "card supports.";
    case SND_MIDISET:
      return "Which set of MIDI music the WaveBlaster / OPL3 backend plays. "
             "WiiWare = polished re-arrangements (Yann van der Cruyssen). "
             "OrgMIDI = note-for-note transcription of the original Organya "
             "music. Only sets you have installed under data appear here.";
    case SND_PRERENDER:
      return "Organya only. The first time each song plays it is rendered to a "
             "disk cache so playback stays smooth afterward. Costs a few seconds "
             "and some disk space per song, less demanding on CPU at playback.";
    case SND_QUALITY:
      return "On = 11025 Hz mono mixing (default, lighter on the CPU). Off = a "
             "higher legacy sample rate (heavier, only a marginal gain). Leave "
             "On unless there is CPU headroom to spare.";
    default:
      return "";
  }
}

/* A row is selectable when its preconditions hold; others render dimmed and
 * are skipped by navigation. Pre-render + Audio quality are Organya-only (the
 * MIDI backends are chip-synthesized, so the PCM device rate does not apply).
 * The MIDI music-set row is the mirror image: applicable only to a MIDI
 * backend (anything but Organya). */
static int snd_active(int row)
{
  int organya = strcmp(scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND")),
                       "organya") == 0;
  if (row == SND_MIDISET)   return !organya;
  if (row == SND_PRERENDER) return organya;
  if (row == SND_QUALITY)   return organya;
  return 1;
}

/* A row is VISIBLE (rendered at all) when it is meaningful on this machine.
 * The MIDI music-set row is omitted entirely unless at least two MIDI sets are
 * installed on disk -- a first-timer with one set never sees a dead choice
 * (#39 sec.4). Every other row is always present (greying handles the rest). */
static int snd_visible(int row)
{
  if (row == SND_MIDISET) return g_midi_nsets >= 2;
  return 1;
}

/* Move the selection by dir, skipping rows that are not both visible AND
 * active (greyed or omitted rows). */
static int snd_step(int sel, int dir)
{
  int i, r = sel;
  for (i = 0; i < SND_NROWS; ++i)
  {
    r = (r + dir + SND_NROWS) % SND_NROWS;
    if (snd_visible(r) && snd_active(r)) return r;
  }
  return sel;
}

/* Cycle MIDI_SET among the discovered sets (Space / Left / Right on the row). */
static void snd_midiset_cycle(int dir)
{
  int idx = scfg_index("MIDI_SET");
  int cur;
  if (g_midi_nsets <= 0) return;
  cur = midiset_index_by_value(g_midi_sets, g_midi_nsets, scfg_get(&g_cfg, idx));
  if (cur < 0) cur = 0;
  cur = (cur + dir + g_midi_nsets) % g_midi_nsets;
  if (strcmp(scfg_get(&g_cfg, idx), g_midi_sets[cur].value) != 0)
  {
    scfg_set(&g_cfg, idx, g_midi_sets[cur].value);
    g_dirty = 1;
  }
}

/* Compose the absolute DOS path a MIDI set loads from: <cwd>\DATA\<DIR>,
 * uppercased with backslashes (e.g. C:\DOSKUTSU\DATA\MIDI). SETUP runs in the
 * game directory, so getcwd() yields the install root the engine resolves data
 * against. Falls back to a bare DATA\<DIR> if the cwd cannot be read. */
static void midiset_dos_path(const char *dir, char *out, int cap)
{
  char cwd[96];
  char up[MIDISET_DIR_MAX];
  int  i, n;

  if (getcwd(cwd, sizeof(cwd)) == NULL || cwd[0] == '\0')
    cwd[0] = '\0';
  for (i = 0; cwd[i]; ++i)
  {
    if (cwd[i] == '/') cwd[i] = '\\';
    else if (cwd[i] >= 'a' && cwd[i] <= 'z') cwd[i] = (char)(cwd[i] - 'a' + 'A');
  }
  n = (int)strlen(cwd);
  /* strip a trailing backslash unless it is a bare drive root "X:\" */
  if (n >= 1 && cwd[n - 1] == '\\' && !(n == 3 && cwd[1] == ':'))
    cwd[--n] = '\0';
  for (i = 0; dir[i] && i < (int)sizeof(up) - 1; ++i)
    up[i] = (dir[i] >= 'a' && dir[i] <= 'z') ? (char)(dir[i] - 'a' + 'A') : dir[i];
  up[i] = '\0';

  if (n == 0)
    snprintf(out, (size_t)cap, "DATA\\%s", up);
  else if (cwd[n - 1] == '\\')          /* root already ends with a backslash */
    snprintf(out, (size_t)cap, "%sDATA\\%s", cwd, up);
  else
    snprintf(out, (size_t)cap, "%s\\DATA\\%s", cwd, up);
}

/* #39 A: build the per-set DESCRIPTION list shown when the MIDI-music-set row
 * is highlighted -- one row per installed set (matching the Music-backend help
 * style): friendly name in the keyword column, then the DOS path it loads from
 * + its track count, and an incomplete-set warning when it has fewer tracks
 * than the fullest installed set. No "=" sign. descbuf rows must be >= 128.
 * Returns the number of rows filled; *keyw gets the keyword-column width. */
static int snd_midiset_help(helprow_t *rows, char descbuf[][192], int cap,
                            int *keyw)
{
  int i, n = (g_midi_nsets < cap) ? g_midi_nsets : cap;
  int refcount = 0, kw = 9;

  for (i = 0; i < n; ++i)
  {
    int w = (int)strlen(g_midi_sets[i].label) + 2;
    if (g_midi_sets[i].mid_count > refcount) refcount = g_midi_sets[i].mid_count;
    if (w > kw) kw = w;
  }
  for (i = 0; i < n; ++i)
  {
    char path[64];
    int  miss = refcount - g_midi_sets[i].mid_count;
    midiset_dos_path(g_midi_sets[i].dir, path, sizeof(path));
    if (miss > 0)
      snprintf(descbuf[i], 192,
               "%s, %d tracks -- %d fewer than the fullest set; those songs "
               "play no music.", path, g_midi_sets[i].mid_count, miss);
    else
      snprintf(descbuf[i], 192, "%s, %d tracks.", path,
               g_midi_sets[i].mid_count);
    rows[i].key  = g_midi_sets[i].label;
    rows[i].desc = descbuf[i];
  }
  *keyw = kw;
  return n;
}

/* Modal pick-list of the discovered MIDI sets (Enter on the row). Tags the
 * current set "(current)"; the DESCRIPTION shows each set's track count and an
 * incomplete-set WARNING when it has fewer tracks than the fullest installed
 * set (#39 Q-A4). Writes MIDI_SET on a change. Returns 1 (a list was shown). */
static int snd_pick_midiset(void)
{
  const char *items[MIDISET_MAX];
  const char *tags[MIDISET_MAX];
  const char *descs[MIDISET_MAX];
  char        dbuf[MIDISET_MAX][192];
  int idx = scfg_index("MIDI_SET");
  int i, choice, start = 0, refcount = 0;

  if (g_midi_nsets <= 0) return 0;

  for (i = 0; i < g_midi_nsets; ++i)
    if (g_midi_sets[i].mid_count > refcount) refcount = g_midi_sets[i].mid_count;

  i = midiset_index_by_value(g_midi_sets, g_midi_nsets, scfg_get(&g_cfg, idx));
  if (i >= 0) start = i;

  for (i = 0; i < g_midi_nsets; ++i)
  {
    int  miss = refcount - g_midi_sets[i].mid_count;
    char path[64];
    midiset_dos_path(g_midi_sets[i].dir, path, sizeof(path));
    items[i] = g_midi_sets[i].label;
    tags[i]  = (i == start) ? "(current)" : "";
    if (miss > 0)
      snprintf(dbuf[i], sizeof(dbuf[i]),
               "%s, %d tracks -- %d fewer than the fullest set; those songs "
               "play no music.", path, g_midi_sets[i].mid_count, miss);
    else
      snprintf(dbuf[i], sizeof(dbuf[i]), "%s, %d tracks.", path,
               g_midi_sets[i].mid_count);
    descs[i] = dbuf[i];
  }

  choice = tui_picklist("MIDI music set", 0, 0, items, tags, descs,
                        g_midi_nsets, start, 0, NULL, NULL, 0);
  if (choice >= 0 && choice < g_midi_nsets &&
      strcmp(scfg_get(&g_cfg, idx), g_midi_sets[choice].value) != 0)
  {
    scfg_set(&g_cfg, idx, g_midi_sets[choice].value);
    g_dirty = 1;
  }
  return 1;
}

static void screen_sound(void)
{
  int sel = SND_BACKEND;
  scfg_t snap = g_cfg;          /* T52: entry snapshot for the ESC revert path */
  int dirty_snap = g_dirty;

  /* #39: discover the MIDI music sets present on disk (data/midi/, data/orgmid/).
   * Rescan once on entry -- the on-disk set list does not change mid-screen. */
  g_midi_nsets = midiset_scan("data", g_midi_sets, MIDISET_MAX);

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place (no flash) */
  for (;;)
  {
    int i, k, hy, p, nvis = 0, vis[SND_NROWS], boxy;

    /* Build the visible-row list (the MIDI-set row is omitted entirely unless
     * >=2 sets are installed), so omitted rows leave no gap. */
    for (i = 0; i < SND_NROWS; ++i)
      if (snd_visible(i)) vis[nvis++] = i;

    /* D: vertically center the option box between the title bar and the
     * bottom-anchored DESCRIPTION box (no more top-anchored gap). */
    hy = TUI_DESC_TOP(8);
    boxy = menu_box_top(3, hy, nvis + 2);

    tui_titlebar("MUSIC");
    tui_box(9, boxy, 64, nvis + 2, "MUSIC"); /* R-O: centered (8/8 margins) */
    for (p = 0; p < nvis; ++p)
    {
      char row[80], val[28];
      int  ri = vis[p];
      int active = snd_active(ri);
      int selrow = (active && ri == sel);
      int lblfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
      int bg = selrow ? PAL->sel_bg : PAL->bg;
      int valfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->value);
      snd_value(ri, val, (int)sizeof(val));
      /* item 1: highlight the WHOLE line (full box interior width). Full-width
       * label bar, then overpaint the value in its column. */
      snprintf(row, sizeof(row), " %-61.61s", snd_label(ri));
      tui_at(10, boxy + 1 + p, lblfg, bg, row);
      tui_at(10 + 26, boxy + 1 + p, valfg, bg, val);
    }

    /* per-item help: multi-option rows get a per-line keyed list, each desc
     * wrapped + clipped inside the box with indented continuation (item 4);
     * single-concept rows stay wrapped prose. */
    /* R-J/R-N: ONE bottom-anchored DESCRIPTION box (rows 16-23). The per-row
     * keyed help fills the top; the Organya advisory (R-N: folded in here, no
     * separate NOTE box) prints as a trailing warn-colored line at the bottom.
     * tui_box repaints the interior each frame, so a vanished advisory leaves
     * no residue. */
    tui_box(TUI_DESC_X, hy, TUI_DESC_W, 8, "DESCRIPTION");
    {
      int oh_n, oh_kw;
      const helprow_t *oh;
      if (sel == SND_MIDISET && g_midi_nsets > 0)
      {
        /* #39 A: list each installed set on its own line (DOS path + count),
         * matching the Music-backend help style. */
        helprow_t mrows[MIDISET_MAX];
        char      mbuf[MIDISET_MAX][192];
        int       mkw, mn = snd_midiset_help(mrows, mbuf, MIDISET_MAX, &mkw);
        help_list(TUI_DESC_TX, hy + 1, mkw, TUI_DESC_TW - mkw, 2, mrows, mn);
      }
      else if ((oh = opt_help_for(DKT_KEYS[snd_key_idx(sel)].cfg_key,
                                  &oh_n, &oh_kw)) != NULL)
        help_list(TUI_DESC_TX, hy + 1, oh_kw, TUI_DESC_TW - oh_kw, 2, oh, oh_n);
      else
        tui_wrap(TUI_DESC_TX, hy + 1, TUI_DESC_TW, 4, PAL->desc, PAL->bg, snd_help(sel));
    }
    {
      const char *backend = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
      int is_org = strcmp(backend, "organya") == 0;
      const char *note = NULL;
      if (is_org && sel == SND_QUALITY)
        note = "Note: the SETUP Organya test plays a pre-rendered clip, so it "
               "will not reflect this rate.";
      else if (is_org && sel == SND_PRERENDER && g_prof.cpu_class != CPU_586)
        note = "Note: pre-render is recommended for slower CPUs (uses a disk "
               "cache, increases load times).";
      if (note)
        tui_wrap(TUI_DESC_TX, hy + 5, TUI_DESC_TW, 2, PAL->warn_fg, PAL->bg, note);
    }
    tui_status(EDIT_STATUS);

    k = tui_getkey();
    if (k == TUI_KEY_UP)        sel = snd_step(sel, -1);
    else if (k == TUI_KEY_DOWN) sel = snd_step(sel, +1);
    else if (k == TUI_KEY_HOME) sel = SND_BACKEND; /* always selectable */
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      /* T62: only Space / Left / Right cycle the highlighted row's value. */
      if (snd_active(sel))
      {
        if (sel == SND_MIDISET)
          /* #39: MIDI_SET is a DKT_STR over the discovered logical sets, so it
           * has its own cycle (the generic cycle_value does not handle STR). */
          snd_midiset_cycle((k == TUI_KEY_LEFT) ? -1 : +1);
        else
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
      /* R-I: Enter opens a pick-list for the highlighted row; R-H: full-clear
       * after it closes (no overlay residue). Organya auto-suggest rides along
       * on a backend change, same as the cycle path. */
      int shown;
      if (!snd_active(sel))
        shown = 0;
      else if (sel == SND_MIDISET)
        shown = snd_pick_midiset(); /* #39: dedicated set picker (STR key) */
      else
        shown = pick_value(snd_key_idx(sel));
      if (shown)
      {
        if (sel == SND_BACKEND)
          recommend_org_prerender(&g_cfg, &g_prof);
        tui_clear();
      }
    }
    else if (k == TUI_KEY_ESC)
    {
      /* T52: changed since entry -> Save setting? (Y keep / N revert to the
       * entry baseline); else silent back. */
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

/* ---- Sound Hardware screen (BLASTER A/I/DMA/P/T; DMA derives D+H) --- */

/* Traditional Sound Blaster family name for a BLASTER T-code. */
static const char *sb_type_name(int t)
{
  return (t == 6) ? "Sound Blaster 16"
       : (t == 4) ? "Sound Blaster Pro"
       : (t == 3) ? "Sound Blaster Pro 2.0"
       : (t == 2) ? "Sound Blaster 2.0"
       : (t == 1) ? "Sound Blaster" : "?";
}

/* Allowed values per BLASTER field. A and P are hex (i/o ports); I/D/H/T are
 * decimal. P value -1 == "none" (omit the MPU-401 MIDI field entirely). Value
 * lists are in ASCENDING display order; `defval` marks the conventional value
 * that the pick-list tags "(default)" (so the order is not tied to the tag).
 * Sets are from the operator's Doom / iMUSE reference shots (R-A). */
typedef struct
{
  char        letter;  /* BLASTER field letter */
  const char *label;
  const int  *vals;
  int         nvals;
  int         hex;     /* 1 -> render/emit as hex */
  int         defval;  /* conventional value tagged "(default)" in the picker */
} hwfield_t;

static const int hw_port_vals[] = { 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x280 };
static const int hw_irq_vals[]  = { 2, 5, 7, 10 };
/* R-A operator refinement: ONE "DMA channel" field (Doom/iMUSE do not split
 * 8/16-bit). The single pick derives BOTH the SB16 D (8-bit) + H (16-bit)
 * BLASTER slots in hw_compose: a low pick (0/1/3) is D (H defaults to 5); a
 * high pick (5/6/7) is H (D defaults to 1). Invariant: D in {0,1,3},
 * H in {5,6,7}, both always present + valid. */
static const int hw_dma_vals[]  = { 0, 1, 3, 5, 6, 7 };
static const int hw_midi_vals[] = { -1, 0x220, 0x230, 0x240, 0x250, 0x300,
                                    0x320, 0x330, 0x332, 0x334, 0x336, 0x340, 0x360 };
static const int hw_type_vals[] = { 1, 2, 3, 4, 6 };

/* Field order is A, I, DMA, P, T (cur[] indices 0..4). The DMA field uses
 * letter 'D' as a marker; hw_compose/hw_seed handle the D/H derivation. */
static const hwfield_t HW_FIELDS[] = {
  { 'A', "I/O port",          hw_port_vals, 7,  1, 0x220 },
  { 'I', "IRQ",               hw_irq_vals,  4,  0, 5     },
  { 'D', "DMA channel",       hw_dma_vals,  6,  0, 1     },
  { 'P', "MPU-401 MIDI port", hw_midi_vals, 13, 1, 0x330 },
  { 'T', "Card type",         hw_type_vals, 5,  0, 6     }
};
#define HW_NFIELDS ((int)(sizeof(HW_FIELDS) / sizeof(HW_FIELDS[0])))
#define HW_DMA_IDX 2 /* cur[] index of the single DMA field */

/* R-M: the "Override AUTOEXEC.BAT" row is REMOVED -- the BLASTER fields are
 * always editable and always composed into the saved config (no override gate,
 * no AUTOEXEC language). Row layout: 0..HW_NFIELDS-1 BLASTER fields (A/I/DMA/
 * P/T), then the three R-B live rows (Sound on/off + the two SB16 mixer volume
 * levels, moved here from the Music screen). */
#define HW_ROW_SOUND  (HW_NFIELDS)
#define HW_ROW_SFXVOL (HW_NFIELDS + 1)
#define HW_ROW_MUSVOL (HW_NFIELDS + 2)
#define HW_NROWS      (HW_NFIELDS + 3)

/* The scfg key for a live Custom-setup row (Sound/SFX/Music), or NULL. */
static const char *hw_live_key(int row)
{
  if (row == HW_ROW_SOUND)  return "AUDIO_OFF";
  if (row == HW_ROW_SFXVOL) return "SB16_VOICE_VOL";
  if (row == HW_ROW_MUSVOL) return "SB16_FM_VOL";
  return NULL;
}

/* Is a Custom-setup row selectable? R-M: the BLASTER fields are always live
 * (no override gate). The two volume rows grey when sound is disabled; the
 * field rows + the Sound on/off row are always live. */
static int hw_row_active(int row)
{
  if (row < HW_NFIELDS)    return 1; /* A/I/DMA/P/T -- always editable */
  if (row == HW_ROW_SOUND) return 1;
  return strcmp(scfg_get(&g_cfg, scfg_index("AUDIO_OFF")), "1") != 0; /* vols */
}

static int hw_find_index(const hwfield_t *f, int val)
{
  int i;
  for (i = 0; i < f->nvals; ++i)
    if (f->vals[i] == val) return i;
  return 0;
}

static void hw_fmt(const hwfield_t *f, int v, char *out, int cap)
{
  if (f->letter == 'P' && v < 0) { snprintf(out, (size_t)cap, "none"); return; }
  if (f->letter == 'T')
  {
    /* Traditional Sound Blaster family names, "Name (Tn)" format (R-A). */
    snprintf(out, (size_t)cap, "%s (T%d)", sb_type_name(v), v);
    return;
  }
  if (f->hex) snprintf(out, (size_t)cap, "0x%X", v);
  else        snprintf(out, (size_t)cap, "%d", v);
}

/* Seed the per-field VALUES from an existing BLASTER string. cur[] holds the
 * actual field value (not a vals[] index), so an "Other..." port outside the
 * standard list round-trips intact (T47 plan 3.3a). */
static void hw_seed_from_str(const char *bl, int *cur)
{
  const char *p = bl;
  int dval = -1, hval = -1;
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
    if (L == 'D') { dval = v; continue; } /* D + H collapse into the single */
    if (L == 'H') { hval = v; continue; } /* DMA pick (derived below)       */
    for (fi = 0; fi < HW_NFIELDS; ++fi)
      if (HW_FIELDS[fi].letter == L) { cur[fi] = v; break; }
  }
  /* Reconstruct the single DMA pick: an EXPLICIT high channel (H 6/7) wins;
   * otherwise the 8-bit D (the default H5 case maps back to D, so the common
   * D1 H5 reloads as pick 1). */
  if (hval >= 6)      cur[HW_DMA_IDX] = hval;
  else if (dval >= 0) cur[HW_DMA_IDX] = dval;
  else if (hval >= 0) cur[HW_DMA_IDX] = hval;
}

/* Compose "A220 I5 D1 H5 P330 T6" from the field VALUES, deriving the SB16
 * D (8-bit) + H (16-bit) slots from the single DMA pick (P omitted when
 * "none"). Both D + H are always emitted, valid, and distinct. */
static void hw_compose(const int *cur, char *out, int cap)
{
  int port    = cur[0];
  int irq     = cur[1];
  int dmapick = cur[HW_DMA_IDX];
  int midi    = cur[3];
  int type    = cur[4];
  int d, h, n;
  if (dmapick >= 5) { h = dmapick; d = 1; } /* high pick -> 16-bit H; D->1 */
  else              { d = dmapick; h = 5; } /* low pick  -> 8-bit D;  H->5 */
  n = snprintf(out, (size_t)cap, "A%X I%d D%d H%d", port, irq, d, h);
  if (midi >= 0 && n < cap) n += snprintf(out + n, (size_t)(cap - n), " P%X", midi);
  if (n < cap)              snprintf(out + n, (size_t)(cap - n), " T%d", type);
}

/* R-M: write the composed BLASTER to the session live whenever a field is
 * edited (no override gate -- the Custom-setup fields are always authoritative
 * + always saved). */
static void hw_save_blaster(const int *cur)
{
  char c[SCFG_VAL_MAX];
  hw_compose(cur, c, (int)sizeof(c));
  scfg_set(&g_cfg, scfg_index("BLASTER"), c);
  g_dirty = 1;
}

/* First-timer help for the highlighted Custom-setup row -- one short clause. */
static const char *hw_help(int sel)
{
  if (sel == HW_ROW_SOUND)
    return "Turn all game audio on or off. Disabled means no music and no "
           "sound effects (less demanding on CPU). Most players want Enabled.";
  if (sel == HW_ROW_SFXVOL)
    return "Sound Blaster 16 mixer level for digital sound effects (0-31). "
           "Lower it if the effects drown out the music.";
  if (sel == HW_ROW_MUSVOL)
    return "Sound Blaster 16 mixer level for OPL3 FM music (0-31). Lower it if "
           "the music is too loud next to the sound effects.";
  switch (HW_FIELDS[sel].letter) /* R-M: field rows are 0-based (no override) */
  {
    case 'A': return "Sound Blaster I/O port, almost always 0x220.";
    case 'I': return "Interrupt (IRQ) line, usually 5 or 7.";
    case 'D': return "DMA channel. 0/1/3 are 8-bit, 5/6/7 are 16-bit; 1 is the "
                     "usual default.";
    case 'P': return "MPU-401 MIDI port (usually 0x330), or none. Needed for "
                     "WaveBlaster music.";
    case 'T': return "Sound card type. Sound Blaster 16 works for most cards.";
    default:  return "";
  }
}

/* Per-field "Other..." hex validation (T47 plan Part 2): A = 0x200-0x2F0,
 * P = 0x300-0x3F0, both a multiple of 0x10. IRQ/DMA/HDMA/type get NO Other
 * (the ISA-valid set is fixed). Returns 1 + the parsed value when valid. */
static int hw_other_valid(char letter, const char *s, int *out)
{
  char *endp;
  long v = strtol(s, &endp, 16);
  if (endp == s || (v % 0x10) != 0) return 0;
  if (letter == 'A' && v >= 0x200 && v <= 0x2F0) { *out = (int)v; return 1; }
  if (letter == 'P' && v >= 0x300 && v <= 0x3F0) { *out = (int)v; return 1; }
  return 0;
}

/* DF-style pick-list for one BLASTER field (T47 plan 3.3a): show every legal
 * value, tag the detected one "(detected)" (else the conventional f->defval
 * "(default)"), and on the A / P ports offer an "Other..." hex free-entry
 * (re-prompts on a bad value). Returns the chosen value, or `value` unchanged
 * on ESC/cancel. detected = the profiled value, or -999 if none detected. */
#define HW_PICK_MAX 16 /* >= max f->nvals (MPU list is 13) */
static int hw_pick(int field, int value, int detected)
{
  const hwfield_t *f = &HW_FIELDS[field];
  const char *items[HW_PICK_MAX], *tags[HW_PICK_MAX];
  char labels[HW_PICK_MAX][28], tagbuf[HW_PICK_MAX][16], other[16];
  int  allow_other = (f->letter == 'A' || f->letter == 'P');
  int  i, start = hw_find_index(f, value), choice, nv = f->nvals;
  const char *prompt = (f->letter == 'A')
    ? "Type a hex I/O port 200-2F0 (multiple of 10), e.g. 220."
    : "Type a hex MPU port 300-3F0 (multiple of 10), e.g. 330.";

  if (nv > HW_PICK_MAX) nv = HW_PICK_MAX; /* defensive bound */
  for (i = 0; i < nv; ++i)
  {
    hw_fmt(f, f->vals[i], labels[i], (int)sizeof(labels[i]));
    items[i] = labels[i];
    if (detected != -999 && f->vals[i] == detected)
      snprintf(tagbuf[i], sizeof(tagbuf[i]), "(detected)");
    else if (f->vals[i] == f->defval)
      snprintf(tagbuf[i], sizeof(tagbuf[i]), "(default)");
    else tagbuf[i][0] = '\0';
    tags[i] = tagbuf[i][0] ? tagbuf[i] : NULL;
  }

  for (;;)
  {
    choice = tui_picklist(f->label, 0, 0, items, tags, NULL, nv, start,
                          allow_other, prompt, other, (int)sizeof(other));
    if (choice == TUI_PICK_OTHER)
    {
      int v;
      if (hw_other_valid(f->letter, other, &v)) return v;
      {
        const char *lines[1];
        lines[0] = (f->letter == 'A')
          ? "Enter a hex port 200-2F0, a multiple of 10 (e.g. 220, 240)."
          : "Enter a hex MPU port 300-3F0, a multiple of 10 (e.g. 330).";
        tui_message("Invalid port", lines, 1);
      }
      continue; /* re-open the list */
    }
    if (choice >= 0 && choice < nv) return f->vals[choice];
    return value; /* ESC / cancel -> unchanged */
  }
}

static void screen_hardware(void)
{
  int cur[HW_NFIELDS];
  int det[HW_NFIELDS];   /* profiled value per field (for the (detected) tag) */
  int sel = 0, i;
  const char *cfgbl = scfg_get(&g_cfg, scfg_index("BLASTER"));

  /* Seed actual VALUES from the profiler, then override from any cfg BLASTER.
   * Fields: 0 A, 1 I, 2 DMA pick, 3 P, 4 T. The DMA pick derives from the
   * detected channels (an explicit 16-bit hdma 6/7 wins, else the 8-bit dma). */
  cur[0] = g_prof.snd_base ? g_prof.snd_base : 0x220;
  cur[1] = g_prof.snd_irq  ? g_prof.snd_irq  : 5;
  cur[HW_DMA_IDX] = (g_prof.snd_hdma >= 6) ? g_prof.snd_hdma : g_prof.snd_dma;
  cur[3] = g_prof.has_waveblaster ? 0x330 : -1;
  cur[4] = g_prof.snd_type ? g_prof.snd_type : 6;
  if (cfgbl[0]) hw_seed_from_str(cfgbl, cur);

  /* The detected value per field (or -999 when nothing was detected) drives the
   * "(detected)" tag in the pick-list. */
  det[0] = g_prof.snd_detected ? g_prof.snd_base : -999;
  det[1] = g_prof.snd_detected ? g_prof.snd_irq  : -999;
  det[HW_DMA_IDX] = g_prof.snd_detected
                    ? ((g_prof.snd_hdma >= 6) ? g_prof.snd_hdma : g_prof.snd_dma)
                    : -999;
  det[3] = g_prof.has_waveblaster ? 0x330 : -999;
  det[4] = g_prof.snd_detected ? g_prof.snd_type : -999;

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place (no flash) */
  for (;;)
  {
    int k;
    /* D: center the option box between the title bar and the DESCRIPTION box. */
    int boxy = menu_box_top(3, TUI_DESC_TOP(5), HW_NFIELDS + 6);
    tui_titlebar("SOUND HARDWARE");
    tui_box(9, boxy, 64, HW_NFIELDS + 6, "SOUND"); /* R-O: centered (8/8 margins) */

    /* Rows 0..N-1: the BLASTER fields. R-M: always editable (no override). */
    for (i = 0; i < HW_NFIELDS; ++i)
    {
      char row[80], val[28];
      int  selrow = (sel == i);
      int  fg  = selrow ? PAL->sel_fg : PAL->body;
      int  bg  = selrow ? PAL->sel_bg : PAL->bg;
      int  vfg = selrow ? PAL->sel_fg : PAL->value;
      hw_fmt(&HW_FIELDS[i], cur[i], val, (int)sizeof(val));
      snprintf(row, sizeof(row), " %-61.61s", HW_FIELDS[i].label);
      tui_at(10, boxy + 1 + i, fg, bg, row);
      tui_at(10 + 28, boxy + 1 + i, vfg, bg, val);
    }
    /* Rows after the fields (R-B): Sound on/off + the SB16 mixer volume levels,
     * edited live; the two volume rows grey when sound is disabled. */
    {
      static const char *xlabel[3] = { "Sound", "SFX volume", "Music volume" };
      int base = boxy + 1 + HW_NFIELDS, xi;
      for (xi = 0; xi < 3; ++xi)
      {
        int rowno  = HW_ROW_SOUND + xi;
        int active = hw_row_active(rowno);
        int selrow = (active && sel == rowno);
        int fg  = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
        int bg  = selrow ? PAL->sel_bg : PAL->bg;
        int vfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->value);
        int kidx = scfg_index(hw_live_key(rowno));
        char row[80], val[28];
        if (rowno == HW_ROW_SOUND) /* AUDIO_OFF shown inverted */
          snprintf(val, sizeof(val), "%s",
                   strcmp(scfg_get(&g_cfg, kidx), "1") == 0 ? "Disabled" : "Enabled");
        else
          fmt_value(kidx, val, (int)sizeof(val));
        snprintf(row, sizeof(row), " %-61.61s", xlabel[xi]);
        tui_at(10, base + xi, fg, bg, row);
        tui_at(10 + 28, base + xi, vfg, bg, val);
      }
    }

    /* R-N/R-J: one DESCRIPTION box, bottom-anchored. R-P: the RESULTING CONFIG
     * preview box was removed (the BLASTER is still composed + written to the
     * config on each field edit -- hw_save_blaster -- just no longer previewed
     * on screen). With it gone, DESCRIPTION pins to the standard bottom. */
    {
      int helpy = TUI_DESC_TOP(5);
      tui_box(TUI_DESC_X, helpy, TUI_DESC_W, 5, "DESCRIPTION");
      tui_wrap(TUI_DESC_TX, helpy + 1, TUI_DESC_TW, 3, PAL->desc, PAL->bg, hw_help(sel));
    }
    tui_status("Enter Open list   Space/Left-Right Change   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN)
    {
      int dir = (k == TUI_KEY_UP) ? -1 : +1, j;
      for (j = 0; j < HW_NROWS; ++j)
      {
        sel = (sel + dir + HW_NROWS) % HW_NROWS;
        if (hw_row_active(sel)) break;
      }
    }
    else if (k == TUI_KEY_HOME) sel = 0;
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      /* Space / Left / Right cycle the highlighted value in place. A BLASTER
       * field steps via the value index + writes the composed BLASTER live; the
       * live rows (Sound/volume) cycle their scfg key directly. */
      int dir = (k == TUI_KEY_LEFT) ? -1 : +1;
      if (sel < HW_NFIELDS)
      {
        const hwfield_t *f = &HW_FIELDS[sel];
        int idx = (hw_find_index(f, cur[sel]) + dir + f->nvals) % f->nvals;
        cur[sel] = f->vals[idx];
        hw_save_blaster(cur);
      }
      else if (hw_row_active(sel))
        cycle_value(scfg_index(hw_live_key(sel)), dir);
    }
    else if (k == TUI_KEY_ENTER)
    {
      /* R-I: Enter opens the pick-list. A field row -> the DF hardware picker
       * (writes the composed BLASTER live); the Sound row -> Enabled/Disabled.
       * R-H: full-clear after any pick-list closes (no overlay residue). */
      if (sel < HW_NFIELDS)
      {
        cur[sel] = hw_pick(sel, cur[sel], det[sel]);
        hw_save_blaster(cur);
        tui_clear();
      }
      else if (sel == HW_ROW_SOUND)
      {
        if (pick_value(scfg_index("AUDIO_OFF"))) tui_clear();
      }
    }
    else if (k == TUI_KEY_ESC)
      return; /* T44: edits are live in the session; main-menu save commits */
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

/* Parse one BLASTER field (A/I/D/H/P/T) from the configured BLASTER string,
 * returning `fallback` if absent. The field letters are single upper/lower
 * case and values are hex (A/P) or decimal (I/D/H/T) digits. Used to surface
 * the configured sound hardware in the profile panel (R-D) + the Sound submenu
 * banner (R-C). */
static int cfg_blaster_field(char letter, int fallback)
{
  const char *p = scfg_get(&g_cfg, scfg_index("BLASTER"));
  int base = (letter == 'A' || letter == 'P') ? 16 : 10;
  char lower = (char)(letter + 32);
  for (; p && *p; ++p)
    if (*p == letter || *p == lower)
      return (int)strtol(p + 1, NULL, base);
  return fallback;
}

/* The configured card type (T-code), I/O base, IRQ, 8-bit DMA -- each from the
 * cfg BLASTER, falling back to the detected profile value. */
static int cfg_card_type(void) { return cfg_blaster_field('T', g_prof.snd_type ? g_prof.snd_type : 6); }
static int cfg_io_port(void)   { return cfg_blaster_field('A', g_prof.snd_base ? g_prof.snd_base : 0x220); }
static int cfg_irq(void)       { return cfg_blaster_field('I', g_prof.snd_irq  ? g_prof.snd_irq  : 5); }

/* The single "DMA channel" pick reconstructed from the configured BLASTER D + H
 * slots (matches the Custom-setup DMA field): an explicit 16-bit channel (6/7)
 * wins, else the 8-bit D. */
static int cfg_dma(void)
{
  int d = cfg_blaster_field('D', g_prof.snd_dma);
  int h = cfg_blaster_field('H', g_prof.snd_hdma);
  return (h >= 6) ? h : d;
}

/* Friendly name for the configured music backend (AUDIO_BACKEND). */
static const char *cfg_backend_name(void)
{
  const char *b = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  if (strcmp(b, "wb") == 0)      return "WaveBlaster";
  if (strcmp(b, "opl3") == 0)    return "OPL3";
  if (strcmp(b, "organya") == 0) return "Organya";
  return "Auto";
}

/* ---- System Speed presets (T47 plan 3.4) ---------------------------- *
 * Selecting a class writes concrete values ONCE into the session and records
 * the class name in the SETUP-only SPEED_CLASS key. Advanced may override the
 * perf keys freely afterward; SPEED_CLASS is a record of provenance, not a
 * constraint. The macro deliberately does NOT touch sound keys (those stay a
 * Sound-menu decision). */
typedef struct { const char *cls; const char *label; int perf; const char *desc; } speedrow_t;
static const speedrow_t SPEED_ROWS[] = {
  { "slow",     "Slow",      2,
    "Good for a slow 486 (DX2-50 or below). Drops the most detail to keep play smooth." },
  { "normal",   "Normal",    1,
    "Good for a 486DX2-66 or Am5x86. Trims some decorative detail for speed." },
  { "fast",     "Fast",      1,
    "Good for a fast 486 with VLB video, or a Pentium. The tested sweet spot." },
  { "veryfast", "Very Fast", 0,
    "Good for a fast Pentium where full faithful detail is affordable." }
};
#define SPEED_NROWS ((int)(sizeof(SPEED_ROWS) / sizeof(SPEED_ROWS[0])))

/* Index of a class string in SPEED_ROWS, or -1 (e.g. the "notset" sentinel). */
static int speed_row_index(const char *cls)
{
  int i;
  for (i = 0; i < SPEED_NROWS; ++i)
    if (strcmp(SPEED_ROWS[i].cls, cls) == 0) return i;
  return -1;
}

/* Map the detected CPU to its recommended speed-class row (plan 3.4 / 3.7).
 * The main-menu Auto-detect and this screen share it so they never disagree. */
static int speed_row_for_cpu(const sysprofile_t *p)
{
  if (p->cpu_class == CPU_586)
    return (p->cpu_mhz_est >= 90) ? 3 /* veryfast */ : 2 /* fast */;
  if (p->cpu_class == CPU_486_MID) return 1; /* normal */
  return 0;                                   /* CPU_486_SLOW -> slow */
}

/* Apply a speed-class macro to the session (writes the 4 perf/display keys +
 * SPEED_CLASS). FIXED_TIMESTEP / DIRTY_RECTS / PIXEL_FORMAT_8 stay on for every
 * class; only PERF_MODE + the recorded class vary. */
static void speed_apply(int row)
{
  char b[8];
  if (row < 0 || row >= SPEED_NROWS) return;
  snprintf(b, sizeof(b), "%d", SPEED_ROWS[row].perf);
  scfg_set(&g_cfg, scfg_index("PERF_MODE"),      b);
  scfg_set(&g_cfg, scfg_index("FIXED_TIMESTEP"), "1");
  scfg_set(&g_cfg, scfg_index("DIRTY_RECTS"),    "1");
  scfg_set(&g_cfg, scfg_index("PIXEL_FORMAT_8"), "1");
  scfg_set(&g_cfg, scfg_index("SPEED_CLASS"),    SPEED_ROWS[row].cls);
  g_dirty = 1;
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

  /* R-D: the panel's Sound line shows the configured card + music backend
   * (replaces the old detail Sound line + the Synth presence line). Card from
   * the configured BLASTER T-field (else the detected card); backend from
   * AUDIO_BACKEND. The full Port/IRQ/DMA detail lives in the Sound submenu
   * banner + Custom setup. */
  snprintf(v, sizeof(v), "%s, %s", sb_type_name(cfg_card_type()), cfg_backend_name());
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Sound ....", v);

  snprintf(v, sizeof(v), "%s  VBE %x.%x %s", g_prof.video_desc,
           (g_prof.vbe_version >> 8) & 0xff, g_prof.vbe_version & 0xff,
           g_prof.vbe_lfb ? "(LFB)" : "");
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Video ....", v);

  /* T47 plan 3.2: the Speed line mirrors DF's "Machine Speed: Fast (Fast
   * Recommended)" -- the class the settings were last set FROM (SPEED_CLASS),
   * plus the auto-detect recommendation in parens. "(not set)" until a class
   * is chosen or Auto-detect runs. */
  {
    const char *cls = scfg_get(&g_cfg, scfg_index("SPEED_CLASS"));
    int ci  = speed_row_index(cls);
    int rec = speed_row_for_cpu(&g_prof);
    snprintf(v, sizeof(v), "%s  (%s recommended)",
             ci >= 0 ? SPEED_ROWS[ci].label : "(not set)",
             SPEED_ROWS[rec].label);
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Speed ....", v);
  }
}

/* System Speed screen (T47 plan 3.4): a list of the speed classes + an
 * Auto-detect row, each with a plain-language description. Selecting a class
 * applies its macro to the session; ESC leaves the settings unchanged (T44
 * session model -- main-menu save commits).
 *
 * Round-2 review fix: renders with the SAME layout as every other list screen
 * -- a vertically-centered option box (menu_box_top) above a standard
 * bottom-anchored, full-width DESCRIPTION box (TUI_DESC geometry) -- instead of
 * the old self-contained pick-list whose narrow help bar sat smushed directly
 * under the list and diverged from the other screens. */
static void screen_speed(void)
{
  const char *cls = scfg_get(&g_cfg, scfg_index("SPEED_CLASS"));
  int cur = speed_row_index(cls);
  int rec = speed_row_for_cpu(&g_prof);
  int nrows = SPEED_NROWS + 1;            /* the classes + an Auto-detect row */
  int sel = (cur >= 0) ? cur : rec;
  const int descy = TUI_DESC_TOP(4);     /* standard DESCRIPTION box geometry */

  tui_clear(); /* T45: clear ONCE on entry; the loop repaints in place */
  for (;;)
  {
    int i, k, boxy = menu_box_top(3, descy, nrows + 2);
    const char *d;
    tui_titlebar("SYSTEM SPEED");
    tui_box(19, boxy, 44, nrows + 2, "SYSTEM SPEED"); /* x=19 centers a 44-wide box */
    for (i = 0; i < nrows; ++i)
    {
      const char *label = (i < SPEED_NROWS) ? SPEED_ROWS[i].label : "Auto-detect";
      const char *tag = NULL;
      int selrow = (i == sel);
      int fg  = selrow ? PAL->sel_fg : PAL->body;
      int bg  = selrow ? PAL->sel_bg : PAL->bg;
      int vfg = selrow ? PAL->sel_fg : PAL->value;
      char row[46];
      if (i < SPEED_NROWS)
      {
        if      (i == cur) tag = "(current)";
        else if (i == rec) tag = "(recommended)";
      }
      snprintf(row, sizeof(row), " %-41.41s", label);
      tui_at(20, boxy + 1 + i, fg, bg, row);
      if (tag)
      {
        int tl = (int)strlen(tag);
        int tx = 19 + 44 - 1 - tl;        /* right-aligned in the box interior */
        tui_at(tx, boxy + 1 + i, vfg, bg, tag);
      }
    }
    /* standard bottom-anchored, full-width DESCRIPTION box (same as every
     * other screen): the selected row's plain-language help. */
    tui_box(TUI_DESC_X, descy, TUI_DESC_W, 4, "DESCRIPTION");
    d = (sel < SPEED_NROWS) ? SPEED_ROWS[sel].desc
                            : "Set the speed class that matches the detected CPU.";
    tui_wrap(TUI_DESC_TX, descy + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg, d);
    tui_status("Enter Select   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP)        sel = (sel + nrows - 1) % nrows;
    else if (k == TUI_KEY_DOWN) sel = (sel + 1) % nrows;
    else if (k == TUI_KEY_HOME) sel = 0;
    else if (k == TUI_KEY_ENTER)
    {
      speed_apply(sel == SPEED_NROWS ? rec : sel); /* apply class (or auto) */
      return;
    }
    else if (k == TUI_KEY_ESC) return;             /* T44: no change on ESC */
  }
}

static void screen_autodetect(void)
{
  char cpu[48], snd[64], synth[48], perf[24];
  const char *bval, *pval, *pname;
  int bw = 60, bh = 14, bx, by, x, y, scls;

  recommend_apply(&g_cfg, &g_prof, NULL, 0); /* apply; we render our own lines */
  /* T47 plan 3.7: Auto-detect also applies the System Speed class macro, using
   * the SAME cpu->class mapping the System Speed screen uses (so the two never
   * disagree). */
  scls = speed_row_for_cpu(&g_prof);
  speed_apply(scls);
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
  tui_at(x, y, PAL->body, PAL->bg, "Recommended for this system:");
  y += 2;
  /* each finding on its own line: label (key) and value in distinct roles. */
  tui_kv(x, y++, 16, PAL->title, PAL->body,  PAL->bg, "CPU", cpu);
  tui_kv(x, y++, 16, PAL->title, PAL->body,  PAL->bg, "Sound card", snd);
  tui_kv(x, y++, 16, PAL->title, PAL->body,  PAL->bg, "Synth", synth);
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg,
         "Music backend", snd_backend_name(bval));
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg, "Performance", perf);
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg, "Fixed timestep", "On (50 Hz)");
  tui_kv(x, y++, 16, PAL->title, PAL->value, PAL->bg, "System speed",
         SPEED_ROWS[scls].label);
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

/* NOTE (T47 plan 3.3): the standalone TEST SFX / MUSIC chooser screen was
 * retired -- the two tests now live inline in the Sound submenu
 * (screen_sound_menu + sound_inline_test), each with a result badge. */

static void screen_save(void)
{
  cfg_write_toast();
}

/* ---- Sound submenu (T47 plan 3.3) ----------------------------------- *
 * Folds the three former top-level sound items (Sound setup / Sound hardware /
 * Test SFX-Music) into one DF-style submenu, with the audio tests inline (a
 * result badge beside each). Express setup's full one-key detect flow is a
 * Phase-2 deliverable; in Phase 1 the row signposts it + points at Custom /
 * Auto-detect. Custom = the Sound Hardware screen; Music = the music-playback
 * screen (backend / pre-render / quality); volume + on/off live in Custom. */

/* Run one inline audio test (plan 3.3 co-location): apply the session's Sound
 * Hardware choice, bring the device up, run the modal test, tear it down.
 * Returns 1 heard / 0 silent / -1 device error / -2 audio test not available
 * (the default AUDIOTEST=0 scaffold build). Per-test open/close so a Custom
 * setup edit is reflected on the next test. */
static int sound_inline_test(int phase)
{
  int rc;
  const char *bl = scfg_get(&g_cfg, scfg_index("BLASTER"));
  if (!audiotest_available())
  {
    const char *lines[2];
    lines[0] = audiotest_error();
    lines[1] = "Save the settings and start the game to hear them.";
    tui_message("TEST", lines, 2);
    return -2;
  }
  if (bl && bl[0]) setenv("BLASTER", bl, 1);
  if (audiotest_init(&g_cfg) != 0)
  {
    const char *lines[1]; lines[0] = audiotest_error();
    tui_message("TEST", lines, 1);
    return -1;
  }
  rc = audiotest_popup(phase);
  audiotest_shutdown();
  return rc;
}

/* R-C/R-G: a current-sound-settings banner at the top of the Sound submenu,
 * formatted to MATCH the main-menu SYSTEM PROFILE panel exactly: titled
 * "SOUND", one setting per row in a SINGLE aligned column with dotted leaders
 * (Card / Port / IRQ / DMA / Backend). Sourced from the configured BLASTER +
 * AUDIO_BACKEND. Box height 7 = 5 rows + borders, like the profile panel. */
static void draw_sound_banner(int x, int y, int w)
{
  char v[40];
  int  kx = x + 2, vw = 11, row = y + 1;
  tui_box(x, y, w, 7, "SOUND");
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Card .....",
         sb_type_name(cfg_card_type()));
  snprintf(v, sizeof(v), "0x%X", cfg_io_port());
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Port .....", v);
  snprintf(v, sizeof(v), "%d", cfg_irq());
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "IRQ ......", v);
  snprintf(v, sizeof(v), "%d", cfg_dma());
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "DMA ......", v);
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Backend ..",
         cfg_backend_name());
}

static void screen_sound_menu(void)
{
  static const char *items[] = {
    "Express setup", "Custom setup", "Music",
    "Test sound effects", "Test music", "Back"
  };
  static const char *helps[] = {
    "Detect the sound card and set everything in one step.",
    "Set the card, port, IRQ, DMA, volume, and sound on/off.",
    "Choose the music backend, Organya pre-render, and audio quality.",
    "Play a test sound effect to check the audio.",
    "Play a test song to check the music.",
    "Return to the main menu."
  };
  const int n = (int)(sizeof(items) / sizeof(items[0]));
  const int my = 11; /* menu box top row (the SOUND banner occupies rows 3-9) */
  int sel = 0, res_sfx = -2, res_music = -2;

  tui_clear();
  for (;;)
  {
    int i, k;
    tui_titlebar("SOUND");
    draw_sound_banner(TUI_DESC_X, 3, TUI_DESC_W);
    tui_box(19, my, 44, n + 2, "SOUND"); /* R-O: x=19 centers a 44-wide box (18/18) */
    for (i = 0; i < n; ++i)
    {
      int selrow = (i == sel);
      int fg = selrow ? PAL->sel_fg : PAL->body;
      int bg = selrow ? PAL->sel_bg : PAL->bg;
      char row[46];
      snprintf(row, sizeof(row), " %-41.41s", items[i]);
      tui_at(20, my + 1 + i, fg, bg, row);
      if (i == 3 || i == 4) /* the two test rows carry a result badge */
      {
        int vfg = selrow ? PAL->sel_fg : PAL->value;
        tui_at(20 + 28, my + 1 + i, vfg, bg,
               audiotest_badge(i == 3 ? res_sfx : res_music));
      }
    }
    tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
    tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg, helps[sel]);
    tui_status("Enter Select   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP)        sel = (sel + n - 1) % n;
    else if (k == TUI_KEY_DOWN) sel = (sel + 1) % n;
    else if (k == TUI_KEY_HOME) sel = 0;
    else if (k == TUI_KEY_ESC)  return;
    else if (k == TUI_KEY_ENTER)
    {
      if (sel == 0)
      {
        const char *lines[3];
        lines[0] = "Express one-key detect arrives in a later update.";
        lines[1] = "For now use Custom setup below, or Auto-detect best";
        lines[2] = "settings from the main menu.";
        tui_message("Express setup", lines, 3);
      }
      else if (sel == 1) screen_hardware();
      else if (sel == 2) screen_sound();
      else if (sel == 3) res_sfx   = sound_inline_test(0);
      else if (sel == 4) res_music = sound_inline_test(1);
      else return; /* Back */
      tui_clear(); /* a sub-screen / popup overwrote the screen; repaint clean */
    }
  }
}

/* ---- Input submenu (T47 plan 3.5) ----------------------------------- *
 * Phase-1 shell: the live USE_JOYSTICK on/off row, plus the Configure /
 * Restore rows shown greyed as signposts -- control remapping (the keyboard /
 * joystick configure screens + calibration) is a Phase-3 deliverable that
 * needs an engine-side binding surface. Greyed rows are skipped by navigation
 * (the same mechanism the Sound + category editors use). */
static void screen_input(void)
{
  enum { R_JOY = 0, R_KBD, R_JOYCFG, R_RESTORE, R_BACK, R_N };
  static const char *labels[R_N] = {
    "Joystick / gamepad", "Configure keyboard", "Configure joystick",
    "Restore default controls", "Back"
  };
  static const char *helps[R_N] = {
    "Read a joystick or gamepad on the PC game port. Off = keyboard only.",
    "Remap keyboard controls. Arrives in a later update.",
    "Assign joystick buttons. Arrives in a later update.",
    "Reset all controls to defaults. Arrives in a later update.",
    "Return to the main menu."
  };
  int joyidx = scfg_index("USE_JOYSTICK");
  int sel = R_JOY;

  tui_clear();
  for (;;)
  {
    int i, k, active[R_N];
    /* D: center the option box between the title bar and the DESCRIPTION box. */
    int boxy = menu_box_top(3, TUI_DESC_TOP(4), R_N + 2);
    active[R_JOY] = 1; active[R_KBD] = 0; active[R_JOYCFG] = 0;
    active[R_RESTORE] = 0; active[R_BACK] = 1; /* Configure/Restore = Phase 3 */
    tui_titlebar("INPUT");
    tui_box(19, boxy, 44, R_N + 2, "INPUT"); /* R-O: centered (18/18 margins) */
    for (i = 0; i < R_N; ++i)
    {
      int selrow = (active[i] && i == sel);
      int lblfg  = !active[i] ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
      int bg     = selrow ? PAL->sel_bg : PAL->bg;
      char row[46];
      snprintf(row, sizeof(row), " %-41.41s", labels[i]);
      tui_at(20, boxy + 1 + i, lblfg, bg, row);
      if (i == R_JOY)
      {
        int vfg = selrow ? PAL->sel_fg : PAL->value;
        tui_at(20 + 28, boxy + 1 + i, vfg, bg,
               strcmp(scfg_get(&g_cfg, joyidx), "1") == 0 ? "On" : "Off");
      }
    }
    tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
    tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg, helps[sel]);
    tui_status("Space Change   Enter Select   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN)
    {
      int dir = (k == TUI_KEY_UP) ? -1 : +1, j;
      for (j = 0; j < R_N; ++j)
      { sel = (sel + dir + R_N) % R_N; if (active[sel]) break; }
    }
    else if (k == TUI_KEY_HOME) sel = R_JOY;
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      if (sel == R_JOY) cycle_value(joyidx, +1); /* live toggle (session) */
    }
    else if (k == TUI_KEY_ENTER)
    {
      /* R-I: Enter on the Joystick row opens its On/Off pick-list (R-H clear
       * after). Back exits the submenu. */
      if (sel == R_JOY) { if (pick_value(joyidx)) tui_clear(); }
      else if (sel == R_BACK) return;
    }
    else if (k == TUI_KEY_ESC) return; /* T44: live edits kept in the session */
  }
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
#define MENU_X 22 /* R-O: centers the 38-wide MAIN MENU box (21/21 margins) */
#define MENU_W 38
#define MENU_Y 10
  {
    /* T47 plan 3.1: tightened from 9 items to 7. Sound x3 -> one Sound submenu
     * (3.3); +System speed (3.4); Performance folded into Advanced (3.6);
     * Input/joystick renamed Input (3.5). */
    static const char *items[] = {
      "Sound",
      "System speed",
      "Input",
      "Advanced",
      "Auto-detect best settings",
      "Save and exit",
      "Quit without saving"
    };
    /* item 7: one short sentence per item (the operator's review-3 example
     * "Choose how music and sound effects play."). */
    static const char *helps[] = {
      "Setup sound card, music, sound effects and volume",
      "Preset performance options depending on system speed",
      "Keyboard, Joystick and gamepad configuration",
      "Performance and compatibility options",
      "Auto-detect hardware and best settings for this system",
      "Save settings and quit to DOS",
      "Discard changes without saving and quit to DOS"
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
         * width/position (x=5, w=72) so the two stacked boxes line up. T47: this
         * is now the shared TUI_DESC geometry every screen's bottom box uses. */
        tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
        tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg, helps[sel]);
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

      if (action == 0)      screen_sound_menu();
      else if (action == 1) screen_speed();
      else if (action == 2) screen_input();
      else if (action == 3) screen_advanced();
      else if (action == 4) screen_autodetect();
      else if (action == 5) { screen_save(); break; }   /* Save and exit       */
      else if (action == -2) { screen_save(); break; }  /* F10 = Save and Exit */
      else /* action == 6 (Quit without saving) or ESC (-1) */
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
