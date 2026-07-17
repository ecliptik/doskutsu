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
#include "musiccard.h"
#include "bindings.h"
#include "scancode.h"
#include "joyport.h"

#define CFG_PATH "DOSKUTSU.CFG"

static scfg_t      g_cfg;
static sysprofile_t g_prof;
static int          g_dirty; /* unsaved changes */

/* MIDI music sets discovered on disk (backlog #39). Rescanned on entry to the
 * Music screen; the conditional "MIDI music set" row + its picker read these. */
static midiset_t g_midi_sets[MIDISET_MAX];
static int       g_midi_nsets;

/* Control-binding session (Phase 3). Loaded once from DOSKUTSU.CFG (BIND_*),
 * edited live by the Configure Keyboard / Joystick screens, written back to the
 * session config on every change. Held module-static so edits persist across
 * screen visits even before the engine registry defines the BIND_* keys (then
 * bindings_save is a no-op and this is purely the in-memory session). */
static binding_t g_binds[BIND_COUNT];
static int       g_binds_init;

static void binds_ensure(void)
{
  if (!g_binds_init) { bindings_load(g_binds, &g_cfg); g_binds_init = 1; }
}

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
#define MENU_STATUS "Enter Select   F10 Save and Exit   ESC Exit (prompts to save)"

/* Write the in-memory config to disk and show a modal result. Used by the
 * F10 "save settings and return to the menu" action (T17) and the main-menu
 * "Save and exit" item. Returns 1 on success, 0 on failure.
 *
 * Success shows the classic modal -- "Settings saved. / Run DOSKUTSU.EXE to
 * play." -- which waits for a keypress. The user is going back to the DOS
 * prompt and DOES have to start the game themselves. FAILURE is a blocking
 * modal too: a failed save is the one moment the user must not walk past. */
