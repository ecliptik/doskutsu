/*
 * tui.c -- CP437 text UI implementation. See tui.h.
 * DJGPP direct-VRAM backend + a host stdio fallback. ASCII-only source.
 *
 * T15 rendering-correctness note (real-HW, operator 2026-06-07):
 *   On real VGA text mode the conio teletype path (cputs/putch) WRAPS at
 *   column 80 and SCROLLS the whole display when the bottom-right cell
 *   (col 80, row 25) is written. That scroll produced the stacked/"doubled"
 *   Main Menu + overflowing text. DOSBox masked it. The DOS backend now
 *   renders exclusively through ScreenPutChar() (a direct poke to the screen
 *   buffer): no cursor move, no wrap, no scroll. Every public write is
 *   CLIPPED to the 80x25 grid.
 *
 * T22 palette note:
 *   All colors go through a named-role palette_t (see tui.h). Two schemes --
 *   classic + Cave-Story -- selected at startup by DOSKUTSU_SETUP_PALETTE.
 *   The full-width title bar is toggled by DOSKUTSU_SETUP_TITLEBAR.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h> /* getenv */

#include "tui.h"

/* CP437 box-drawing glyphs as byte escapes (source stays 7-bit ASCII). */
#define BX_TL ((char)0xC9)
#define BX_TR ((char)0xBB)
#define BX_BL ((char)0xC8)
#define BX_BR ((char)0xBC)
#define BX_H  ((char)0xCD)
#define BX_V  ((char)0xBA)

#define SCR_W 80
#define SCR_H 25

/* ---- palette (T22) -------------------------------------------------- */

/* Classic blue/grey/yellow (the v1 look). */
static const palette_t PAL_CLASSIC = {
  /* bg     */ TUI_BLUE,
  /* body   */ TUI_GREY,
  /* title  */ TUI_YELLOW,
  /* border */ TUI_WHITE,
  /* sel_fg */ TUI_BLACK,
  /* sel_bg */ TUI_GREY,
  /* value  */ TUI_WHITE,
  /* desc   */ TUI_YELLOW,
  /* warn_fg*/ TUI_WHITE,
  /* warn_bg*/ TUI_RED,
  /* bar_fg */ TUI_BLACK,
  /* bar_bg */ TUI_GREY,
  /* dim    */ TUI_DKGREY
};

/* Cave-Story-inspired black/cyan (operator pick for review-2). */
static const palette_t PAL_CS = {
  /* bg     */ TUI_BLACK,
  /* body   */ TUI_WHITE,
  /* title  */ TUI_LTCYAN,
  /* border */ TUI_LTCYAN,
  /* sel_fg */ TUI_WHITE,
  /* sel_bg */ TUI_CYAN,   /* dark cyan */
  /* value  */ TUI_LTCYAN, /* == help key color */
  /* desc   */ TUI_GREY,
  /* warn_fg*/ TUI_LTRED,
  /* warn_bg*/ TUI_BLACK,
  /* bar_fg */ TUI_WHITE,
  /* bar_bg */ TUI_CYAN,   /* dark cyan bar */
  /* dim    */ TUI_DKGREY
};

static const palette_t *g_pal = &PAL_CS; /* default = Cave-Story */
static int g_titlebar = 1;               /* default = full-width title bar */

/* Pop-over differentiation style (T55 A/B): how a modal stands out from the
 * screen behind it. Selected by DOSKUTSU_SETUP_POPUP for the operator's
 * screenshot pick; default = drop shadow. */
#define POPUP_NONE   0
#define POPUP_SHADOW 1  /* classic DOS drop shadow (right + bottom) */
#define POPUP_DIM    2  /* dim the backdrop behind the modal        */
#define POPUP_FILL   3  /* fill the modal body with a distinct bg    */
static int g_popup_style = POPUP_DIM; /* operator pick (review-11) */

