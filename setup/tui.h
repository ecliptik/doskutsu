#ifndef SETUP_TUI_H
#define SETUP_TUI_H

/*
 * tui.h -- minimal CP437 full-screen text UI for SETUP.EXE.
 *
 * Built on DJGPP conio in 80x25 text mode; a small host fallback (stdio)
 * keeps main.c buildable/runnable off-target for flow testing. CP437
 * box-drawing glyphs are emitted as byte escapes so the SOURCE stays 7-bit
 * ASCII (repo rule). ASCII-only source.
 */

/* Key codes returned by tui_getkey(). Printable keys return their ASCII. */
#define TUI_KEY_UP     0x1000
#define TUI_KEY_DOWN   0x1001
#define TUI_KEY_LEFT   0x1002
#define TUI_KEY_RIGHT  0x1003
#define TUI_KEY_ENTER  0x000d
#define TUI_KEY_ESC    0x001b
#define TUI_KEY_SPACE  0x0020 /* ASCII space; also "change setting" (T17) */
#define TUI_KEY_F10    0x1004 /* save settings + return to menu (T17)     */
#define TUI_KEY_HOME   0x1005 /* jump to the first selectable row (T17)   */

/* Colors (subset; map to conio / VGA text-attribute values 0-15). */
#define TUI_BLACK    0
#define TUI_BLUE     1
#define TUI_GREEN    2
#define TUI_CYAN     3  /* dark cyan */
#define TUI_RED      4
#define TUI_GREY     7  /* light grey */
#define TUI_DKGREY   8
#define TUI_LTCYAN  11
#define TUI_LTRED   12
#define TUI_YELLOW  14
#define TUI_WHITE   15

/* ---- palette (T22) -------------------------------------------------- *
 * All screen colors go through named ROLES, not scattered TUI_* literals,
 * so the whole look swaps in one place. Two schemes ship -- the classic
 * blue/grey/yellow and a Cave-Story-inspired black/cyan -- selectable at
 * startup via DOSKUTSU_SETUP_PALETTE (classic|cs; default cs). The
 * help-key-color-matches-row-value requirement falls out for free: both use
 * the single `value` role. */
typedef struct
{
  int bg;       /* screen background                                  */
  int body;     /* normal body / label text                          */
  int title;    /* screen + box titles                                */
  int border;   /* box borders                                        */
  int sel_fg;   /* selection bar foreground                           */
  int sel_bg;   /* selection bar background                           */
  int value;    /* a setting's value AND its matching help key        */
  int desc;     /* help description text                              */
  int warn_fg;  /* warning / notice foreground                        */
  int warn_bg;  /* warning / notice background                        */
  int bar_fg;   /* top title bar + bottom status bar foreground       */
  int bar_bg;   /* top title bar + bottom status bar background       */
  int dim;      /* greyed / disabled (non-selectable) rows            */
} palette_t;

/* The active palette (never NULL). Use the PAL macro for brevity. */
const palette_t *tui_pal(void);
#define PAL (tui_pal())

/* Shared geometry for the bottom Help / Description / NOTE box (operator
 * requirement T47): every screen draws its bottom info box at this identical
 * width + x-position, so the bottom panel never changes size or jumps left/
 * right as you move between screens. Width 72 at x=5 matches the main-menu
 * SYSTEM PROFILE panel; box interior text wraps at x=TUI_DESC_TX width
 * TUI_DESC_TW (1-cell margins inside the 72-wide box). The y-position and the
 * box HEIGHT stay per-screen (content-driven); only the width + x are fixed. */
#define TUI_DESC_X   5   /* 1-based left column of the box      */
#define TUI_DESC_W   72  /* box width (cols 5..76, 2-col margins) */
#define TUI_DESC_TX  7   /* interior text x (x + 2)             */
#define TUI_DESC_TW  68  /* interior text wrap width (W - 4)    */
/* R-J: the bottom info box (Description/Help) is BOTTOM-ANCHORED on every screen
 * so it never floats -- its bottom border sits at row TUI_DESC_BOTTOM (just
 * above the status bar at row 25). A box of height h is drawn at y =
 * TUI_DESC_TOP(h); its wrapped text starts one row below. */