static int cfg_write_toast(void)
{
  tui_clear();
  tui_titlebar("DOSKUTSU SETUP"); /* round-7 item 2: title bar on the toast too */
  if (scfg_save(&g_cfg, CFG_PATH) == 0)
  {
    const char *lines[2];
    g_dirty = 0;
    lines[0] = "Settings saved.";
    lines[1] = "Run DOSKUTSU.EXE to play.";
    tui_message("Saved", lines, 2);
    return 1;
  }
  else
  {
    const char *lines[1];
    lines[0] = "ERROR: could not write " CFG_PATH " (disk full / read-only?)";
    tui_message("Save failed", lines, 1);
    return 0;
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

/* (#9: the per-backend help table BACKEND_HELP was REMOVED -- the music backend
 * is no longer a generic-editor row; it is the Select Music Type picker, whose
 * one-line descriptions live in the MUSIC_TYPES / MUSIC_CARDS tables and whose
 * labels come from snd_backend_name. Keeping a second backend-description table
 * here was the same drift risk we just single-sourced for the labels.) */

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
  /* AUDIO_BACKEND is not a generic-editor row (it is the Select Music Type
   * picker), so it has no per-line help table here -- removed with BACKEND_HELP. */
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

/* #9: forward decl -- snd_backend_name (defined below) is the SINGLE source of
 * music labels, shared by both pickers, the hub banner, Auto-detect, and the
 * generic backend pick-list here, so the wording can never drift. */
static const char *snd_backend_name(const char *v);

/* R-I: open a modal pick-list for registry key `idx` and apply the chosen value
 * to the session (live). Friendly labels per key (backend names, Enabled/
 * Disabled, sample rates, perf-mode names). Returns 1 if a list was shown
 * (the caller then full-clears -- R-H), 0 if the key has no sensible discrete
 * list (a wide INT like the 0-31 volume levels -- caller keeps Left/Right). */
static int pick_value(int idx)
{
  const dkt_key_t *k = &DKT_KEYS[idx];
  const char *items[8], *raw[8];
  /* 32 wide: the longest music label is the composed "MIDI: Gravis UltraSound". */
  char buf[8][32], rbuf[8][8];
  int n = 0, i, choice, start = 0;
  const char *cur = scfg_get(&g_cfg, idx);

  if (k->type == DKT_ENUM && k->enum_vals)
  {
    int is_backend = dkt_key_ieq(k->cfg_key, "AUDIO_BACKEND", 13);
    for (i = 0; k->enum_vals[i] && n < 8; ++i)
    {
      const char *e = k->enum_vals[i];
      raw[n] = e;
      /* #9 single label source. snd_backend_name may return a STATIC composed
       * buffer ("MIDI: <synth>"), and this list keeps every label alive at
       * once -- so copy each one out instead of storing the pointer. */
      snprintf(buf[n], sizeof(buf[n]), "%s",
               is_backend ? snd_backend_name(e) : e);
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
 * NOTE (MIDI instrument set): the MIDI_SET row lets the user pick between the
 * installed arrangement sets. The DEFAULT is now OrgMIDI v2 (data/orgmid2/, our
 * org2mid native-GM conversion; nx0262); WiiWare (data/midi/) is the
 * alternative. Both are populated per docs/ASSETS.md Step 4.5 (WiiWare fetched,
 * orgmid2 generated by `make convert-music`). The row is shown only when >=2
 * sets are present on disk (midiset_scan); with one/none it is omitted so a
 * first-timer is never pointed at a directory their disk lacks.
 */

/* #9: the music BACKEND is now chosen in the Sound hub's "Select Music Type"
 * Music Type row + Select Music Card picker, so this screen -- retitled
 * "Music Options" --
 * holds only the per-card music EXTRAS: GUS voices + GUS high-fidelity (gus),
 * MIDI set (MIDI cards), Organya pre-render (organya), audio quality. Sound
 * enable/disable + the SFX/Music volume levels live in Sound card hardware. */
enum
{
  SND_GUSVOICES = 0, /* GUS_VOICES, shown only for the gus backend (#39) */
  SND_GUSHIFI,       /* GUS_HIFI high-fidelity toggle, gus only (#9/#12) */
  SND_MIDISET,       /* MIDI_SET, shown only for a MIDI backend + >=2 sets (#39) */
  SND_PRERENDER,     /* ORG_PRERENDER, active only for Organya        */
  SND_QUALITY,       /* AUDIO_TIER2                                    */
  SND_NROWS
};

/* GUS active-voice presets (#39): the GF1 DAC output rate is 617400/voices, so
 * the voice count IS the music sample rate. The approved value-list (FastDoom
 * "Select Frequency" style): full label for the picker, a compact "<n> (<rate>)"
 * for the Music-screen row value, and a one-line DESCRIPTION note. The values
 * match dkt_gus_voice_vals in the shared registry. */
static const struct
{
  const char *value;   /* GUS_VOICES cfg value (== voice count)     */
  const char *label;   /* picker row label (approved presentation)  */
  const char *compact; /* Music-screen row value (fits the column)  */
  const char *desc;    /* picker DESCRIPTION note                   */
} GUS_VOICE_PRESETS[] = {
  { "14", "14 voices  - 44100 Hz",                   "14 (44100 Hz)",
    "44100 Hz. Highest fidelity, 14 notes." },
  { "16", "16 voices  - 38587 Hz",                   "16 (38587 Hz)",
    "38587 Hz. 16-note polyphony." },
  { "20", "20 voices  - 30870 Hz",                   "20 (30870 Hz)",
    "30870 Hz. Default; best balance of fidelity and polyphony." },
  { "24", "24 voices  - 25725 Hz",                   "24 (25725 Hz)",
    "25725 Hz. 24-note polyphony." },
  { "28", "28 voices  - 22050 Hz",                   "28 (22050 Hz)",
    "22050 Hz. May be silent on PicoGUS." },
  { "32", "32 voices  - 19293 Hz",                   "32 (19293 Hz)",
    "19293 Hz. Lowest fidelity, 32 notes." }
};
#define GUS_VOICE_NPRESETS \
  ((int)(sizeof(GUS_VOICE_PRESETS) / sizeof(GUS_VOICE_PRESETS[0])))

/* Index of a GUS_VOICES value among the presets, or 0 (the first preset, 14
 * voices) if the stored value is not one of the curated presets. The registry
 * default is 20 (a valid preset), so this fallback only trips on a hand-edited
 * out-of-list value. */
static int gus_preset_index(const char *v)
{
  int i;
  for (i = 0; i < GUS_VOICE_NPRESETS; ++i)
    if (strcmp(GUS_VOICE_PRESETS[i].value, v) == 0) return i;
  return 0;
}

static int snd_key_idx(int row)
{
  switch (row)
  {
    case SND_GUSVOICES: return scfg_index("GUS_VOICES");
    case SND_GUSHIFI:   return scfg_index("GUS_HIFI");
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
    case SND_GUSVOICES: return "GUS voices";
    case SND_GUSHIFI:   return "GUS high fidelity";
    case SND_MIDISET:   return "MIDI music set";
    case SND_PRERENDER: return "Organya pre-render";
    case SND_QUALITY:   return "Audio quality";
    default:            return "";
  }
}

/* ====================================================================== *
 * #9 TYPE-FIRST music selection (operator-approved 2026-07-13). The old flat
 * "Select Music Card" list mixed two different questions in one column: WHAT
 * KIND of music (Organya's built-in software synth vs MIDI on a synth chip)
 * and WHICH SYNTH plays the MIDI. It read "Sound Blaster" for the OPL3 FM
 * entry -- so an operator on a Sound Blaster picked it believing it named
 * their HARDWARE, and Organya (which needs no sound card at all) sat buried as
 * a peer entry with its pre-render row greyed out.
 *
 * The hub now asks the TYPE first (Organya / MIDI / Auto-detect / No Music);
 * picking MIDI opens the synth list (OPL3 FM / WaveBlaster / General MIDI /
 * AdLib / Gravis). NO new cfg key: the type is DERIVED from AUDIO_BACKEND (+
 * the SETUP-only MIDI_DEV discriminator for the two wb synths), and writing
 * runs the same mapping in reverse.
 *
 * SINGLE-SOURCE LABEL DISCIPLINE (preserved from the flat picker): every
 * literal appears exactly ONCE, in the two tables below. The type-level
 * resolver (music_card_label) and the card-level resolver (music_card_short)
 * read them; the hub banner, both pickers, Auto-detect, the Express modal and
 * the SYSTEM PROFILE panel all go through those two, so the wording cannot
 * drift between screens.
 * ====================================================================== */

/* v2: the music TYPE + CARD model (tables, derivation, resolvers) now lives in
 * setup/musiccard.{c,h} -- extracted so it can be unit-tested on the host, since
 * main.c is a TUI binary and is NOT linked into the test. The round-trip test
 * (setup/tests/test_config.c) drives AUDIO_BACKEND -> Type+Card -> back, which is
 * what holds the derivation and the write path in agreement. */

/* Wrapper used where only the backend value is in hand (Auto-detect / Express /
 * the generic enum picker): resolves the wb split from the CURRENT MIDI_DEV. */
static const char *snd_backend_name(const char *v)
{
  const char *md = (strcmp(v, "wb") == 0)
                   ? scfg_get(&g_cfg, scfg_index("MIDI_DEV")) : NULL;
  return music_card_label(v, md);
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
  if (row == SND_GUSVOICES)
    snprintf(out, (size_t)cap, "%s", GUS_VOICE_PRESETS[gus_preset_index(v)].compact);
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
    case SND_GUSVOICES:
      return "Gravis UltraSound active voices. The GF1 output rate is "
             "617400/voices, so fewer voices = a higher sample rate. 28 "
             "voices = 22050 Hz can be SILENT on some PicoGUS cards (a "
             "firmware rate quirk; use any other value). More voices = more "
             "notes at a lower rate.";
    case SND_GUSHIFI:
      /* Each option on its own line (tui_wrap honors '\n'), fits the box. */
      return "On  - multisample, richer instruments\n"
             "Off - single sample, less GF1 memory";
    case SND_MIDISET:
      return "Which set of MIDI music the WaveBlaster / OPL3 backend plays. "
             "WiiWare = polished re-arrangements (Yann van der Cruyssen). "
             "OrgMIDI = note-for-note transcription of the original Organya "
             "music. Only sets you have installed under data appear here.";
    case SND_PRERENDER:
      /* Post-0275: an EAGER all-songs batch precache on FIRST LAUNCH (not the
       * old lazy per-song-on-first-play). ~20 min / ~47 MB measured on the
       * POD-83 reference machine (iter-5); a 486 takes longer. Fits the 4x68
       * DESCRIPTION box (tui_wrap TUI_DESC_TW=68, 4 lines). */
      return "Organya only. First launch pre-renders all songs to disk: a "
             "one-time ~20 min (longer on 486), ~47 MB. Later launches skip it. "
             "The MIDI backends (OPL3, WaveBlaster, AdLib, GUS) need no "
             "pre-render. ESC skips the render.";
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
  const char *be = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  int organya = strcmp(be, "organya") == 0;
  int gus     = strcmp(be, "gus") == 0;
  int adlib   = strcmp(be, "adlib") == 0;
  /* v2 Q2 (a LIVE BUG fixed): the MIDI music-set applies to EVERY MIDI card,
   * which includes the Gravis -- the GF1 plays the .mid sets like any other MIDI
   * backend, and the whole orgmid2-vs-wiimidi A/B was decided ON the GUS drums.
   * The old gate (wb|opl3|auto) EXCLUDED gus, hiding that choice from the exact
   * user who cares most about it. Gate on the derived Type instead. */
  int midi    = music_type_of(be) == MTYPE_MIDI;
  if (row == SND_GUSVOICES) return gus;
  if (row == SND_GUSHIFI)   return gus;
  if (row == SND_MIDISET)   return midi;
  if (row == SND_PRERENDER) return organya;
  /* Audio quality (the SDL mix-device rate, AUDIO_TIER2) is the SB/OPL3/WB
   * rate-quality knob too -- it governs the SFX mix rate on those backends, not
   * just Organya's whole-synth rate (team-lead Option A: one editor per key, so
   * SB's quality choice lives here rather than in a duplicate SB screen). It is
   * greyed only for adlib (OPL2, music-only, no PCM mix) and gus (the GF1 has
   * its own DAC rate, set by the GUS voices row above).
   *
   * THIS RULE SURVIVED THE v2 REDESIGN DELIBERATELY (team-lead ruling). The v2
   * brief summarized Music Options as "pre-render + quality for Organya", which
   * would have made this row Organya-ONLY -- and that would have left an
   * SB/OPL3/WB user with NO UI at all for their SFX mix rate: the same
   * greyed-and-undiscoverable failure v2 exists to fix, just pointed the other
   * way. Do not "simplify" it to organya-only. */
  if (row == SND_QUALITY)   return !(adlib || gus);
  return 1;
}

/* A row is VISIBLE (rendered at all) when it is meaningful on this machine.
 * The MIDI music-set row is omitted entirely unless at least two MIDI sets are
 * installed on disk -- a first-timer with one set never sees a dead choice
 * (#39 sec.4). Every other row is always present (greying handles the rest). */
static int snd_visible(int row)
{
  /* The GUS-voices row is shown only when the Gravis backend is selected --
   * a first-timer on any other card never sees a dead choice (mirrors the
   * MIDI-set row, which needs >=2 installed sets). */
  if (row == SND_GUSVOICES || row == SND_GUSHIFI)
    return strcmp(scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND")), "gus") == 0;
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

/* Cycle GUS_VOICES among the curated presets (Space / Left / Right on the row). */
static void snd_gusvoices_cycle(int dir)
{
  int idx = scfg_index("GUS_VOICES");
  int cur = gus_preset_index(scfg_get(&g_cfg, idx));
  cur = (cur + dir + GUS_VOICE_NPRESETS) % GUS_VOICE_NPRESETS;
  if (strcmp(scfg_get(&g_cfg, idx), GUS_VOICE_PRESETS[cur].value) != 0)
  {
    scfg_set(&g_cfg, idx, GUS_VOICE_PRESETS[cur].value);
    g_dirty = 1;
  }
}

/* Modal "Select GUS Voices" value-list (Enter on the GUS-voices row). FastDoom
 * "Select Frequency" style: every preset shown at once with its voice count +
 * resulting DAC rate, the current one tagged "(current)", and a DESCRIPTION
 * note (incl. the 28-voice/22050 Hz PicoGUS firmware-rescale silence warning). Writes
 * GUS_VOICES on a change. Returns 1 (a list was shown). */
static int snd_pick_gusvoices(void)
{
  const char *items[GUS_VOICE_NPRESETS];
  const char *tags[GUS_VOICE_NPRESETS];
  const char *descs[GUS_VOICE_NPRESETS];
  int idx = scfg_index("GUS_VOICES");
  int i, choice, start = gus_preset_index(scfg_get(&g_cfg, idx));

  for (i = 0; i < GUS_VOICE_NPRESETS; ++i)
  {
    items[i] = GUS_VOICE_PRESETS[i].label;
    tags[i]  = (i == start) ? "(current)" : "";
    descs[i] = GUS_VOICE_PRESETS[i].desc;
  }

  choice = tui_picklist("Select GUS Voices", 0, 0, items, tags, descs,
                        GUS_VOICE_NPRESETS, start, 0, NULL, NULL, 0);
  if (choice >= 0 && choice < GUS_VOICE_NPRESETS &&
      strcmp(scfg_get(&g_cfg, idx), GUS_VOICE_PRESETS[choice].value) != 0)
  {
    scfg_set(&g_cfg, idx, GUS_VOICE_PRESETS[choice].value);
    g_dirty = 1;
  }
  return 1;
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
  if (n >= 1 && cwd[n - 1] == '\\' && !(n == 3 && cwd[1] == ':'))
    cwd[--n] = '\0';
  for (i = 0; dir[i] && i < (int)sizeof(up) - 1; ++i)
    up[i] = (dir[i] >= 'a' && dir[i] <= 'z') ? (char)(dir[i] - 'a' + 'A') : dir[i];
  up[i] = '\0';

  if (n == 0)            snprintf(out, (size_t)cap, "DATA\\%s", up);
  else if (cwd[n-1]=='\\') snprintf(out, (size_t)cap, "%sDATA\\%s", cwd, up);
  else                   snprintf(out, (size_t)cap, "%s\\DATA\\%s", cwd, up);
}

/* #39 A: build the per-set DESCRIPTION list shown when the MIDI-music-set row
 * is highlighted -- friendly name in the keyword column, then the DOS path it
 * loads from + its track count. Built-in sets + ONE custom drop-in; further
 * customs collapse to a "+N more" row (the picker still lists every set). The
 * help renderer is capped at one line per row (see the help_list call), so the
 * path + count never wraps past the box. Returns rows filled; *keyw = column. */
static int snd_midiset_help(helprow_t *rows, char descbuf[][192], int cap,
                            int *keyw)
{
  int i, r = 0, kw = 9, custom_shown = 0, extra = 0;

  for (i = 0; i < g_midi_nsets; ++i)
  {
    int w = (int)strlen(g_midi_sets[i].label) + 2;
    if (w > kw) kw = w;
  }
  for (i = 0; i < g_midi_nsets && r < cap; ++i)
  {
    char path[64];
    if (strncmp(g_midi_sets[i].label, "Custom", 6) == 0)
    {
      if (custom_shown) { extra++; continue; }
      custom_shown = 1;
    }
    midiset_dos_path(g_midi_sets[i].dir, path, sizeof(path));
    snprintf(descbuf[r], 192, "%s (%d track%s)", path,
             g_midi_sets[i].mid_count, g_midi_sets[i].mid_count == 1 ? "" : "s");
    rows[r].key  = g_midi_sets[i].label;
    rows[r].desc = descbuf[r];
    r++;
  }
  if (extra > 0 && r < cap)
  {
    snprintf(descbuf[r], 192, "%d more -- open the list to choose", extra);
    rows[r].key  = "+more";
    rows[r].desc = descbuf[r];
    r++;
  }
  *keyw = kw;
  return r;
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
  int i, choice, start = 0;

  if (g_midi_nsets <= 0) return 0;

  i = midiset_index_by_value(g_midi_sets, g_midi_nsets, scfg_get(&g_cfg, idx));
  if (i >= 0) start = i;

  for (i = 0; i < g_midi_nsets; ++i)
  {
    char path[64];
    midiset_dos_path(g_midi_sets[i].dir, path, sizeof(path));
    items[i] = g_midi_sets[i].label;
    tags[i]  = (i == start) ? "(current)" : "";
    snprintf(dbuf[i], sizeof(dbuf[i]), "%s (%d track%s)", path,
             g_midi_sets[i].mid_count, g_midi_sets[i].mid_count == 1 ? "" : "s");
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

/* MIDI-set discovery is the slow part of opening the Music screen on real HW:
 * midiset_scan() opendir()s every set dir and counts every .mid for the
 * "(N tracks)" display. A built-in Cave Story set is ~90 .mid, so it is
 * hundreds of DOS FindNext calls -- ~5-10 s on a CF-backed 486, growing with
 * each custom drop-in dir. The on-disk set list cannot change during a SETUP
 * run, so scan ONCE and cache for the rest of the session; later Music entries
 * are then instant. Lazy (the first Music entry pays the one-time cost) so a
 * SETUP run that never opens Music pays nothing. A brief popup covers the one
 * scan so it reads as work-in-progress rather than a frozen screen. */
static int g_midi_scanned = 0;
static void midiset_scan_cached(void)
{
  int w = 50, h = 7;
  int x = (80 - w) / 2 + 1;
  int y = (25 - h) / 2 + 1;
  if (g_midi_scanned) return;
  tui_clear();
  tui_titlebar("MUSIC");
  tui_box(x, y, w, h, "Scanning Music Sets");
  tui_wrap(x + 2, y + 2, w - 4, 3, PAL->body, PAL->bg,
           "Reading MIDI set directories from disk...");
  tui_popup_decorate(x, y, w, h);
  tui_present(); /* flush the notice before the blocking scan (no getkey here) */
  g_midi_nsets = midiset_scan("data", g_midi_sets, MIDISET_MAX);
  g_midi_scanned = 1;
}

static void screen_sound(void)
{
  /* Land on the first row that is meaningful for the chosen card (the backend
   * now lives in the hub's Music-type picker, so this screen has no fixed
   * always-on first row). */
  int sel = 0;
  scfg_t snap = g_cfg;          /* T52: entry snapshot for the ESC revert path */
  int dirty_snap = g_dirty;

  if (!(snd_visible(sel) && snd_active(sel))) sel = snd_step(sel, +1);

  /* #39: discover the MIDI music sets present on disk (data/midi/, data/orgmid/).
   * Cached once per SETUP session -- the on-disk set list does not change
   * mid-run, and the per-set track count is slow to recompute on a CF 486. */
  midiset_scan_cached();

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

    tui_titlebar("MUSIC OPTIONS");
    tui_box(9, boxy, 64, nvis + 2, "MUSIC OPTIONS"); /* R-O: centered (8/8 margins) */
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
        /* one line per row (path + count is single-line; never wrap past the box) */
        help_list(TUI_DESC_TX, hy + 1, mkw, TUI_DESC_TW - mkw, 1, mrows, mn);
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
    else if (k == TUI_KEY_HOME)
      sel = (snd_visible(0) && snd_active(0)) ? 0 : snd_step(0, +1);
    else if (k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == TUI_KEY_SPACE)
    {
      /* T62: only Space / Left / Right cycle the highlighted row's value. */
      if (snd_active(sel))
      {
        if (sel == SND_MIDISET)
          /* #39: MIDI_SET is a DKT_STR over the discovered logical sets, so it
           * has its own cycle (the generic cycle_value does not handle STR). */
          snd_midiset_cycle((k == TUI_KEY_LEFT) ? -1 : +1);
        else if (sel == SND_GUSVOICES)
          /* #39: cycle among the curated voice presets, not raw enum values. */
          snd_gusvoices_cycle((k == TUI_KEY_LEFT) ? -1 : +1);
        else
          cycle_value(snd_key_idx(sel), (k == TUI_KEY_LEFT) ? -1 : +1);
      }
    }
    else if (k == TUI_KEY_ENTER)
    {
      /* R-I: Enter opens a pick-list for the highlighted row; R-H: full-clear
       * after it closes (no overlay residue). */
      int shown;
      if (!snd_active(sel))
        shown = 0;
      else if (sel == SND_MIDISET)
        shown = snd_pick_midiset(); /* #39: dedicated set picker (STR key) */
      else if (sel == SND_GUSVOICES)
        shown = snd_pick_gusvoices(); /* #39: "Select GUS Voices" value list */
      else
        shown = pick_value(snd_key_idx(sel));
      if (shown)
        tui_clear();
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
/* BLASTER "T" card-type codes per the Creative / pgusinit /sbtype standard:
 *   1 = SB 1.x, 2 = SB Pro (SB Pro 1, dual OPL2), 3 = SB 2.0,
 *   4 = SB Pro 2 (OPL3), 6 = SB16.
 * DISPLAY-ONLY: hw_type_vals[] and every numeric T path are unchanged; this
 * function only maps a code to its shown name. The pre-fix table had 2/3/4
 * mislabelled (T3 read "Pro 2.0", T4 read "Pro", T2 read "2.0"), so picking the
 * OPL3 card ("Sound Blaster Pro 2 (OPL3)") now correctly names T4 instead of T3. */
static const char *sb_type_name(int t)
{
  return (t == 6) ? "Sound Blaster 16"
       : (t == 4) ? "Sound Blaster Pro 2 (OPL3)"
       : (t == 3) ? "Sound Blaster 2.0"
       : (t == 2) ? "Sound Blaster Pro"
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

/* Is the Sound Blaster actually in use in the current config? True when the
 * music backend is SB-family (auto / opl3 / organya / wb -- all drive the SB16:
 * OPL3 FM, the WaveBlaster header, or the SB DAC) OR the SFX device is the SB
 * DAC. The two no-SB native cards are AdLib (OPL at 0x388, no SB) and Gravis
 * (GF1). When SB is NOT in use, the Card/Port/IRQ/DMA detail is meaningless and
 * the UI shows the music device + "n/a" instead of a stale SB card. sfx_sb
 * mirrors sfx_native_name(): SFX rides the SB DAC unless the card is adlib/gus
 * or effects are off (SFX_DEVICE=none). scfg_get never returns NULL (empty when
 * unset), so the strcmp()s are safe -- same pattern as sfx_current_name(). */
static int sb_in_use(void)
{
  const char *be = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  const char *sd = scfg_get(&g_cfg, scfg_index("SFX_DEVICE"));
  int music_sb = !(strcmp(be, "adlib") == 0 || strcmp(be, "gus") == 0 ||
                   strcmp(be, "none") == 0);   /* empty/auto -> SB */
  int sfx_sb   = !(strcmp(be, "adlib") == 0 || strcmp(be, "gus") == 0) &&
                 strcmp(sd, "none") != 0;
  return music_sb || sfx_sb;
}

/* COMPACT music-backend name for the main-menu SYSTEM PROFILE "Sound" line,
 * which pairs it with the SB card type ("Sound Blaster 16, OPL3"). Deliberately
 * shorter than the verbose picker labels (snd_backend_name, which composes
 * "MIDI: <synth>") -- this is a constrained one-line panel, not the picker. */
static const char *cfg_backend_name(void)
{
  const char *b = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  /* wb: delegate to the synth-level label source (WaveBlaster / General MIDI) --
   * its short name already fits this compact line. The rest stay compact. */
  if (strcmp(b, "wb") == 0)
    return music_card_short("wb", scfg_get(&g_cfg, scfg_index("MIDI_DEV")));
  if (strcmp(b, "opl3") == 0)    return "OPL3";
  if (strcmp(b, "organya") == 0) return "Organya";
  if (strcmp(b, "adlib") == 0)   return "AdLib";
  if (strcmp(b, "gus") == 0)     return "Gravis";
  if (strcmp(b, "none") == 0)    return "No Music";
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
  /* Show "<SB card>, <backend>" only when the SB is actually in use; for a
   * no-SB config (AdLib / Gravis music with non-SB SFX) show just the music
   * device, never a stale SB card type. */
  if (sb_in_use())
    snprintf(v, sizeof(v), "%s, %s", sb_type_name(cfg_card_type()), cfg_backend_name());
  else
    snprintf(v, sizeof(v), "%s", cfg_backend_name());
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Sound ....", v);

  /* T-DFUX-P2: the Video line carries the measured video-memory fill speed
   * (KB/s) after the chip + VBE detail. g2k-only evidence; "(speed n/a)" when
   * the bench could not run (no flat access). The path code (which fallback
   * measured it) is in PROFILE.LOG, not crowded onto the panel line. */
  if (g_prof.video_speed_kbs > 0)
    snprintf(v, sizeof(v), "%s  VBE %x.%x %s  %d KB/s", g_prof.video_desc,
             (g_prof.vbe_version >> 8) & 0xff, g_prof.vbe_version & 0xff,
             g_prof.vbe_lfb ? "(LFB)" : "", g_prof.video_speed_kbs);
  else
    snprintf(v, sizeof(v), "%s  VBE %x.%x %s  (speed n/a)", g_prof.video_desc,
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
     * screen). This also resolves the old "1/5" wording + a width risk. The
     * DMA seed source (card self-report vs BLASTER env) is shown so a PicoGUS
     * in SB mode -- which self-reports no DMA -- reads "DMA 1 (BLASTER)" rather
     * than a bare, mysterious channel number. */
    snprintf(snd, sizeof(snd), "Port 0x%X, IRQ %d, DMA %d (%s), Type T%d",
             g_prof.snd_base, g_prof.snd_irq, g_prof.snd_dma,
             snd_dma_src_name(g_prof.snd_dma_src), g_prof.snd_type);
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

/* Returns 1 when the config actually reached disk, 0 on write failure. */
static int screen_save(void)
{
  return cfg_write_toast();
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

/* ====================================================================== *
 * #9 Sound redesign -- dual hardware pickers (FastDoom PATTERN, original
 * doskutsu screens). The Sound hub leads with a Music-TYPE picker and a
 * Sound-FX-device picker. Picking a music type (or, under MIDI, a synth) walks
 * that choice's inline param sub-screen(s) (fdsetup "pick -> params -> back"),
 * then the SFX list re-narrows to only the devices that card can drive. The
 * MUSIC_TYPES / MUSIC_CARDS tables + their resolvers are defined up with
 * music_card_label. See docs/internal/SETUP-SOUND-UX-SPEC.md sec.4-5.
 * ====================================================================== */

/* Friendly device name for the SFX the current music backend routes effects to
 * when SFX are ENABLED. SFX routing is DERIVED from the music path (engine
 * patch 0240): an SB-family card -> the Sound Blaster DAC; the Gravis card ->
 * the GF1 wavetable; AdLib has no DAC (effects forced off). */
static const char *sfx_native_name(const char *be)
{
  /* #38 landed: GF1 SFX now works, so the "gus" branch IS reached -- Gravis
   * offers a native SFX device (the GF1 wavetable) just like Sound Blaster. */
  if (strcmp(be, "gus") == 0)   return "Gravis UltraSound";
  if (strcmp(be, "adlib") == 0) return "No Sound FX"; /* DAC-less: forced off */
  return "Sound Blaster"; /* opl3 / organya / wb / auto / none -> SB DAC */
}

/* The Sound-FX device as the current config resolves it: "No Sound FX" when the
 * card is DAC-less (adlib) or the user turned effects off (SFX_DEVICE=none),
 * otherwise the music card's native SFX device. */
static const char *sfx_current_name(void)
{
  const char *be = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  const char *sd = scfg_get(&g_cfg, scfg_index("SFX_DEVICE"));
  if (strcmp(be, "adlib") == 0)  return "No Sound FX";
  if (strcmp(sd, "none") == 0)   return "No Sound FX";
  return sfx_native_name(be);
}

/* v2: the two inline-test result badges live at file scope now, because the
 * test-after-pick prompt (below) runs INSIDE the pickers and must feed the very
 * same badge the Sound menu's Test rows show. An earlier Express revision
 * auto-played a test and DISCARDED its result, so the Test row still read "Not
 * tested" -- the operator flagged it. Sharing the state is what stops that
 * repeating. -2 = never tested this session. */
static int g_res_sfx   = -2;
static int g_res_music = -2;

/* v2 test-after-pick: right after a card's hardware sub-screen, offer to verify
 * it. `music` selects which test (1 = music, 0 = sound effects). The result is
 * recorded in the shared badge, so "Working" shows on the Sound menu exactly as
 * if the user had run the Test row by hand.
 *
 * Default highlight is Yes (operator ask). NOTE tui_yesno resolves ESC to the
 * DEFAULT, so ESC here means Yes -- i.e. it plays the test rather than skipping
 * it. That is the one place v2 deviates from the spec's "ESC == No"; the two are
 * mutually exclusive with this primitive, and playing a (stoppable) test is
 * harmless, whereas changing tui_yesno would alter every destructive prompt that
 * correctly relies on ESC -> No. */
static void snd_test_after_pick(int music)
{
  int r;
  tui_clear();
  if (!tui_yesno(music ? "Test music" : "Test sound effects",
                 music ? "Test music now?" : "Test sound effects now?", 0))
  {
    tui_clear();
    return;
  }
  r = sound_inline_test(music);
  if (music) g_res_music = r;
  else       g_res_sfx   = r;
  tui_clear();
}

/* Walk the inline param sub-screen(s) a pick requires, then return to the hub
 * (the fdsetup "pick -> params -> back" flow). Shared by BOTH picker levels so
 * the walk cannot diverge between them. */
static void snd_walk_sub(int sub)
{
  tui_clear();
  switch (sub)
  {
    case MCARD_SUB_SB:  screen_hardware(); break;
    case MCARD_SUB_GUS: snd_pick_gusvoices(); break;
    case MCARD_SUB_ORGANYA:
      screen_hardware();
      /* T8 auto-suggest: Organya on a sub-Pentium CPU enables the PCM
       * pre-render cache (user can toggle it back off in Music options). */
      recommend_org_prerender(&g_cfg, &g_prof);
      break;
    default: break; /* adlib / No Music: no per-card hardware */
  }
  tui_clear();
}

/* v2 SESSION MEMORY of the last MIDI card (defined here so snd_pick_music_card
 * can seed its start row from it -- #21). Captured on LEAVING MIDI in
 * snd_type_cycle; -1 = unset -> a fresh cycle/pick into MIDI lands on Auto-detect
 * (MUSIC_CARDS[0]), the operator-specified default. SETUP-only, never persisted.
 * The full rationale lives in the comment block just above snd_type_cycle. */
static int g_last_card = -1;

/* "Select Music Card" -- the flat HARDWARE picker (Sound hub "Select Music
 * Card" row). Writes AUDIO_BACKEND (+ MIDI_DEV for the two wb synths), then
 * walks that card's sub-screen. #21: reachable under ANY Music Type now; picking
 * a card writes a MIDI backend, so it also switches Type to MIDI. ESC = abandon
 * with NO cfg change. Returns 1 when a card was committed, 0 on ESC. */
static int snd_pick_music_card(void)
{
  const char *items[MUSIC_NCARDS];
  const char *tags[MUSIC_NCARDS];
  const char *descs[MUSIC_NCARDS];
  int beidx = scfg_index("AUDIO_BACKEND");
  int mdidx = scfg_index("MIDI_DEV");
  /* "(current)" resolves from the LIVE cfg. #21: the row is now selectable under
   * ANY Type, so cur may be -1 here (Organya / No Music have no card in play) --
   * then nothing is tagged and the list starts on the remembered card
   * (g_last_card), or Auto-detect when there is no memory. Picking a card writes
   * a MIDI backend, which switches Type to MIDI for free. */
  int cur = music_card_index(scfg_get(&g_cfg, beidx), scfg_get(&g_cfg, mdidx));
  int i, choice, start = (cur >= 0) ? cur : (g_last_card >= 0 ? g_last_card : 0);

  for (i = 0; i < MUSIC_NCARDS; ++i)
  {
    items[i] = MUSIC_CARDS[i].label;
    descs[i] = MUSIC_CARDS[i].desc;
    tags[i]  = (i == cur) ? "(current)" : "";
  }

  choice = tui_picklist("Select Music Card", 0, 0, items, tags, descs,
                        MUSIC_NCARDS, start, 0, NULL, NULL, 0);
  if (choice < 0 || choice >= MUSIC_NCARDS) return 0; /* ESC: abandon, no change */

  if (strcmp(scfg_get(&g_cfg, beidx), MUSIC_CARDS[choice].value) != 0)
  {
    scfg_set(&g_cfg, beidx, MUSIC_CARDS[choice].value);
    g_dirty = 1;
    /* D4: the music card (backend) changed -> the prior test badges are stale.
     * Both the music test and the backend-derived SFX test no longer describe
     * what will play, so drop them back to "Not tested". */
    g_res_music = -2;
    g_res_sfx   = -2;
  }
  /* For a wb card, record WHICH one (SETUP-only label/MPU-default
   * discriminator; the engine ignores MIDI_DEV). Non-wb cards leave it be. */
  if (MUSIC_CARDS[choice].midi_dev &&
      strcmp(scfg_get(&g_cfg, mdidx), MUSIC_CARDS[choice].midi_dev) != 0)
  {
    scfg_set(&g_cfg, mdidx, MUSIC_CARDS[choice].midi_dev);
    g_dirty = 1;
    g_res_music = -2; /* D4: a different wb synth -> stale music badge */
  }

  snd_walk_sub(MUSIC_CARDS[choice].sub);
  snd_test_after_pick(1); /* v2: offer the music test right after the hardware */
  return 1;
}

/* "Select Music Type" picker (Sound hub row 1) -- the FIRST level. Asks WHAT
 * KIND of music (Organya / MIDI / Auto-detect / No Music); MIDI descends into
 * the synth list, the other three write AUDIO_BACKEND directly and walk their
 * sub-screen (Organya: BLASTER hardware + the pre-render auto-suggest, which is
 * why the Music-options pre-render row is no longer an undiscoverable greyed
 * line; Auto-detect: BLASTER hardware; No Music: nothing).
 *
 * The SFX list does NOT need an explicit re-narrow write: SFX_DEVICE only ever
 * stores "none" (off) or is unset (the engine derives the native device from
 * the backend), so a music change re-points the derived SFX device for free.
 * ESC = abandon, no cfg change. Returns 1 (a list was shown), matching the
 * screen_sound_menu Enter contract. */
/* v2 SESSION MEMORY of the last MIDI card, for the Organya <-> MIDI toggle.
 *
 * When Type is Organya or No Music, AUDIO_BACKEND holds organya/none -- so the
 * CARD IS NOT REPRESENTABLE IN THE CFG and there is nothing to restore from the
 * file when the user cycles back to MIDI. This is a SETUP-only, in-RAM memory
 * (no new cfg key, never persisted): -1 = unset -> a fresh cycle into MIDI lands
 * on Auto-detect (MUSIC_CARDS[0]), which is the operator-specified default.
 *
 * It is captured ON LEAVING MIDI (snd_type_cycle below), reading the live cfg --
 * so a card picked in the picker is remembered without any extra bookkeeping.
 *
 * We deliberately do NOT reconstruct the card from the MIDI_DEV that survives in
 * the cfg across an Organya round-trip: MIDI_DEV only discriminates the two wb
 * cards, so half-recovering a card family would be more surprising than the
 * clean Auto-detect default. Documented so nobody "fixes" it later.
 * (g_last_card is DEFINED above snd_pick_music_card, which now seeds its picker
 * start row from it -- #21 made that picker reachable under Organya / No Music.) */

/* Cycle the Music Type row in place (Left/Right/Space/Enter). Writes
 * AUDIO_BACKEND for the new type: Organya/No Music write their own value; MIDI
 * writes the REMEMBERED card's backend (Auto-detect when there is no memory),
 * so the cfg is never left in a half-state. */
static void snd_type_cycle(int dir)
{
  int beidx = scfg_index("AUDIO_BACKEND");
  int mdidx = scfg_index("MIDI_DEV");
  const char *be = scfg_get(&g_cfg, beidx);
  int cur = music_type_of(be);
  int next = (cur + dir + MTYPE_NTYPES) % MTYPE_NTYPES;

  if (next == cur) return;

  /* D4: the music type is changing -> the prior music/SFX test badges no longer
   * describe what will play; drop them both back to "Not tested". */
  g_res_music = -2;
  g_res_sfx   = -2;

  /* Leaving MIDI: remember the card we were on (read from the LIVE cfg). */
  if (cur == MTYPE_MIDI)
    g_last_card = music_card_index(be, scfg_get(&g_cfg, mdidx));

  if (next == MTYPE_MIDI)
  {
    int c = (g_last_card >= 0) ? g_last_card : 0; /* unset -> Auto-detect */
    if (strcmp(scfg_get(&g_cfg, beidx), MUSIC_CARDS[c].value) != 0)
    {
      scfg_set(&g_cfg, beidx, MUSIC_CARDS[c].value);
      g_dirty = 1;
    }
    if (MUSIC_CARDS[c].midi_dev &&
        strcmp(scfg_get(&g_cfg, mdidx), MUSIC_CARDS[c].midi_dev) != 0)
    {
      scfg_set(&g_cfg, mdidx, MUSIC_CARDS[c].midi_dev);
      g_dirty = 1;
    }
    return;
  }

  /* Organya / No Music: write the type's own backend. SFX_DEVICE is left as-is
   * (unset/empty -> the engine derives the native SFX device); a user who wants
   * music-only picks "No Sound FX" in the FX picker. */
  if (strcmp(scfg_get(&g_cfg, beidx), MUSIC_TYPES[next].value) != 0)
  {
    scfg_set(&g_cfg, beidx, MUSIC_TYPES[next].value);
    g_dirty = 1;
  }

  /* DELIBERATELY NO snd_walk_sub() HERE -- a deviation from SETUP-UX-V2-PLAN
   * sec.4.3, found while implementing it. This is a CYCLE row: Left/Right must
   * change the value IN PLACE (the v2 value-row grammar). Walking a sub-screen
   * per cycle would pop the BLASTER hardware screen open on every keypress that
   * lands on Organya -- unusable, and it would trap a user just scrubbing
   * through the three types.
   *
   * Nothing is lost. Organya's SB hardware is still reachable, through the same
   * single door as every other SB config: the Sound FX Card picker (Organya
   * renders to the SB DAC, so Sound Blaster IS its effects device). And the
   * pre-render auto-suggest below is silent (a cfg write, no modal), so it still
   * fires exactly when it should. */
  if (next == MTYPE_ORGANYA)
    recommend_org_prerender(&g_cfg, &g_prof);
}

/* "Select Sound FX Device" picker (Sound hub row 2). iter #3 (#21): the list is
 * NO LONGER narrowed by the music card -- the operator directive is that the SFX
 * hardware is ALWAYS selectable. Every device is offered on every music card:
 * Sound Blaster / Gravis UltraSound / No Sound FX. Non-functional combos are
 * allowed; the DESCRIPTION pane warns, nothing blocks. nx-eng verified (task #8)
 * that the engine treats SFX_DEVICE as a pure ON/OFF toggle -- any non-"none"
 * value means "SFX on via the backend's NATIVE device", so an un-honored cross
 * combo (e.g. "gus" under OPL3 music) degrades silently to the native device,
 * never a crash.
 *
 * Raw values kept simple and honest:
 *   Sound Blaster     -> ""    (engine derives the native device: SB on every
 *                               SB-family / Organya / No-Music backend)
 *   Gravis UltraSound -> "gus" (explicit; honored only when the music is GUS,
 *                               else the engine falls back to the native device)
 *   No Sound FX       -> "none" (dispatch off)
 *
 * When Sound Blaster is picked AND it is genuinely the effects path (not under
 * GUS or AdLib music, where the native device is the GF1 / PC speaker), we walk
 * the BLASTER Port/IRQ/DMA sub-screen inline -- the SINGLE door to SB hardware
 * when SB does effects but not music. Returns 1. */
static int snd_pick_sfx_device(void)
{
  const char *be = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  int idx = scfg_index("SFX_DEVICE");
  const char *items[3], *descs[3], *raw[3];
  int gus   = strcmp(be, "gus") == 0;
  int adlib = strcmp(be, "adlib") == 0;
  const char *cur = scfg_get(&g_cfg, idx);
  int start, choice, sb_walk;

  /* Row 0: Sound Blaster ("" -> engine derives the native device). */
  items[0] = "Sound Blaster";
  descs[0] = gus
    ? "Sound effects on the Sound Blaster DAC. NOTE: under Gravis UltraSound "
      "music the effects play on the GF1 instead -- choose a Sound Blaster "
      "music card for SB effects."
    : adlib
    ? "Sound effects on the Sound Blaster DAC. NOTE: AdLib (OPL2) music has no "
      "DAC, so effects fall back to the PC speaker."
    : "Sound effects play through the Sound Blaster DAC.";
  raw[0] = "";

  /* Row 1: Gravis UltraSound ("gus"). */
  items[1] = "Gravis UltraSound";
  descs[1] = gus
    ? "Sound effects play on the Gravis UltraSound GF1 wavetable, alongside the "
      "music (shared card voices)."
    : "Sound effects on the Gravis UltraSound GF1. NOTE: requires Gravis "
      "UltraSound music mode to function; under other music the effects fall "
      "back to the Sound Blaster.";
  raw[1] = "gus";

  /* Row 2: No Sound FX ("none" -> dispatch off). */
  items[2] = "No Sound FX";
  descs[2] = "Turn sound effects off. Music keeps playing.";
  raw[2] = "none";

  /* Start on the row matching the stored value (an explicit "gus" or "none");
   * everything else -- "", unset, a legacy value -- is the derive-native SB row. */
  start = (strcmp(cur, "none") == 0) ? 2 : (strcmp(cur, "gus") == 0) ? 1 : 0;

  choice = tui_picklist("Select Sound FX Device", 0, 0, items, NULL, descs,
                        3, start, 0, NULL, NULL, 0);
  if (choice < 0 || choice >= 3) return 1; /* ESC: abandon, no change */

  if (strcmp(scfg_get(&g_cfg, idx), raw[choice]) != 0)
  {
    scfg_set(&g_cfg, idx, raw[choice]);
    g_dirty = 1;
    g_res_sfx = -2; /* D4: the SFX device changed -> the old badge is stale */
  }

  /* Walk the SB hardware sub-screen only when Sound Blaster is genuinely the
   * effects path. Under GUS or AdLib music the native SFX device is the GF1 /
   * PC speaker regardless of this pick, so the SB Port/IRQ/DMA screen would be
   * meaningless there. */
  sb_walk = (raw[choice][0] == '\0' && !gus && !adlib);
  if (sb_walk)
  {
    tui_clear();
    screen_hardware();
    tui_clear();
  }
  /* Offer the SFX test right after the device is configured. Skipped when the
   * user just turned effects OFF -- there is nothing to hear. */
  if (strcmp(scfg_get(&g_cfg, idx), "none") != 0)
    snd_test_after_pick(0);
  return 1;
}

/* Full music name for the hub banner + the Select-Music-Type row value: the
 * type, qualified by the synth when the type is MIDI ("MIDI: OPL3 FM"). */
static const char *music_card_name(void)
{
  return snd_backend_name(scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND")));
}

/* SHORT music-device name for the banner's "Card" row on a no-SB config (AdLib
 * OPL / Gravis GF1), where the row names the music device instead of an idle SB
 * card. Uses the synth-level resolver so the row reads "Gravis UltraSound", not
 * the type-qualified "MIDI: Gravis UltraSound"; a non-MIDI type (No Music)
 * falls back to its type label. */
static const char *music_device_name(void)
{
  const char *be = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  const char *md = scfg_get(&g_cfg, scfg_index("MIDI_DEV"));
  const char *s  = music_card_short(be, md);
  return s[0] ? s : music_card_label(be, md);
}

/* R-C/R-G: a current-sound-settings banner at the top of the Sound submenu,
 * formatted to MATCH the main-menu SYSTEM PROFILE panel exactly: titled
 * "SOUND", one setting per row in a SINGLE aligned column with dotted leaders.
 * #9: leads with the two device lines (Music Device / Sound FX Device --
 * spec 5.2), then the SB Card / Port / IRQ / DMA detail. Box height 8 = 6 rows
 * + borders. */
static void draw_sound_banner(int x, int y, int w)
{
  char v[40];
  int  kx = x + 2, vw = 11, row = y + 1;
  tui_box(x, y, w, 8, "SOUND");
  /* #9 spec 5.2: lead with the two device picks, then the SB hardware detail. */
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Music ....",
         music_card_name());
  tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Sound FX .",
         sfx_current_name());
  /* Card/Port/IRQ/DMA describe the SB16 -- meaningful only when the SB is in
   * use. On a no-SB config (AdLib OPL or Gravis GF1 music with non-SB SFX) the
   * SB hardware is idle, so show the music device as the Card and "n/a" for the
   * SB-specific Port/IRQ/DMA. All 6 rows stay (box height unchanged). */
  if (sb_in_use())
  {
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Card .....",
           sb_type_name(cfg_card_type()));
    snprintf(v, sizeof(v), "0x%X", cfg_io_port());
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Port .....", v);
    snprintf(v, sizeof(v), "%d", cfg_irq());
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "IRQ ......", v);
    snprintf(v, sizeof(v), "%d", cfg_dma());
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "DMA ......", v);
  }
  else
  {
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Card .....",
           music_device_name());
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "Port .....", "n/a");
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "IRQ ......", "n/a");
    tui_kv(kx, row++, vw, PAL->body, PAL->value, PAL->bg, "DMA ......", "n/a");
  }
}

/* T-DFUX-P2: Express setup -- DF's one-key "Detect", reimagined. (1) a red
 * DANGER modal (DF 3328: the probe pokes card ports; a misconfigured box can
 * hang). (2) re-run the EXISTING profile.c probes (BLASTER parse / DSP reset +
 * version 0xE1 / OPL3 timer / MPU-401 DRR -- all inside profile_detect). (3) an
 * EVIDENCE modal (DF 3329: report the DSP version found, not just "ok"). (4)
 * write the detected BLASTER + recommended backend to the SESSION (T44: live,
 * ESC keeps, main-menu Save/F10 commits -- never a direct file write here).
 * (5) offer the inline music test. Audio tier (AUDIO_TIER2 / ORG_PRERENDER) is
 * deliberately NOT touched (operator default: the Speed/Express macros do not
 * change audio fidelity). */
static void screen_express(void)
{
  char bl[SCFG_VAL_MAX];
  char l0[80], l1[80], l2[80], l3[80], l4[80];
  const char *warn[4];
  const char *ev[5];
  const char *backend;

  warn[0] = "Express setup will now probe your sound hardware.";
  warn[1] = "It reads the card ports directly. If the screen stays";
  warn[2] = "frozen for more than a few seconds the card settings are";
  warn[3] = "wrong -- reboot and use Custom setup instead.";
  tui_message_warn("DETECTING HARDWARE", warn, 4);

  /* Re-run the probes (full re-detect; a brief video-bench flash is expected). */
  profile_detect(&g_prof);
  /* The probe's mode-flash + the prior warning modal leave residue (a text-page
   * re-set does not reliably clear under every BIOS/DOSBox); clear before the
   * evidence modal so it renders over a clean backdrop. */
  tui_clear();

  /* Compose the detected BLASTER + pick the recommended backend (OPL3 when a
   * card/FM is present, else auto-detect chain). WaveBlaster is never
   * auto-selected here (operator directive: WB is a manual Sound choice). */
  if (g_prof.snd_base > 0)
  {
    int n = snprintf(bl, sizeof bl, "A%X I%d D%d H%d",
                     g_prof.snd_base, g_prof.snd_irq,
                     g_prof.snd_dma, g_prof.snd_hdma);
    if (g_prof.snd_type > 0 && n > 0 && n < (int)sizeof bl)
      snprintf(bl + n, sizeof bl - (size_t)n, " T%d", g_prof.snd_type);
  }
  else
    bl[0] = '\0';
  backend = (g_prof.has_opl3 || g_prof.snd_detected) ? "opl3" : "auto";

  /* One fact per line, consistent "Label: Value", Title Case, no trailing
   * periods (operator review). "(MIDI)" makes clear OPL3/WaveBlaster ARE the
   * MIDI backends. */
  if (g_prof.snd_detected)
    snprintf(l0, sizeof l0, "Sound Blaster: Port 0x%X, IRQ %d, DMA %d",
             g_prof.snd_base, g_prof.snd_irq, g_prof.snd_dma);
  else
    snprintf(l0, sizeof l0, "Sound Blaster: Not detected");
  if (g_prof.dsp_major)
    snprintf(l1, sizeof l1, "DSP Version: %d.%02d", g_prof.dsp_major, g_prof.dsp_minor);
  else
    snprintf(l1, sizeof l1, "DSP Version: None");
  snprintf(l2, sizeof l2, "OPL3 FM: %s", g_prof.has_opl3 ? "Yes" : "No");
  snprintf(l3, sizeof l3, "WaveBlaster: %s", g_prof.has_waveblaster ? "Yes" : "No");
  /* #9: render the backend through the SINGLE music-card label source so the
   * Express evidence modal matches the picker + banner wording exactly. */
  snprintf(l4, sizeof l4, "Music Backend: %s", snd_backend_name(backend));
  ev[0] = l0; ev[1] = l1; ev[2] = l2; ev[3] = l3; ev[4] = l4;
  tui_message("DETECTION COMPLETE", ev, 5);

  /* Write the two keys to the SESSION (T44 live; main-menu Save commits). */
  if (bl[0]) scfg_set(&g_cfg, scfg_index("BLASTER"), bl);
  scfg_set(&g_cfg, scfg_index("AUDIO_BACKEND"), backend);
  g_dirty = 1;

  /* Clear the DETECTION COMPLETE modal first -- modals draw over whatever is on
   * screen, so without this the next dialog stacks on top of it. */
  tui_clear();
  /* Do NOT auto-play a test here (operator review: the inline test's result was
   * discarded so "Test music" still read "Not tested"). Point the user at the two
   * Test rows in the Sound menu, which run the test AND record the Working badge. */
  {
    const char *done[1] = { "Test sound effects and music to confirm." };
    tui_message("SOUND CONFIGURED", done, 1);
  }
}

/* Is a Sound-menu row selectable right now? Greyed rows render dimmed and are
 * SKIPPED by navigation (the same mechanism the Music-options screen uses).
 *
 * v2 grey rules (iter #3 #21: HARDWARE pickers never grey):
 *   Select Music Card -- ALWAYS selectable. Operator directive: the hardware
 *     picker is always reachable. Under Organya / No Music it still displays the
 *     remembered card (see g_last_card); picking a card there switches Type to
 *     MIDI (snd_pick_music_card writes that card's MIDI backend).
 *   Test music        -- pointless with No Music (stays greyed).
 *   Test sound effects-- pointless when the FX device is "No Sound FX" (greyed).
 * Music Options stays selectable under No Music on purpose: Audio quality still
 * governs the SFX mix rate, so greying the row would hide a live setting. */
static int snd_hub_active(int row, int rc_muscard, int rc_testmus, int rc_testsfx)
{
  (void)rc_muscard; /* #21: Select Music Card is always selectable now */
  if (row == rc_testmus) return music_type_of(
      scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"))) != MTYPE_NONE;
  if (row == rc_testsfx) return strcmp(sfx_current_name(), "No Sound FX") != 0;
  return 1;
}

/* Move the Sound-menu selection by dir, skipping greyed rows. Falls back to the
 * current row if nothing else is live (cannot happen -- Express/Back always are,
 * but the guard keeps the loop total). */
static int snd_hub_step(int sel, int dir, int n,
                        int rc_muscard, int rc_testmus, int rc_testsfx)
{
  int i, r = sel;
  for (i = 0; i < n; ++i)
  {
    r = (r + dir + n) % n;
    if (snd_hub_active(r, rc_muscard, rc_testmus, rc_testsfx)) return r;
  }
  return sel;
}

/* The label shown on the Select-Music-Card row. Under Type == MIDI it is the
 * live card. Under Organya / No Music the row is greyed, but it still shows the
 * REMEMBERED card (or Auto-detect when there is no memory) -- i.e. exactly what
 * cycling back to MIDI would restore, so the greyed row is informative rather
 * than blank. */
static const char *snd_hub_card_label(void)
{
  const char *be = scfg_get(&g_cfg, scfg_index("AUDIO_BACKEND"));
  int c;
  if (music_type_of(be) == MTYPE_MIDI)
    c = music_card_index(be, scfg_get(&g_cfg, scfg_index("MIDI_DEV")));
  else
    c = g_last_card; /* -1 (unset) -> Auto-detect, the fresh default */
  if (c < 0) c = 0;
  return MUSIC_CARDS[c].label;
}

static void screen_sound_menu(void)
{
  /* v2 Sound menu (SETUP-UX-V2-PLAN sec.3). The two questions the old flat list
   * conflated now sit on adjacent rows: "Music Type" (what KIND of music -- an
   * in-place 3-state CYCLE row, not a picker) and "Select Music Card" (WHICH
   * hardware plays it -- the restored flat picker, greyed unless Type == MIDI).
   * Then the FX picker, the adaptive Music Options, and the inline tests.
   *
   * There is NO standalone "Sound card hardware" row (operator UX): SB
   * Port/IRQ/DMA is configured inline by whichever picker puts the SB into use --
   * the music-card pick for an SB-family card, or the SFX pick when Sound
   * Blaster is the effects device. */
  enum { ROW_EXPRESS = 0, ROW_MUSTYPE, ROW_MUSCARD, ROW_SFXDEV, ROW_MUSOPTS,
         ROW_TESTMUS, ROW_TESTSFX, ROW_BACK, ROW_NROWS };
  static const char *items[] = {
    "Express setup",
    "Music Type", "Select Music Card", "Select Sound FX Card",
    "Music Options",
    "Test music", "Test sound effects", "Back"
  };
  static const char *helps[] = {
    "Detect the sound card and set everything in one step.",
    "What KIND of music: Organya (built-in, no sound card needed), MIDI (played "
    "by a synth on a sound card), or No Music. Left/Right changes it.",
    "WHICH card plays the music, and sets up its hardware. Picking a card here "
    "switches Music Type to MIDI.",
    "Choose where sound effects play. Every device is offered; the description "
    "warns about combinations that need a matching music card.",
    "Per-card music extras: GUS voices, MIDI set, Organya pre-render, quality.",
    "Play a test song to check the music.",
    "Play a test sound effect to check the audio.",
    "Return to the main menu."
  };
  const int n = ROW_NROWS;
  const int bx = 11, bw = 60;     /* R-O: x=11 centers a 60-wide box (10/10) */
  const int vcol = bx + 1 + 30;   /* value/badge column for the value rows    */
  /* Bottom-anchor the menu box just above the DESCRIPTION box instead of a
   * fixed top row. The banner occupies rows 3-10; the DESCRIPTION box is
   * TUI_DESC_TOP(4)=20..23. A fixed my=12 put the (n+2)-tall box at rows
   * 12..20, so its bottom border collided with the DESCRIPTION top border
   * (row 20). Anchoring to TUI_DESC_TOP(4)-(n+2)=11 lands it at rows 11..19,
   * abutting the banner above and the DESCRIPTION below with no overlap, and
   * stays correct if the row count changes. */
  const int my = TUI_DESC_TOP(4) - (n + 2);
  /* v2: the badges are file-scope now (g_res_sfx / g_res_music) so the
   * test-after-pick prompt inside the pickers feeds the SAME badge these rows
   * show. They persist for the whole SETUP session, not just this screen. */
  int sel = 0;

  tui_clear();
  for (;;)
  {
    int i, k;
    tui_titlebar("SOUND");
    draw_sound_banner(TUI_DESC_X, 3, TUI_DESC_W);
    tui_box(bx, my, bw, n + 2, "SOUND");
    for (i = 0; i < n; ++i)
    {
      int active = snd_hub_active(i, ROW_MUSCARD, ROW_TESTMUS, ROW_TESTSFX);
      int selrow = (i == sel);
      /* house convention (screen_sound / screen_advanced): dim WINS over the
       * selection highlight, so a greyed row can never masquerade as focused. */
      int fg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->body);
      int bg = (selrow && active) ? PAL->sel_bg : PAL->bg;
      int vfg = !active ? PAL->dim : (selrow ? PAL->sel_fg : PAL->value);
      char row[64];
      snprintf(row, sizeof(row), " %-*.*s", bw - 3, bw - 3, items[i]);
      tui_at(bx + 1, my + 1 + i, fg, bg, row);
      /* Value rows show their current value; the test rows show a result badge.
       * The card row keeps displaying the remembered card even while greyed. */
      if (i == ROW_MUSTYPE)
        tui_at(vcol, my + 1 + i, vfg, bg,
               MUSIC_TYPES[music_type_of(scfg_get(&g_cfg,
                             scfg_index("AUDIO_BACKEND")))].label);
      else if (i == ROW_MUSCARD)
        tui_at(vcol, my + 1 + i, vfg, bg, snd_hub_card_label());
      else if (i == ROW_SFXDEV)
        tui_at(vcol, my + 1 + i, vfg, bg, sfx_current_name());
      else if (i == ROW_TESTMUS || i == ROW_TESTSFX)
        tui_at(vcol, my + 1 + i, vfg, bg,
               audiotest_badge(i == ROW_TESTMUS ? g_res_music : g_res_sfx));
    }
    tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
    tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg, helps[sel]);
    /* v2 value-row grammar: state the keys that actually work on THIS row. */
    tui_status(sel == ROW_MUSTYPE ? "Left/Right Change   ESC Back"
                                  : "Enter Select   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP)
      sel = snd_hub_step(sel, -1, n, ROW_MUSCARD, ROW_TESTMUS, ROW_TESTSFX);
    else if (k == TUI_KEY_DOWN)
      sel = snd_hub_step(sel, +1, n, ROW_MUSCARD, ROW_TESTMUS, ROW_TESTSFX);
    else if (k == TUI_KEY_HOME) sel = 0;   /* row 0 (Express) is always active */
    else if (k == TUI_KEY_ESC)  return;
    /* v2: Music Type is a CYCLE row -- Left/Right/Space change it in place. A
     * type change can grey the row the cursor is on (e.g. Test music under No
     * Music), so re-seat the selection onto a live row afterwards. */
    else if ((k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT || k == ' ') &&
             sel == ROW_MUSTYPE)
    {
      snd_type_cycle(k == TUI_KEY_LEFT ? -1 : +1);
      tui_clear(); /* snd_walk_sub may have drawn a sub-screen */
    }
    else if (k == TUI_KEY_ENTER)
    {
      if (sel == ROW_EXPRESS)       screen_express();   /* one-key detect */
      /* Enter on the cycle row advances it, matching Right (grammar: Enter is
       * never a dead key on a value row). */
      else if (sel == ROW_MUSTYPE)  snd_type_cycle(+1);
      else if (sel == ROW_MUSCARD)  snd_pick_music_card();
      else if (sel == ROW_SFXDEV)   snd_pick_sfx_device();
      else if (sel == ROW_MUSOPTS)  screen_sound();      /* Music Options */
      else if (sel == ROW_TESTMUS)  g_res_music = sound_inline_test(1);
      else if (sel == ROW_TESTSFX)  g_res_sfx   = sound_inline_test(0);
      else return; /* Back */
      tui_clear(); /* a sub-screen / popup overwrote the screen; repaint clean */
    }
    /* A cycle or a pick may have greyed the current row -- never leave the
     * cursor parked on a dead row. */
    if (!snd_hub_active(sel, ROW_MUSCARD, ROW_TESTMUS, ROW_TESTSFX))
      sel = snd_hub_step(sel, +1, n, ROW_MUSCARD, ROW_TESTMUS, ROW_TESTSFX);
  }
}

/* ---- Configure keyboard (plan 3.5a) --------------------------------- *
 * Two-column action/key list of the 11 player actions (engine INPUTS enum
 * order), with two group blanks (after Down, after Strafe) per the mockup.
 * Enter/Space on a row captures the next keypress and binds it; binding a key
 * already used by another action SWAPS the two (Q-B1) and flashes both rows.
 * Edits are live to the session binding model (g_binds) + written through to
 * the config (BIND_* keys, once the engine registry defines them). */

/* Display-row -> action index (0..10), or -1 for a group-separator blank. */
static const int KBD_ROWS[] = { 0, 1, 2, 3, -1, 4, 5, 6, -1, 7, 8, 9, 10 };
#define KBD_NROWS ((int)(sizeof(KBD_ROWS) / sizeof(KBD_ROWS[0])))
#define KBD_BX 16   /* box left (centered: (80-50)/2+1) */
#define KBD_BW 50

static const char *keydisp(long keycode)
{
  const setup_key_t *k = scancode_by_keycode(keycode);
  return k ? k->display : "(unbound)";
}

/* Draw one keyboard row. style: 0 normal, 1 selected, 2 flash (just-swapped). */
static void kbd_draw_row(int by, int drow, int style)
{
  int action = KBD_ROWS[drow];
  int y = by + 1 + drow;
  int fg = (style == 1) ? PAL->sel_fg : (style == 2) ? PAL->warn_fg : PAL->body;
  int bg = (style == 1) ? PAL->sel_bg : (style == 2) ? PAL->warn_bg : PAL->bg;
  int vfg = (style == 1) ? PAL->sel_fg : (style == 2) ? PAL->warn_fg : PAL->value;
  char row[KBD_BW - 1];
  if (action < 0) { /* blank separator */
    snprintf(row, sizeof(row), " %-*s", KBD_BW - 3, "");
    tui_at(KBD_BX + 1, y, PAL->body, PAL->bg, row);
    return;
  }
  snprintf(row, sizeof(row), " %-*s", KBD_BW - 3, "");
  tui_at(KBD_BX + 1, y, fg, bg, row);                       /* row background */
  tui_at(KBD_BX + 2, y, fg, bg, g_binds[action].name);      /* action name    */
  tui_at(KBD_BX + 20, y, vfg, bg, keydisp(g_binds[action].keycode)); /* key   */
}

static void kbd_render(int by, int sel_drow)
{
  int i;
  tui_titlebar("CONFIGURE KEYBOARD");
  tui_box(KBD_BX, by, KBD_BW, KBD_NROWS + 2, "CONFIGURE KEYBOARD");
  for (i = 0; i < KBD_NROWS; ++i)
    kbd_draw_row(by, i, (i == sel_drow) ? 1 : 0);
}

/* Flash the two display rows whose actions were just swapped (Q-B1). */
static void kbd_flash(int by, int da, int db, int sel_drow)
{
  int n;
  for (n = 0; n < 3; ++n)
  {
    kbd_draw_row(by, da, 2); kbd_draw_row(by, db, 2);
    tui_present(); tui_delay_ms(110);
    kbd_draw_row(by, da, (da == sel_drow)); kbd_draw_row(by, db, (db == sel_drow));
    tui_present(); tui_delay_ms(90);
  }
}

static int action_to_drow(int action)
{
  int i;
  for (i = 0; i < KBD_NROWS; ++i) if (KBD_ROWS[i] == action) return i;
  return 0;
}

/* Centered "Press a key" capture popup (issue 3): replaces the old bottom-row
 * status prompt with a modal box drawn over the screen, and captures one key --
 * including a lone Shift/Ctrl/Alt via the BIOS shift byte (issue 1). Returns the
 * captured key, or NULL with *cancel set when the player presses Esc. An
 * unsupported key (Enter, a function key, ...) re-prompts in place. */
static const setup_key_t *capture_key_popup(const char *action, int *cancel)
{
  char title[64];
  int w = 52, h = 5;
  int x = (80 - w) / 2 + 1;
  int y = (25 - h) / 2;
  int err = 0;

  *cancel = 0;
  snprintf(title, sizeof(title), "Press a key to map to %s", action);
  for (;;)
  {
    int ext = 0, code = 0, mod = SETUP_MOD_NONE, cncl;
    const setup_key_t *k;
    char blank[52];
    snprintf(blank, sizeof(blank), "%-*s", w - 4, "");
    tui_box(x, y, w, h, "REMAP KEY");
    tui_at(x + 2, y + 1, PAL->body, PAL->bg, blank);
    tui_at(x + 2, y + 2, err ? PAL->warn_fg : PAL->desc, PAL->bg, blank);
    tui_at(x + 2, y + 1, PAL->body, PAL->bg, title);
    tui_at(x + 2, y + 2, err ? PAL->warn_fg : PAL->desc, PAL->bg,
           err ? "Unsupported key -- try another.   ESC cancels."
               : "Shift / Ctrl / Alt are allowed.   ESC cancels.");
    tui_popup_decorate(x, y, w, h);
    tui_present();

    cncl = tui_capture_key_mod(&ext, &code, &mod);
    if (cncl) { *cancel = 1; return NULL; }
    if (mod != SETUP_MOD_NONE) return scancode_modifier(mod);
    k = scancode_decode(ext, code);
    if (!k) { err = 1; continue; }       /* Enter / F-key / nav -> re-prompt */
    return k;
  }
}

static void screen_keyboard(void)
{
  int by = menu_box_top(3, TUI_DESC_TOP(4), KBD_NROWS + 2);
  int sel = 0; /* first display row is action 0 (Left) */

  binds_ensure();
  tui_clear();
  for (;;)
  {
    int k, action = KBD_ROWS[sel];
    kbd_render(by, sel);
    tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
    tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg,
             "Select an action and press Enter, then press the key to bind to it. "
             "Reusing a key swaps the two actions.");
    tui_status("Up/Down Move   Enter Rebind   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN)
    {
      int dir = (k == TUI_KEY_UP) ? -1 : +1;
      do { sel = (sel + dir + KBD_NROWS) % KBD_NROWS; } while (KBD_ROWS[sel] < 0);
    }
    else if (k == TUI_KEY_HOME) sel = 0;
    else if (k == TUI_KEY_ENTER || k == TUI_KEY_SPACE)
    {
      int cancel;
      const setup_key_t *nk = capture_key_popup(g_binds[action].name, &cancel);
      tui_clear(); /* wipe the centered popup before the list repaints */
      if (!cancel && nk)
      {
        int conflict = bindings_find_key(g_binds, nk->keycode, action);
        if (conflict == action) { /* same key -> nothing to do */ }
        else if (conflict >= 0)
        {
          long oldkey = g_binds[action].keycode;  /* SWAP (Q-B1) */
          g_binds[action].keycode = nk->keycode;
          g_binds[conflict].keycode = oldkey;
          bindings_save(g_binds, &g_cfg); g_dirty = 1;
          kbd_render(by, sel);                    /* repaint before flashing */
          kbd_flash(by, sel, action_to_drow(conflict), sel);
        }
        else
        {
          g_binds[action].keycode = nk->keycode;
          bindings_save(g_binds, &g_cfg); g_dirty = 1;
        }
      }
    }
    else if (k == TUI_KEY_ESC) return; /* T44: live edits kept in the session */
  }
}

/* ---- Configure joystick (plan 3.5b) --------------------------------- *
 * The SDL3-DOS gameport is 2 axes / 4 buttons / 0 hats: axes are fixed to
 * movement (shown greyed as documentation), the 7 non-directional actions are
 * button-assignable to Button 1-4 or <None> (4 buttons < 7 actions, so <None>
 * is allowed -- Q-B2 defaults Jump/Fire/WpnPrev/WpnNext). Picking a button
 * another action uses swaps the two (consistent with the keyboard screen). */

/* Display rows: -1 = blank; -2/-3 = the fixed X/Y axis doc rows; >=0 = a
 * button-assignable action index. */
/* Row sentinels: -2/-3 = fixed-axis doc rows; -4 = the (selectable) Invert-Y
 * toggle; -1 = blank separator; >=0 = remappable action index (button assign). */
static const int JOY_ROWS[] = { -2, -3, -4, -1, 4, 5, 7, 8, 9, 10, 6 };
#define JOY_NROWS ((int)(sizeof(JOY_ROWS) / sizeof(JOY_ROWS[0])))
#define JOY_BX 16
#define JOY_BW 50

static void joybut_disp(int jbut, char *out, int cap)
{
  if (jbut < 0) snprintf(out, (size_t)cap, "<None>");
  else          snprintf(out, (size_t)cap, "Button %d", jbut + 1); /* 1-based UI */
}

/* Centered "press a joystick button" capture popup (issue 4): replaces the old
 * Button 1-4/<None> pick-list with a live poll of the game port -- the player
 * assigns by PRESSING the physical button. Returns 0..3 for the button pressed,
 * -1 for <None> (N / Backspace), or -2 on Esc cancel. Debounced: it waits for
 * all buttons released before accepting a press, so a button still held from a
 * prior action is not grabbed. */
static int capture_jbut_popup(const char *action)
{
  char title[64];
  int w = 54, h = 6;
  int x = (80 - w) / 2 + 1;
  int y = (25 - h) / 2;
  int seen_clear = 0;

  snprintf(title, sizeof(title), "Press a joystick button for %s", action);
  for (;;)
  {
    unsigned btn = 0;
    int present = joyport_sample(NULL, NULL, &btn);
    char blank[56];
    snprintf(blank, sizeof(blank), "%-*s", w - 4, "");
    tui_box(x, y, w, h, "ASSIGN BUTTON");
    tui_at(x + 2, y + 1, PAL->body, PAL->bg, blank);
    tui_at(x + 2, y + 2, PAL->desc, PAL->bg, blank);
    tui_at(x + 2, y + 3, PAL->dim,  PAL->bg, blank);
    tui_at(x + 2, y + 1, PAL->body, PAL->bg, title);
    tui_at(x + 2, y + 2, PAL->desc, PAL->bg,
           "Press a button.   N = None.   ESC cancels.");
    tui_at(x + 2, y + 3, present ? PAL->dim : PAL->warn_fg, PAL->bg,
           present ? "Game port ready." : "No joystick detected on the game port.");
    tui_popup_decorate(x, y, w, h);
    tui_present();

    if (!seen_clear) { if (btn == 0) seen_clear = 1; }
    else if (btn)
    {
      int b;
      for (b = 0; b < BIND_NJBUT; ++b) if (btn & (1u << b)) return b;
    }

    if (tui_kbhit())
    {
      int kk = tui_getkey();
      if (kk == TUI_KEY_ESC) return -2;
      if (kk == 'n' || kk == 'N' || kk == '\b') return -1; /* <None> */
    }
    else tui_delay_ms(30);
  }
}

static void screen_joystick_cfg(void)
{
  int by = menu_box_top(3, TUI_DESC_TOP(4), JOY_NROWS + 2);
  int sel = 2; /* first selectable row (Invert-Y toggle) */

  binds_ensure();
  tui_clear();
  for (;;)
  {
    int i, k, action;
    tui_titlebar("CONFIGURE JOYSTICK");
    tui_box(JOY_BX, by, JOY_BW, JOY_NROWS + 2, "CONFIGURE JOYSTICK");
    for (i = 0; i < JOY_NROWS; ++i)
    {
      int r = JOY_ROWS[i];
      int y = by + 1 + i;
      int selrow = (i == sel);
      char val[24];
      if (r == -1) continue;                       /* blank separator */
      if (r == -2 || r == -3)                       /* fixed axis doc row */
      {
        tui_at(JOY_BX + 2, y, PAL->dim, PAL->bg,
               r == -2 ? "Move Left/Right" : "Move Up/Down");
        tui_at(JOY_BX + 20, y, PAL->dim, PAL->bg,
               r == -2 ? "X Axis  (fixed)" : "Y Axis  (fixed)");
        continue;
      }
      if (r == -4)                                  /* Invert-Y toggle (selectable) */
      {
        int ii  = scfg_index("JOY_INVERT_Y");
        int fg  = selrow ? PAL->sel_fg : PAL->body;
        int bg  = selrow ? PAL->sel_bg : PAL->bg;
        int vfg = selrow ? PAL->sel_fg : PAL->value;
        char rowbuf[JOY_BW - 1];
        snprintf(rowbuf, sizeof(rowbuf), " %-*s", JOY_BW - 3, "");
        tui_at(JOY_BX + 1, y, fg, bg, rowbuf);
        tui_at(JOY_BX + 2, y, fg, bg, "Invert Y axis");
        if (ii >= 0) fmt_value(ii, val, sizeof(val));
        else         snprintf(val, sizeof(val), "Off");
        tui_at(JOY_BX + 20, y, vfg, bg, val);
        continue;
      }
      action = r;
      {
        int fg  = selrow ? PAL->sel_fg : PAL->body;
        int bg  = selrow ? PAL->sel_bg : PAL->bg;
        int vfg = selrow ? PAL->sel_fg : PAL->value;
        char row[JOY_BW - 1];
        snprintf(row, sizeof(row), " %-*s", JOY_BW - 3, "");
        tui_at(JOY_BX + 1, y, fg, bg, row);
        tui_at(JOY_BX + 2, y, fg, bg, g_binds[action].name);
        joybut_disp(g_binds[action].jbut, val, sizeof(val));
        tui_at(JOY_BX + 20, y, vfg, bg, val);
      }
    }
    tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
    tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg,
             "Assign a game-port button to each action, or flip Invert Y axis if "
             "your stick's up/down feels reversed. Pick <None> for unused actions.");
    tui_status("Up/Down Move   Enter Toggle/Assign   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN)
    {
      int dir = (k == TUI_KEY_UP) ? -1 : +1;
      /* -4 (Invert-Y toggle) is selectable; other negatives are skipped. */
      do { sel = (sel + dir + JOY_NROWS) % JOY_NROWS; }
      while (JOY_ROWS[sel] < 0 && JOY_ROWS[sel] != -4);
    }
    else if (k == TUI_KEY_HOME)
    {
      sel = 0;
      while (JOY_ROWS[sel] < 0 && JOY_ROWS[sel] != -4) ++sel;
    }
    else if ((k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT) && JOY_ROWS[sel] == -4)
    {
      int ii = scfg_index("JOY_INVERT_Y");
      if (ii >= 0) cycle_value(ii, (k == TUI_KEY_RIGHT) ? +1 : -1);
    }
    else if (k == TUI_KEY_ENTER || k == TUI_KEY_SPACE)
    {
      if (JOY_ROWS[sel] == -4) /* toggle Invert Y axis */
      {
        int ii = scfg_index("JOY_INVERT_Y");
        if (ii >= 0) cycle_value(ii, +1);
      }
      else
      {
        int newbut;
        action = JOY_ROWS[sel];
        newbut = capture_jbut_popup(g_binds[action].name); /* press-to-assign */
        tui_clear();
        if (newbut != -2 /* not cancelled */ && newbut != g_binds[action].jbut)
        {
          int conflict = bindings_find_jbut(g_binds, newbut, action);
          if (conflict >= 0) /* swap: the other action takes our old button */
            g_binds[conflict].jbut = g_binds[action].jbut;
          g_binds[action].jbut = newbut;
          bindings_save(g_binds, &g_cfg); g_dirty = 1;
        }
      }
    }
    else if (k == TUI_KEY_ESC) return;
  }
}

/* ---- Calibrate joystick (plan 3.5c) --------------------------------- *
 * The classic 3-step game-port calibration: center, upper-left, lower-right;
 * then a verify screen with live readout + a button-test row. The captured
 * min/max are written to the JOY_CAL config value so the engine need not
 * auto-swirl at start (persistence rides the SDL joy-cal hint patch, task #10);
 * if that config key is not yet defined the flow still runs as verify-only and
 * says so. SETUP reads the game port directly (joyport.c, no SDL). */

/* Live poll: redraw the readout each frame until the player presses ENTER to
 * record the step (returns 1, fills *ox,*oy) or Esc cancels (returns 0).
 * Issue 5: advancing is on an EXPLICIT keypress only -- stick movement and
 * button presses NEVER auto-advance (the old behavior grabbed the first button
 * press while the player was still centering). Buttons are not sampled here. */
static int calib_capture(int by, const char *l1, const char *l2, int *ox, int *oy)
{
  int x = 0, y = 0;
  int btn_armed = 0; /* button edge-detect: count a press only after a release */
  tui_clear();
  for (;;)
  {
    char rd[40];
    unsigned btn  = 0;
    int btn_press = 0;
    int present = joyport_sample(&x, &y, &btn);
    /* A joystick button records the step too (operator request), but ONLY on a
     * fresh press edge -- armed after a release -- so a held / carried-over
     * button never auto-advances while the player is still centering (issue-5). */
    if (btn == 0) btn_armed = 1;
    else if (btn_armed) { btn_press = 1; btn_armed = 0; }
    /* 56-wide box centered on the 80-col screen ((80-56)/2 = 12); text at col 14.
     * The longest line ("Connect a game-port stick, or press ESC to cancel.",
     * ~50 chars) ends well inside the col-66 interior edge -- the old 40-wide box
     * at col 20 clipped the prompts off the right border. */
    tui_titlebar("CALIBRATE JOYSTICK");
    tui_box(12, by, 56, 8, "CALIBRATE JOYSTICK");
    tui_at(14, by + 1, PAL->body, PAL->bg, l1);
    tui_at(14, by + 2, PAL->body, PAL->bg, l2);
    snprintf(rd, sizeof(rd), present ? "X = %-6d  Y = %-6d" : "no joystick detected",
             x, y);
    tui_at(14, by + 4, PAL->value, PAL->bg, rd);
    tui_at(14, by + 6, present ? PAL->desc : PAL->warn_fg, PAL->bg,
           present ? "Position the stick, then press Enter or a button"
                   : "Connect a game-port stick, or press ESC to cancel.");
    tui_status("Enter/Button Record   ESC Cancel");
    tui_present();

    if (tui_kbhit())
    {
      int k = tui_getkey();
      if (k == TUI_KEY_ESC) return 0;
      /* Only record when a stick is actually present: a -1 (open one-shot) must
       * never be stored -- input-sdl's validity gate rejects negative mins. */
      if ((k == TUI_KEY_ENTER || k == TUI_KEY_SPACE) && present)
        { *ox = x; *oy = y; return 1; }
    }
    else tui_delay_ms(40);
    /* a debounced joystick-button press records the step as well */
    if (btn_press && present) { *ox = x; *oy = y; return 1; }
  }
}

static void screen_joystick_cal(void)
{
  int by = menu_box_top(3, TUI_DESC_TOP(4), 8);
  int cx = 0, cy = 0, ulx = 0, uly = 0, lrx = 0, lry = 0;
  int minx, miny, maxx, maxy;
  int joyidx;
  char val[48];

  /* Capture the resting CENTER (a spring-return flightstick's electrical centre
   * is often not the geometric midpoint -- measure it, do not compute it) and
   * the two opposite corners. Samples are direct-port poll COUNTS (joyport.c,
   * the in-game domain). Per-axis min/max are then min/max of the corner samples
   * (robust to whichever travel direction makes the count rise), and the centre
   * is clamped into [min,max] so the value passes input-sdl's validity gate
   * (negative mins / range < 20 counts / centre outside [min,max] are rejected). */
  if (!calib_capture(by, "Step 1 of 3:", "Center the stick, then press Enter.",
                     &cx, &cy)) return;
  if (!calib_capture(by, "Step 2 of 3:", "Move to the UPPER-LEFT corner, press Enter.",
                     &ulx, &uly)) return;
  if (!calib_capture(by, "Step 3 of 3:", "Move to the LOWER-RIGHT corner, press Enter.",
                     &lrx, &lry)) return;

  minx = (ulx < lrx) ? ulx : lrx; maxx = (ulx > lrx) ? ulx : lrx;
  miny = (uly < lry) ? uly : lry; maxy = (uly > lry) ? uly : lry;
  if (cx < minx) cx = minx;
  if (cx > maxx) cx = maxx;
  if (cy < miny) cy = miny;
  if (cy > maxy) cy = maxy;

  /* Persist the captured calibration NOW -- the operator completed the three
   * deliberate capture steps, so the verify screen is review-only and a missed
   * or flaky save-confirm can no longer silently drop the calibration (the v2
   * g2k bug: JOY_CAL came back empty -> engine auto-cal -> stick dead). ESC on
   * the verify screen reverts to the previous value. */
  joyidx = scfg_index("JOY_CAL");
  if (joyidx >= 0)
  {
    /* JOY_CAL grammar (engine registry / SDL patch sscanf): six values
     * xmin,xcenter,xmax,ymin,ycenter,ymax. Persist NOW (capture-complete) so a
     * just-finished calibration can never be silently dropped. */
    snprintf(val, sizeof(val), "%d,%d,%d,%d,%d,%d", minx, cx, maxx, miny, cy, maxy);
    scfg_set(&g_cfg, joyidx, val); g_dirty = 1;
  }
  tui_clear();
  for (;;)
  {
    int x = 0, y = 0, b;
    unsigned btn = 0;
    char line[48];
    joyport_sample(&x, &y, &btn);
    tui_titlebar("CALIBRATE JOYSTICK");
    tui_box(16, by, 48, 9, "CALIBRATE JOYSTICK");
    snprintf(line, sizeof(line), "X  min %-6d  center %-6d  max %-6d", minx, cx, maxx);
    tui_at(18, by + 1, PAL->body, PAL->bg, line);
    snprintf(line, sizeof(line), "Y  min %-6d  center %-6d  max %-6d", miny, cy, maxy);
    tui_at(18, by + 2, PAL->body, PAL->bg, line);
    snprintf(line, sizeof(line), "Live   X = %-6d  Y = %-6d", x, y);
    tui_at(18, by + 4, PAL->value, PAL->bg, line);
    tui_at(18, by + 6, PAL->body, PAL->bg, "Buttons:");
    for (b = 0; b < BIND_NJBUT; ++b)
    {
      char tag[4];
      snprintf(tag, sizeof(tag), "%d", b + 1);
      tui_at(27 + b * 3, by + 6, (btn & (1u << b)) ? PAL->warn_fg : PAL->dim,
             PAL->bg, tag);
    }
    tui_box(TUI_DESC_X, TUI_DESC_TOP(4), TUI_DESC_W, 4, "DESCRIPTION");
    tui_wrap(TUI_DESC_TX, TUI_DESC_TOP(4) + 1, TUI_DESC_TW, 2, PAL->desc, PAL->bg,
             joyidx >= 0
               ? "Calibration saved. Move the stick to verify the range and test "
                 "the buttons. Enter or ESC keeps it; re-run to redo."
               : "Verify the range and test the buttons. Saved calibration "
                 "arrives with the engine update; ESC returns.");
    tui_status("Enter/ESC Keep");
    tui_present();

    if (tui_kbhit())
    {
      int k = tui_getkey();
      /* Both Enter and ESC KEEP the calibration -- it was already persisted on
       * capture-complete above. (Previously ESC reverted to the prior value,
       * which silently discarded a just-completed calibration when the operator
       * pressed ESC to leave -- the g2k "JOY_CAL came back empty" trap.) To
       * redo, re-run Calibrate joystick. */
      if (k == TUI_KEY_ESC || k == TUI_KEY_ENTER || k == TUI_KEY_SPACE) return;
    }
    else tui_delay_ms(40);
  }
}

/* ---- Restore default controls (plan 3.5) ---------------------------- */
static void controls_restore_defaults(void)
{
  if (tui_yesno("Restore default controls",
                "Reset all keyboard + joystick controls to the defaults?", 1))
  {
    binds_ensure();
    bindings_defaults(g_binds);
    bindings_save(g_binds, &g_cfg);
    g_dirty = 1;
  }
}

/* ---- Input submenu (T47 plan 3.5) ----------------------------------- *
 * The live USE_JOYSTICK on/off row + the Phase-3 control screens. The two
 * joystick rows grey out (navigation skips them) when the game port is Off. */
static void screen_input(void)
{
  enum { R_JOY = 0, R_KBD, R_JOYCFG, R_JOYCAL, R_RESTORE, R_BACK, R_N };
  static const char *labels[R_N] = {
    "Joystick / gamepad", "Configure keyboard", "Configure joystick",
    "Calibrate joystick", "Restore default controls", "Back"
  };
  static const char *helps[R_N] = {
    "Read a joystick or gamepad on the PC game port. Off = keyboard only.",
    "Remap the keyboard controls for each game action.",
    "Assign game-port buttons to actions (needs Joystick = On).",
    "Calibrate the game-port stick range (needs Joystick = On).",
    "Reset all keyboard + joystick controls to the defaults.",
    "Return to the main menu."
  };
  int joyidx = scfg_index("USE_JOYSTICK");
  int sel = R_JOY;

  binds_ensure();
  tui_clear();
  for (;;)
  {
    int i, k, active[R_N], joy_on = (strcmp(scfg_get(&g_cfg, joyidx), "1") == 0);
    int boxy = menu_box_top(3, TUI_DESC_TOP(4), R_N + 2);
    active[R_JOY] = 1; active[R_KBD] = 1;
    active[R_JOYCFG] = joy_on; active[R_JOYCAL] = joy_on; /* grey when game port Off */
    active[R_RESTORE] = 1; active[R_BACK] = 1;
    if (!active[sel]) sel = R_JOY; /* never rest on a row that just greyed out */
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
        tui_at(20 + 28, boxy + 1 + i, vfg, bg, joy_on ? "On" : "Off");
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
      if      (sel == R_JOY)     { if (pick_value(joyidx)) tui_clear(); }
      else if (sel == R_KBD)     { screen_keyboard(); tui_clear(); }
      else if (sel == R_JOYCFG)  { screen_joystick_cfg(); tui_clear(); }
      else if (sel == R_JOYCAL)  { screen_joystick_cal(); tui_clear(); }
      else if (sel == R_RESTORE) { controls_restore_defaults(); tui_clear(); }
      else if (sel == R_BACK)    return;
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

  /* System Speed: if no valid class is recorded yet -- a fresh config, OR a
   * DOSKUTSU.CFG written by Express / before SPEED_CLASS existed (it has no
   * SPEED_CLASS line) -- stamp the class the detected CPU maps to, so the main
   * menu shows a real class (Slow / Normal / Fast / Very Fast) instead of
   * "(not set)". Never overrides an explicit class. Session-only (no g_dirty):
   * it persists on the next save and re-derives identically, so just opening
   * SETUP does not flag a false "unsaved". */
  {
    const char *sc = scfg_get(&g_cfg, scfg_index("SPEED_CLASS"));
    if (speed_row_index(sc ? sc : "") < 0)
      scfg_set(&g_cfg, scfg_index("SPEED_CLASS"),
               SPEED_ROWS[speed_row_for_cpu(&g_prof)].cls);
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
    /* v2 (SETUP-UX-V2-PLAN sec.2): Auto-detect leads -- it is the recommended
     * FIRST action for a first-timer, and it used to sit buried at index 4.
     * iter #3 (#20): the "Save and run DOSKUTSU" item was DROPPED -- the single
     * "Save and exit" path is back, and SETUP has one exit code (0) again.
     * SETUP.BAT keeps its env-hygiene clears but no longer chains the game. */
    enum { M_AUTODET = 0, M_SOUND, M_SPEED, M_INPUT, M_ADVANCED,
           M_SAVEEXIT, M_QUIT };
    static const char *items[] = {
      "Auto-detect best settings",
      "Sound",
      "System speed",
      "Input",
      "Advanced",
      "Save and exit",
      "Quit without saving"
    };
    /* item 7: one short sentence per item (the operator's review-3 example
     * "Choose how music and sound effects play."). */
    static const char *helps[] = {
      "Auto-detect hardware and best settings for this system",
      "Setup sound card, music, sound effects and volume",
      "Preset performance options depending on system speed",
      "Keyboard, Joystick and gamepad configuration",
      "Performance and compatibility options",
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

      if (action == M_AUTODET)       screen_autodetect();
      else if (action == M_SOUND)    screen_sound_menu();
      else if (action == M_SPEED)    screen_speed();
      else if (action == M_INPUT)    screen_input();
      else if (action == M_ADVANCED) screen_advanced();
      else if (action == M_SAVEEXIT) { screen_save(); break; } /* Save and exit */
      else if (action == -2) { screen_save(); break; }  /* F10 = Save and Exit */
      else /* action == M_QUIT (Quit without saving) or ESC (-1) */
      {
        /* Session model: every screen's edits live in the in-memory config and
         * nothing is written until "Save and exit". The ONLY discard paths are
         * this branch. ESC with unsaved edits OFFERS to save first (so leaving
         * via ESC can't silently lose work); the explicit "Quit without saving"
         * menu item skips the offer -- the user already chose to discard. */
        if (action == -1 && g_dirty)
        {
          if (tui_yesno("Unsaved changes", "Save your changes before exiting?", 1))
          {
            screen_save(); /* Yes -> write the config, then exit */
            break;
          }
          /* No -> fall through to the explicit discard confirm below. */
        }
        /* T52 accident guard before leaving to DOS; default highlight No. */
        if (tui_yesno("Quit",
                      g_dirty ? "Discard unsaved changes and quit?" : "Quit SETUP?", 1))
          break;
      }
    }
  }
#undef MENU_X
#undef MENU_W
#undef MENU_Y

  tui_shutdown();

  /* iter #3 (#20): the "Save and run DOSKUTSU" exit-code chain was dropped, so
   * SETUP is back to a single exit path. SETUP.BAT keeps its env-hygiene clears
   * but no longer branches on an ERRORLEVEL, so a plain 0 is all it needs. */
  return 0;
}