static void tui__load_env(void)
{
  const char *p = getenv("DOSKUTSU_SETUP_PALETTE");
  const char *t = getenv("DOSKUTSU_SETUP_TITLEBAR");
  const char *u = getenv("DOSKUTSU_SETUP_POPUP");
  if (p && (strcmp(p, "classic") == 0 || strcmp(p, "CLASSIC") == 0))
    g_pal = &PAL_CLASSIC;
  else
    g_pal = &PAL_CS;
  g_titlebar = !(t && t[0] == '0' && t[1] == '\0');
  if      (u && strcmp(u, "none")   == 0) g_popup_style = POPUP_NONE;
  else if (u && strcmp(u, "shadow") == 0) g_popup_style = POPUP_SHADOW;
  else if (u && strcmp(u, "fill")   == 0) g_popup_style = POPUP_FILL;
  else                                    g_popup_style = POPUP_DIM; /* default + "dim" */
}

const palette_t *tui_pal(void)       { return g_pal; }
int tui_titlebar_enabled(void)       { return g_titlebar; }

#if defined(__DJGPP__)

#include <conio.h> /* textmode / _setcursortype / getch */
#include <pc.h>    /* ScreenPutChar -- direct, scroll-free screen writes */

/* Pack fg/bg into a VGA text attribute byte. */
#define TUI_ATTR(fg, bg) ((((bg) & 0x0f) << 4) | ((fg) & 0x0f))

/* ---- shadow buffer / flicker-free present (T51) --------------------- *
 * All widget writes go to an off-screen SHADOW buffer, never straight to VRAM.
 * tui_present() then writes ONLY the cells that differ from what is currently
 * displayed (g_disp) to VRAM via ScreenPutChar. Because VRAM never sees a
 * full-screen clear-to-background pass (tui_clear now clears the shadow, in
 * memory), and each changed cell transitions old->new in a single write, there
 * is no visible black flash on real VGA when changing rows OR whole screens.
 * tui_getkey() presents automatically, so the existing render loops need no
 * change; the audio-test popup, which draws then blocks without a getkey, calls
 * tui_present() explicitly. */
typedef struct { unsigned char ch; unsigned char attr; } cell_t;
static cell_t g_shadow[SCR_H][SCR_W];
static cell_t g_disp[SCR_H][SCR_W];
static int    g_disp_valid = 0; /* 0 -> next present writes every cell */

/* Poke one cell at 0-based (cx,cy) into the SHADOW buffer. Out-of-grid writes
 * are dropped so a widget can never escape the 80x25 grid. */
static void vga_putc(int cx, int cy, int attr, char c)
{
  if (cx < 0 || cx >= SCR_W || cy < 0 || cy >= SCR_H) return;
  g_shadow[cy][cx].ch   = (unsigned char)c;
  g_shadow[cy][cx].attr = (unsigned char)attr;
}

/* Flush the shadow buffer to VRAM, writing only changed cells (diff vs g_disp).
 * Idempotent + cheap: a no-change frame writes nothing. */
void tui_present(void)
{
  int x, y;
  for (y = 0; y < SCR_H; ++y)
    for (x = 0; x < SCR_W; ++x)
    {
      cell_t s = g_shadow[y][x];
      if (!g_disp_valid || s.ch != g_disp[y][x].ch || s.attr != g_disp[y][x].attr)
      {
        ScreenPutChar(s.ch, s.attr, x, y);
        g_disp[y][x] = s;
      }
    }
  g_disp_valid = 1;
}

/* T55: make a modal popup stand out from the screen behind it, per the selected
 * style. Operates on the shadow buffer AFTER the box + its content are drawn,
 * each frame, recoloring cells (glyphs preserved). x,y,w,h is the 1-based box
 * rect. POPUP_NONE is a no-op. */