#define TUI_DESC_BOTTOM 23
#define TUI_DESC_TOP(h) (TUI_DESC_BOTTOM - (h) + 1)

/* 1 if the full-width title BAR variant is active (DOSKUTSU_SETUP_TITLEBAR,
 * default on); 0 = plain centered title text (T22 A/B). */
int tui_titlebar_enabled(void);

/* Draw the page title on row 1: a full-width palette bar with the centered
 * ALL-CAPS title when the bar variant is on, else plain centered title text. */
void tui_titlebar(const char *s);

void tui_init(void);
void tui_shutdown(void);
void tui_clear(void);

/* Flush the current frame's writes to the screen (T51 flicker-free present).
 * All tui_* draws go to an off-screen shadow buffer; this writes only the cells
 * that changed since the last present to VRAM, so there is no full-screen clear
 * flash. tui_getkey() calls it automatically -- only code that draws and then
 * blocks WITHOUT a getkey (the audio-test popup) needs to call it explicitly. */
void tui_present(void);

/* T55: decorate a modal popup so it stands out from the screen behind it,
 * per DOSKUTSU_SETUP_POPUP (none|shadow|dim|fill; default shadow). Call it after
 * drawing the popup box + content, each frame; x,y,w,h is the 1-based box rect.
 * No-op for POPUP_NONE / the host build. */
void tui_popup_decorate(int x, int y, int w, int h);

/* Draw a single-line CP437 box at (x,y) of size w x h with an optional
 * centred title in the top edge. 1-based coordinates (conio convention). */
void tui_box(int x, int y, int w, int h, const char *title);

/* Write s at (x,y) with the given foreground/background. The write is
 * CLIPPED to the screen width -- it never emits past column 80, so it can
 * never wrap to the next line or scroll the display. 1-based coordinates. */
void tui_at(int x, int y, int fg, int bg, const char *s);

/* Write s horizontally centered on row y (1-based), clipped to the screen. */
void tui_center(int y, int fg, int bg, const char *s);

/* Render a labeled line "<key><pad><value>" at (x,y): key in color kc, value
 * in color vc, both on background bg. The key is padded to keyw columns so a
 * column of values lines up. Each part is clipped (T15). Used for per-line
 * help lists and detected-finding lists (T22). */
void tui_kv(int x, int y, int keyw, int kc, int vc, int bg,
            const char *key, const char *value);

/* Word-wrap text into a rectangle: up to maxlines rows, each at most w
 * columns wide, drawn from 1-based (x,y) downward. Breaks on spaces; a word
 * longer than w is hard-split; an embedded '\n' forces a line break. Every
 * row is clipped to the screen (T15: no overflow, no scroll). Returns the
 * number of rows actually drawn. Use it for help / notice / message text so
 * long strings stay inside their box instead of running off the edge. */
int tui_wrap(int x, int y, int w, int maxlines, int fg, int bg,
             const char *text);

/* Draw a horizontal progress bar of inner width w at 1-based (x,y):
 * permille/1000 of the w cells are filled with the solid block glyph in
 * `fillfg`, the remainder shown as a light-shade track in `trackfg`, all on
 * `bg`. permille is clamped to [0,1000]. The write is clipped to the screen
 * (T15: no overflow, no scroll). Used by the audio-test popup (T24). */
void tui_progress(int x, int y, int w, int permille, int fillfg, int trackfg,
                  int bg);

/* Bottom status / help line. */
void tui_status(const char *s);

/* Blocking key read; returns a TUI_KEY_* code or a printable ASCII byte. */
int tui_getkey(void);

