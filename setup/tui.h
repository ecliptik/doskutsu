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

#endif /* SETUP_TUI_H */