void tui_popup_decorate(int x, int y, int w, int h)
{
  int cx0 = x - 1, cy0 = y - 1, cx1 = x + w - 2, cy1 = y + h - 2; /* 0-based */
  int dim = TUI_ATTR(TUI_DKGREY, TUI_BLACK);
  int gx, gy;

  if (g_popup_style == POPUP_SHADOW)
  {
    /* dark cells one column right + one row below the box (classic shadow). */
    for (gy = cy0 + 1; gy <= cy1 + 1; ++gy)
      if (cx1 + 1 < SCR_W && gy >= 0 && gy < SCR_H)
        g_shadow[gy][cx1 + 1].attr = (unsigned char)dim;
    for (gx = cx0 + 1; gx <= cx1 + 1; ++gx)
      if (cy1 + 1 < SCR_H && gx >= 0 && gx < SCR_W)
        g_shadow[cy1 + 1][gx].attr = (unsigned char)dim;
  }
  else if (g_popup_style == POPUP_DIM)
  {
    /* dim the body backdrop outside the box; leave the top title bar (row 0)
     * and bottom status bar (row SCR_H-1) readable. */
    for (gy = 1; gy < SCR_H - 1; ++gy)
      for (gx = 0; gx < SCR_W; ++gx)
        if (gx < cx0 || gx > cx1 || gy < cy0 || gy > cy1)
          g_shadow[gy][gx].attr = (unsigned char)dim;
  }
  else if (g_popup_style == POPUP_FILL)
  {
    /* recolor the whole box rect's background to blue (keep fg + glyph). */
    for (gy = cy0; gy <= cy1; ++gy)
      for (gx = cx0; gx <= cx1; ++gx)
        if (gx >= 0 && gx < SCR_W && gy >= 0 && gy < SCR_H)
        {
          unsigned char a = g_shadow[gy][gx].attr;
          g_shadow[gy][gx].attr = (unsigned char)((a & 0x0f) | (TUI_BLUE << 4));
        }
  }
}

/* Write s from 1-based (x,y), clipping at the screen right edge AND at the
 * 1-based inclusive column `maxcol`. Never wraps, never scrolls. */
static void vga_puts(int x, int y, int attr, const char *s, int maxcol)
{
  int cx = x - 1, cy = y - 1;
  if (maxcol > SCR_W) maxcol = SCR_W;
  for (; *s; ++s)
  {
    if (cx + 1 > maxcol) break; /* cx+1 is the 1-based column */
    vga_putc(cx, cy, attr, *s);
    ++cx;
  }
}

void tui_init(void)
{
  tui__load_env();
  textmode(C80);
  _setcursortype(_NOCURSOR);
  g_disp_valid = 0;  /* force the first present to write every cell */
  tui_clear();       /* clears the shadow */
  tui_present();     /* paint the initial blank screen to VRAM */
}

void tui_shutdown(void)
{
  _setcursortype(_NORMALCURSOR);
  textattr(0x07);
  clrscr();
}

void tui_clear(void)
{
  int attr = TUI_ATTR(g_pal->body, g_pal->bg);
  int x, y;
  for (y = 0; y < SCR_H; ++y)
    for (x = 0; x < SCR_W; ++x)
      vga_putc(x, y, attr, ' ');
}

void tui_at(int x, int y, int fg, int bg, const char *s)
{
  vga_puts(x, y, TUI_ATTR(fg, bg), s, SCR_W);
}

void tui_box(int x, int y, int w, int h, const char *title)
{
  int attr = TUI_ATTR(g_pal->border, g_pal->bg);
  int cx = x - 1, cy = y - 1; /* 0-based top-left */
  int i, j;

  if (w < 2 || h < 2) return;

  vga_putc(cx, cy, attr, BX_TL);
  for (i = 1; i < w - 1; ++i) vga_putc(cx + i, cy, attr, BX_H);
  vga_putc(cx + w - 1, cy, attr, BX_TR);

  for (i = 1; i < h - 1; ++i)
  {
    vga_putc(cx, cy + i, attr, BX_V);
    for (j = 1; j < w - 1; ++j) vga_putc(cx + j, cy + i, attr, ' ');
    vga_putc(cx + w - 1, cy + i, attr, BX_V);
  }

  vga_putc(cx, cy + h - 1, attr, BX_BL);
  for (i = 1; i < w - 1; ++i) vga_putc(cx + i, cy + h - 1, attr, BX_H);
  vga_putc(cx + w - 1, cy + h - 1, attr, BX_BR);

  if (title && *title)
  {
    int tattr = TUI_ATTR(g_pal->title, g_pal->bg);
    int inner = w - 2;
    int avail = inner - 2;
    int tl = (int)strlen(title);
    int tcx, k;
    if (avail < 0) avail = 0;
    if (tl > avail) tl = avail;
    tcx = cx + 1 + (inner - (tl + 2)) / 2;
    if (tcx < cx + 1) tcx = cx + 1;
    vga_putc(tcx, cy, tattr, ' ');
    for (k = 0; k < tl; ++k) vga_putc(tcx + 1 + k, cy, tattr, title[k]);
    vga_putc(tcx + 1 + tl, cy, tattr, ' ');
  }
}