/* Vertical menu of n items inside a box at (x,y,w). Arrow keys move, Enter
 * selects, ESC cancels. start_sel is the initially highlighted row.
 * Returns the chosen index, or -1 on ESC. If helps is non-NULL it is a
 * parallel array of per-item help strings; a Help box is drawn below the
 * menu showing helps[sel], word-wrapped + clipped (T17). Pass NULL for no
 * help box. status sets the bottom status-bar text (NULL = a default).
 * Returns the chosen index, -1 on ESC, or -2 on F10 (T22). */
int tui_menu(int x, int y, int w, const char *title,
             const char *const *items, const char *const *helps,
             const char *status, int n, int start_sel);

/* Modal message box; waits for a keypress. */
void tui_message(const char *title, const char *const *lines, int n);

/* The standard Yes/No prompt (T52): a centered modal with a wrapped question
 * and a vertical Yes/No menu (Up/Down + Enter, Y / N shortcuts). default_no
 * sets the initial highlight (1 = No, for destructive prompts; 0 = Yes).
 * Returns 1 = Yes, 0 = No; ESC resolves to the default. */
int tui_yesno(const char *title, const char *question, int default_no);

/* Yes/No confirm; returns 1 for yes. Back-compat shim over tui_yesno (No
 * default). New code should call tui_yesno directly. */
int tui_confirm(const char *question);

/* tui_picklist return sentinel: the user picked the optional "Other..." row and
 * entered a free-form value (copied into other_buf). Distinct from any valid
 * item index (>= 0) and from the ESC/cancel result (-1). */
#define TUI_PICK_OTHER 0x2000

/* DF-style modal pick-list popup (the one new Phase-1 primitive; per
 * SETUP-DF-UX-PLAN Part 3). Draws a bordered list of n labeled items and
 * blocks until the user chooses one or cancels. Self-documenting: every legal
 * value is visible at once, instead of Left/Right cycling one value into view.
 *
 *   title       box title (NULL/"" -> untitled).
 *   ax, ay      1-based top-left anchor for the list box; pass <= 0 for either
 *               to CENTER on that axis (both <= 0 -> fully centered). When an
 *               anchor would push the box off-screen it is clamped inward.
 *   items       n item labels (required).
 *   tags        optional parallel array; tags[i], when non-NULL and non-empty,
 *               renders right-aligned on the item row in the value role (e.g.
 *               "(detected)" / "(default)" / "(current)"). Pass NULL for none.
 *   descs       optional parallel array; when non-NULL a DESCRIPTION box under
 *               the list shows descs[sel] (wrapped) -- the DF "help bar". The
 *               Other row, when present, uses otherprompt for its description.
 *   n           item count.
 *   start_sel   initially highlighted item [0,n); clamped.
 *   allow_other when non-zero an extra "Other..." row is appended; selecting it
 *               opens a free-entry field. The typed text (trimmed) is copied
 *               into other_buf (other_cap bytes) and the call returns
 *               TUI_PICK_OTHER. tui_picklist does NOT validate the value -- the
 *               caller validates and may re-open on a bad entry.
 *   otherprompt prompt shown in the free-entry box (NULL -> a default).
 *   other_buf,
 *   other_cap   destination for the Other value (may be NULL/0 when
 *               allow_other == 0).
 *
 * Keys: Up/Down move (wrap), Home -> first row, Enter selects, ESC cancels.
 * The popup is drawn over the current screen and decorated (T55); the caller
 * repaints afterward. Every write is clipped to 80x25 (no scroll).
 *
 * Returns the chosen item index [0,n), or TUI_PICK_OTHER (Other entered), or
 * -1 on ESC/cancel. */
int tui_picklist(const char *title, int ax, int ay,
                 const char *const *items, const char *const *tags,
                 const char *const *descs, int n, int start_sel,
                 int allow_other, const char *otherprompt,
                 char *other_buf, int other_cap);

#endif /* SETUP_TUI_H */