void tui_status(const char *s)
{
  int attr = TUI_ATTR(g_pal->bar_fg, g_pal->bar_bg);
  int x;
  /* Fill the whole bottom row -- safe to write col 80 here because
   * ScreenPutChar pokes VRAM directly and does not scroll. */
  for (x = 0; x < SCR_W; ++x) vga_putc(x, SCR_H - 1, attr, ' ');
  vga_puts(2, SCR_H, attr, s, SCR_W);
}

int tui_getkey(void)
{
  int c;
  tui_present(); /* T51: flush the frame's draws to VRAM (diff-only) before blocking */
  c = getch();
  if (c == 0 || c == 0xE0)
  {
    int e = getch();
    switch (e)
    {
      case 72: return TUI_KEY_UP;
      case 80: return TUI_KEY_DOWN;
      case 75: return TUI_KEY_LEFT;
      case 77: return TUI_KEY_RIGHT;
      case 68: return TUI_KEY_F10;  /* extended scan 0x44 == F10  */
      case 71: return TUI_KEY_HOME; /* extended scan 0x47 == Home */
      default: return 0;
    }
  }
  return c;
}

#else /* host stdio fallback so main.c builds/runs off-target */

void tui_init(void)     { tui__load_env(); }
void tui_shutdown(void) { }
void tui_present(void)  { } /* host: writes are immediate (printf), nothing to flush */
void tui_popup_decorate(int x, int y, int w, int h)
{ (void)x; (void)y; (void)w; (void)h; } /* host: no VGA attrs */
void tui_clear(void)    { printf("\n----------------------------------------\n"); }

void tui_at(int x, int y, int fg, int bg, const char *s)
{ (void)x; (void)y; (void)fg; (void)bg; printf("%s\n", s); }

void tui_box(int x, int y, int w, int h, const char *title)
{ (void)x; (void)y; (void)w; (void)h; if (title) printf("== %s ==\n", title); }

void tui_status(const char *s) { printf("[%s]\n", s); }

int tui_getkey(void)
{
  int c = getchar();
  if (c == '\n') return TUI_KEY_ENTER;
  if (c == 'q' || c == 'Q') return TUI_KEY_ESC;
  if (c == 'j') return TUI_KEY_DOWN;
  if (c == 'k') return TUI_KEY_UP;
  if (c == 'S') return TUI_KEY_F10;  /* host stand-in for F10 (flow tests) */
  if (c == 'g') return TUI_KEY_HOME; /* host stand-in for Home             */
  return c;                          /* ' ' returns 0x20 == TUI_KEY_SPACE  */
}

#endif /* __DJGPP__ */

/* ---- portable widgets built on the primitives above ----------------- */

void tui_center(int y, int fg, int bg, const char *s)
{
  int len = (int)strlen(s);
  int x = (SCR_W - len) / 2 + 1;
  if (x < 1) x = 1;
  tui_at(x, y, fg, bg, s);
}

void tui_kv(int x, int y, int keyw, int kc, int vc, int bg,
            const char *key, const char *value)
{
  tui_at(x, y, kc, bg, key);
  tui_at(x + keyw, y, vc, bg, value);
}

/* CP437 shade glyphs for the progress bar (byte escapes -> ASCII source). */
#define BAR_FULL  ((char)0xDB) /* solid block: filled portion */
#define BAR_TRACK ((char)0xB0) /* light shade: empty track    */

void tui_progress(int x, int y, int w, int permille, int fillfg, int trackfg,
                  int bg)
{
  char buf[SCR_W + 1];
  int filled, i;

  if (w < 1) return;
  if (w > SCR_W) w = SCR_W;
  if (permille < 0)    permille = 0;
  if (permille > 1000) permille = 1000;
  filled = (w * permille) / 1000;
  if (filled > w) filled = w;

  if (filled > 0)
  {
    for (i = 0; i < filled; ++i) buf[i] = BAR_FULL;
    buf[filled] = '\0';
    tui_at(x, y, fillfg, bg, buf);
  }
  if (w - filled > 0)
  {
    for (i = 0; i < w - filled; ++i) buf[i] = BAR_TRACK;
    buf[w - filled] = '\0';
    tui_at(x + filled, y, trackfg, bg, buf);
  }
}

void tui_titlebar(const char *s)
{
  const palette_t *p = g_pal;
  if (g_titlebar)
  {
    char bar[SCR_W + 1];
    int i, len, x;
    for (i = 0; i < SCR_W; ++i) bar[i] = ' ';
    bar[SCR_W] = '\0';
    tui_at(1, 1, p->bar_fg, p->bar_bg, bar);
    len = (int)strlen(s);
    x = (SCR_W - len) / 2 + 1;
    if (x < 1) x = 1;
    tui_at(x, 1, p->bar_fg, p->bar_bg, s);
  }
  else
  {
    tui_center(1, p->title, p->bg, s);
  }
}

/* Count the rows tui_wrap would draw for text at interior width w (>=1). */
static int tui_wrap_count(const char *text, int w)
{
  const char *p = text;
  int lines = 0;
  if (w < 1) w = 1;
  while (*p)
  {
    int len = 0, lastsp = -1, take;
    while (*p == ' ') ++p;
    if (!*p) break;
    while (p[len] && p[len] != '\n' && len < w)
    {
      if (p[len] == ' ') lastsp = len;
      ++len;
    }
    if (p[len] == '\0' || p[len] == '\n') take = len;
    else if (lastsp > 0)                  take = lastsp;
    else                                  take = w;
    ++lines;
    p += take;
    if (*p == '\n') ++p;
  }
  return lines;
}

int tui_wrap(int x, int y, int w, int maxlines, int fg, int bg,
             const char *text)
{
  char line[SCR_W + 1];
  const char *p = text;
  int lines = 0;
  if (w < 1) w = 1;
  if (w > SCR_W) w = SCR_W;
  while (*p && lines < maxlines)
  {
    int len = 0, lastsp = -1, take;
    while (*p == ' ') ++p;
    if (!*p) break;
    while (p[len] && p[len] != '\n' && len < w)
    {
      if (p[len] == ' ') lastsp = len;
      ++len;
    }
    if (p[len] == '\0' || p[len] == '\n') take = len;
    else if (lastsp > 0)                  take = lastsp;
    else                                  take = w;
    memcpy(line, p, (size_t)take);
    line[take] = '\0';
    tui_at(x, y + lines, fg, bg, line);
    ++lines;
    p += take;
    if (*p == '\n') ++p;
  }
  return lines;
}

int tui_menu(int x, int y, int w, const char *title,
             const char *const *items, const char *const *helps,
             const char *status, int n, int start_sel)
{
  int sel = (start_sel >= 0 && start_sel < n) ? start_sel : 0;
  int h = n + 2;

  for (;;)
  {
    int i, k;
    tui_box(x, y, w, h, title);
    for (i = 0; i < n; ++i)
    {
      char row[80];
      int  fg = (i == sel) ? g_pal->sel_fg : g_pal->body;
      int  bg = (i == sel) ? g_pal->sel_bg : g_pal->bg;
      snprintf(row, sizeof(row), " %-*.*s", w - 3, w - 3, items[i]);
      tui_at(x + 1, y + 1 + i, fg, bg, row);
    }
    if (helps)
    {
      int hby = y + h + 1; /* one row below the menu box */
      tui_box(8, hby, 64, 5, "HELP");
      tui_wrap(10, hby + 1, 60, 3, g_pal->desc, g_pal->bg,
               helps[sel] ? helps[sel] : "");
    }
    tui_status(status ? status : "Enter Select   ESC Back");

    k = tui_getkey();
    if (k == TUI_KEY_UP)        sel = (sel + n - 1) % n;
    else if (k == TUI_KEY_DOWN) sel = (sel + 1) % n;
    else if (k == TUI_KEY_HOME)  sel = 0;
    else if (k == TUI_KEY_ENTER) return sel;
    else if (k == TUI_KEY_F10)   return -2;
    else if (k == TUI_KEY_ESC)   return -1;
  }
}

void tui_message(const char *title, const char *const *lines, int n)
{
  int i, iw = (int)strlen(title);
  int x, y, w, rows = 0, ry;

  for (i = 0; i < n; ++i)
  {
    int l = (int)strlen(lines[i]);
    if (l > iw) iw = l;
  }
  if (iw > SCR_W - 6) iw = SCR_W - 6;
  if (iw < 1) iw = 1;

  for (i = 0; i < n; ++i)
  {
    int c = tui_wrap_count(lines[i], iw);
    rows += (c > 0) ? c : 1;
  }

  w = iw + 4;
  x = (SCR_W - w) / 2 + 1;
  y = (SCR_H - (rows + 4)) / 2 + 1;
  if (y < 1) y = 1;

  tui_box(x, y, w, rows + 4, title);
  ry = y + 1;
  for (i = 0; i < n; ++i)
  {
    int c = tui_wrap(x + 2, ry, iw, rows, g_pal->body, g_pal->bg, lines[i]);
    ry += (c > 0) ? c : 1;
  }
  tui_at(x + 2, y + rows + 2, g_pal->title, g_pal->bg, "Press a key...");
  tui_popup_decorate(x, y, w, rows + 4); /* T55: differentiate from the backdrop */
  (void)tui_getkey();
}

/* The single standard Yes/No prompt (T52): a centered modal box with a wrapped
 * question and a VERTICAL Yes/No menu (Up/Down + Enter, Y / N shortcut keys) --
 * the same widget + key scheme as the audio-test answer. default_no picks the
 * initial highlight (1 = No for destructive prompts; 0 = Yes). Returns 1 = Yes,
 * 0 = No; ESC resolves to the default (the safe choice). Drawn over the current
 * screen; the caller repaints afterward. */
int tui_yesno(const char *title, const char *question, int default_no)
{
  static const char *opts[2] = { "Yes", "No" };
  int qlen = (int)strlen(question);
  int w = qlen + 6;
  int x, y, h, qrows, ans, yrow;

  if (w < 28) w = 28;
  if (w > SCR_W - 2) w = SCR_W - 2;
  qrows = tui_wrap_count(question, w - 4);
  if (qrows < 1) qrows = 1;
  h = qrows + 5; /* border + question rows + gap + Yes + No + border */
  x = (SCR_W - w) / 2 + 1;
  y = (SCR_H - h) / 2 + 1;
  if (y < 1) y = 1;
  ans = default_no ? 1 : 0;
  yrow = y + 1 + qrows + 1; /* first option row (one blank row after the text) */

  for (;;)
  {
    int o, k;
    tui_box(x, y, w, h, title);
    tui_wrap(x + 2, y + 1, w - 4, qrows, g_pal->body, g_pal->bg, question);
    for (o = 0; o < 2; ++o)
    {
      int sr = (o == ans);
      int fg = sr ? g_pal->sel_fg : g_pal->body;
      int bg = sr ? g_pal->sel_bg : g_pal->bg;
      char rowb[84];
      snprintf(rowb, sizeof(rowb), " %-*.*s", w - 3, w - 3, opts[o]);
      tui_at(x + 1, yrow + o, fg, bg, rowb);
    }
    tui_popup_decorate(x, y, w, h); /* T55: differentiate from the backdrop */
    /* T55 item 2: Y/N still work but are no longer advertised. */
    tui_status("Up/Down Select   Enter Confirm");
    k = tui_getkey();
    if (k == TUI_KEY_UP || k == TUI_KEY_DOWN ||
        k == TUI_KEY_LEFT || k == TUI_KEY_RIGHT) ans ^= 1;
    else if (k == 'y' || k == 'Y') return 1;
    else if (k == 'n' || k == 'N') return 0;
    else if (k == TUI_KEY_ENTER)   return ans == 0 ? 1 : 0;
    else if (k == TUI_KEY_ESC)     return default_no ? 0 : 1; /* ESC = default */
  }
}

/* Back-compat shim: the old single-question confirm, now the standard widget
 * with a destructive default (No). */
int tui_confirm(const char *question)
{
  return tui_yesno("Confirm", question, 1);
}
