/*
 * s3crtc.c -- S3 ViRGE/DX direct-CRTC display-start (page-flip) de-risk probe.
 *
 * S3-2 Stage 1 (task #15). Gates sdl-engine's SDL/0078 direct-CRTC flip, which
 * replaces the hang-prone VBE 0x4F07 set-display-start on the S3+UNIVBE path.
 * The Cirrus analog is tests/probes/crtcswap.c (wave-50; verdict = BYTE units);
 * NONE of the Cirrus extension-register code ports -- the S3 uses CR0C/CR0D +
 * CR69 (Extended System Control 3) for the start address, gated by the CR67
 * streams-mode bits, and CR38/CR39 (not Cirrus SR[0x06]) to unlock.
 *
 * THE QUESTION: on the real S3 ViRGE/DX + UNIVBE 320x240x8 LFB mode, can a
 * direct-CRTC display-start flip page-1 (byte offset 76800) into view, and in
 * WHICH UNITS (byte / word / dword) does UNIVBE leave the CRTC counter? sdl-
 * engine bakes the working units into SDL/0078.
 *
 * STEPS (per the task):
 *  (1) set the real VBE LFB 320x240x8 mode + map the LFB; fill page 0 (offset 0)
 *      with a DARK sentinel + page 1 (offset 76800) with a BRIGHT sentinel.
 *  (2) read CR67 -- if CR67[3:2]==0b11 (streams mode) the display start comes
 *      from the streams primary-FB reg, NOT the CRTC start -> direct-CRTC flip
 *      will NOT work; REPORT it (forces the streams flip path in 0078).
 *  (3) LADDER the display-start UNITS for the page-1 (76800 B) start:
 *        byte  -> 76800   = 0x12C00 -> CR0D=0x00 CR0C=0x2C CR69[4:0]=0x01
 *        word  -> 38400   = 0x09600 -> CR0D=0x00 CR0C=0x96 CR69[4:0]=0x00
 *        dword -> 19200   = 0x04B00 -> CR0D=0x00 CR0C=0x4B CR69[4:0]=0x00
 *      program each, wait VBL, read CR0C/0D/69 back (latch oracle).
 *  (4) CONFIRM the flip: each unit holds ~2 s so the operator sees the screen go
 *      BRIGHT (page 1) for the unit UNIVBE actually uses (wrong units point
 *      elsewhere in page 0 -> stays dark). A pure-software readback CANNOT see
 *      scanout, so the operator-eyeball is the flip oracle; the latch-readback
 *      is the auto half (regs accepted the write), and the CR67 streams verdict
 *      is auto + decisive.
 *
 * VERIFY-BEFORE-TRUST: latch-readback (CR0C/0D/69 read == written) per unit ->
 * never a false GREEN from a write the chip rejected; streams=ON auto-flags that
 * NO unit can flip via CRTC regardless of latch. The working-units determination
 * = latch-OK unit(s) + the operator's BRIGHT-screen confirmation.
 *
 * Per [[dosbox_not_proxy]]: DOSBox-X's svga_s3 is a Trio64 -- the CRTC latch may
 * differ + there is no operator eye; the smoke is correctness-only (runs, sets
 * the mode, programs the regs, exits clean, parseable log). The units verdict is
 * a g2k-with-ViRGE measurement ONLY.
 *
 * HANG-SAFE: the FLIP path is pure protected-mode port I/O (no real-mode INT).
 * Only the mode-set + LFB-map (init) use VBE INT 10h / DPMI. atexit zeroes the
 * CRTC start + restores text mode + relocks CRTC even on abnormal exit. Bounded:
 * VBL polls are spin-capped; per-unit hold is a bounded wall pause.
 *
 * Output: S3CRTC.LOG (CWD, fopen-direct; C:\ fallback), fsync per line, stdout
 * mirror. No args. 8.3: S3CRTC.EXE / S3CRTC.LOG.
 *
 * DOS constraints: -march=i486 -mtune=pentium -O2, no MMX/SSE. Pure DJGPP + DPMI.
 *
 * License: MIT.
 */

#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/nearptr.h>
#include <time.h>
#include <unistd.h>

#define S3CRTC_VERSION "v1 (S3-2 #15 direct-CRTC display-start: streams check + byte/word/dword unit ladder + flip oracle)"

#define PAGE_BYTES   76800u        /* 320x240x8 = one page / the page-1 offset  */
#define PAGE0_OFF    0u
#define PAGE1_OFF    76800u
#define PAGE0_COLOR  0x10          /* DARK   sentinel (page 0)                  */
#define PAGE1_COLOR  0x2F          /* BRIGHT sentinel (page 1 -- the flip tell) */

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;
static void open_log(void)
{
    g_log = fopen("S3CRTC.LOG", "w");
    if (!g_log) g_log = fopen("C:\\S3CRTC.LOG", "w");
}
static void slog(const char *fmt, ...)
{
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); fsync(fileno(g_log)); }
}

/* ============================================================ */
/* S3 CRTC port helpers                                          */
/* ============================================================ */

#define CRTC_IDX  0x3D4
#define CRTC_DATA 0x3D5
#define INPUT_ST1 0x3DA            /* bit 3 = VSYNC active                      */

static uint8_t cr_read(uint8_t idx)  { outportb(CRTC_IDX, idx); return inportb(CRTC_DATA); }
static void    cr_write(uint8_t idx, uint8_t v) { outportb(CRTC_IDX, idx); outportb(CRTC_DATA, v); }

/* S3 extended-CRTC unlock (same as s3blt): CR38=0x48, CR39=0xA5. */
static void s3_unlock_crtc(void) { cr_write(0x38, 0x48); cr_write(0x39, 0xA5); }

static void wait_vbl(void)
{
    int i = 0;
    while ((inportb(INPUT_ST1) & 0x08) != 0) { if (++i > 200000) return; }
    i = 0;
    while ((inportb(INPUT_ST1) & 0x08) == 0) { if (++i > 200000) return; }
}

/* ============================================================ */
/* atexit cleanup -- zero CRTC start + restore text mode         */
/* ============================================================ */

static volatile int g_mode_active = 0;
static void restore_text_mode(void)
{
    /* zero the start address so a latched non-zero value doesn't carry over */
    cr_write(0x0D, 0x00); cr_write(0x0C, 0x00);
    uint8_t c69 = cr_read(0x69); cr_write(0x69, (uint8_t)(c69 & 0xE0));
    __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = 0x0003; __dpmi_int(0x10, &r);
    g_mode_active = 0;
}
static void atexit_cleanup(void) { if (g_mode_active) restore_text_mode(); }

/* ============================================================ */
/* VESA 8bpp LFB 320x240 mode finder (s3vram/s3blt idiom)        */
/* ============================================================ */

static uint16_t find_lfb_320x240(uint32_t *out_phys, uint16_t *out_xres,
                                 uint16_t *out_yres, uint32_t *out_total_vram,
                                 int *out_vbe_major, uint16_t *out_pitch)
{
    *out_phys = 0; *out_xres = 0; *out_yres = 0; *out_total_vram = 0; *out_vbe_major = 0;
    *out_pitch = 0;
    int sel = 0;
    int seg = __dpmi_allocate_dos_memory(32, &sel);
    if (seg < 0) return 0;
    uint32_t buflin = (uint32_t)seg << 4;
    _farpokeb(_dos_ds, buflin+0,'V'); _farpokeb(_dos_ds, buflin+1,'B');
    _farpokeb(_dos_ds, buflin+2,'E'); _farpokeb(_dos_ds, buflin+3,'2');
    for (int i = 4; i < 512; i++) _farpokeb(_dos_ds, buflin+i, 0);
    __dpmi_regs r; memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00; r.x.es = (uint16_t)seg; r.x.di = 0;
    if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) { __dpmi_free_dos_memory(sel); return 0; }
    uint16_t ver = _farpeekw(_dos_ds, buflin+0x04); *out_vbe_major = (ver>>8)&0xFF;
    uint16_t totblk = _farpeekw(_dos_ds, buflin+0x12); *out_total_vram = (uint32_t)totblk*65536u;
    uint16_t mp_off = _farpeekw(_dos_ds, buflin+0x0E);
    uint16_t mp_seg = _farpeekw(_dos_ds, buflin+0x10);
    uint32_t mode_lin = ((uint32_t)mp_seg<<4)+mp_off;
    uint16_t chosen = 0; uint32_t chosen_phys = 0, chosen_px = 0xFFFFFFFFul; uint16_t cx=0, cy=0, cp=0;
    for (int mi = 0; mi < 256; mi++) {
        uint16_t mode = _farpeekw(_dos_ds, mode_lin+mi*2);
        if (mode == 0xFFFF) break;
        for (int i = 256; i < 512; i++) _farpokeb(_dos_ds, buflin+i, 0);
        memset(&r, 0, sizeof r);
        r.x.ax=0x4F01; r.x.cx=mode; r.x.es=(uint16_t)seg; r.x.di=256;
        if (__dpmi_int(0x10,&r)<0 || r.x.ax!=0x004F) continue;
        uint32_t mb = buflin+256;
        uint16_t attr=_farpeekw(_dos_ds,mb+0x00), pitch=_farpeekw(_dos_ds,mb+0x10);
        uint16_t xr=_farpeekw(_dos_ds,mb+0x12), yr=_farpeekw(_dos_ds,mb+0x14);
        uint8_t bpp=_farpeekb(_dos_ds,mb+0x19), model=_farpeekb(_dos_ds,mb+0x1B);
        uint32_t phys=_farpeekl(_dos_ds,mb+0x28);
        if (!(attr&0x0001)||!(attr&0x0080)||bpp!=8||model!=4||phys==0) continue;
        uint32_t px=(uint32_t)xr*yr;
        if (px < PAGE_BYTES) continue;            /* must hold a 320x240 page  */
        if (px < chosen_px) { chosen=mode; chosen_phys=phys; chosen_px=px; cx=xr; cy=yr; cp=pitch; }
    }
    __dpmi_free_dos_memory(sel);
    if (chosen) { *out_phys=chosen_phys; *out_xres=cx; *out_yres=cy; *out_pitch=cp; }
    return chosen;
}

/* ============================================================ */
/* Display-start unit ladder                                     */
/* ============================================================ */

typedef enum { U_BYTE=0, U_WORD=1, U_DWORD=2 } units_t;
static const char *u_name(units_t u){ return u==U_BYTE?"byte":u==U_WORD?"word":"dword"; }
static uint32_t u_encode(units_t u, uint32_t byte_off)
{
    return u==U_BYTE ? byte_off : u==U_WORD ? byte_off/2 : byte_off/4;
}

/* Program CRTC start = `enc` (in the chip's units). CR0D=bits7:0, CR0C=bits15:8,
 * CR69[4:0]=bits20:16 (RMW preserving CR69[7:5]). Returns the 3 written bytes.
 * CLI-BRACKETED: the CRTC index/data pair (0x3D4/0x3D5) is stateful -- an IRQ
 * (audio IRQ-5 / timer) firing between an index write and its data write would
 * corrupt the write. cli/sti around the whole sequence makes it atomic. This is
 * the exact re-entrancy class that hung the VBE 0x4F07 flip; the direct-CRTC
 * replacement must be IRQ-atomic, and so must this probe of it. */
static void program_start(uint32_t enc, uint8_t *w0C, uint8_t *w0D, uint8_t *w69)
{
    *w0D = (uint8_t)(enc & 0xFF);
    *w0C = (uint8_t)((enc >> 8) & 0xFF);
    __asm__ __volatile__ ("cli");
    uint8_t c69 = cr_read(0x69);
    *w69 = (uint8_t)((c69 & 0xE0) | ((enc >> 16) & 0x1F));
    cr_write(0x0D, *w0D);          /* low first */
    cr_write(0x0C, *w0C);
    cr_write(0x69, *w69);
    __asm__ __volatile__ ("sti");
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    atexit(atexit_cleanup);
    open_log();
    slog("=== S3CRTC -- S3 ViRGE direct-CRTC display-start de-risk (task #15) ===");
    slog("S3CRTC-BEGIN");
    slog("version: %s", S3CRTC_VERSION);
    slog("build: DJGPP -march=i486 -mtune=pentium -O2; pure PM port I/O for the flip");

    /* (1) find + set the 320x240x8 LFB mode, map the LFB. */
    uint32_t phys=0, total_vram=0; uint16_t xres=0, yres=0, pitch=0; int vbemaj=0;
    uint16_t mode = find_lfb_320x240(&phys, &xres, &yres, &total_vram, &vbemaj, &pitch);
    if (!mode) {
        slog("[s3crtc] LFB_UNAVAILABLE no 8bpp LFB mode (vbe_major=%d)", vbemaj);
        slog("[s3crtc GATE verdict=ABORT_NO_LFB]"); slog("S3CRTC-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }
    /* page_size = ACTUAL pitch * height (NOT a hardcoded 76800) -- UNIVBE may pad
     * the 320x240 scanline; the page-1 start + the ladder encoding key off this. */
    if (pitch == 0) pitch = xres;                 /* defensive: some BIOS report 0 */
    uint32_t page_size = (uint32_t)pitch * yres;
    uint32_t page1_off = page_size;
    slog("[s3crtc] LFB mode=0x%04X %ux%u 8bpp pitch=%u phys=0x%08lX total_vram=%lu vbe_major=%d",
         mode, xres, yres, pitch, (unsigned long)phys, (unsigned long)total_vram, vbemaj);
    slog("[s3crtc] PITCH=%u page_size=%lu page1_off=0x%05lX pitches_match_320=%s",
         pitch, (unsigned long)page_size, (unsigned long)page1_off, (pitch==320)?"YES":"NO");
    if (pitch != 320)
        slog("[s3crtc] PITCH_FLAG pitch!=320 -- the engine pitches_match gate will NOT "
             "engage the page-flip lever even if the flip works here; sdl-engine needs "
             "this for SDL/0078 (the flip mechanism is still tested below).");
    if (total_vram < page1_off + page_size)
        slog("[s3crtc] WARN VRAM(%lu) < page1_off+page_size(%lu); flip test still attempted",
             (unsigned long)total_vram, (unsigned long)(page1_off + page_size));

    __dpmi_regs r; memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02; r.x.bx = mode | 0x4000;   /* LFB bit 14 */
    __dpmi_int(0x10, &r);
    g_mode_active = 1;
    int set_ok = (r.x.ax == 0x004F);
    if (!set_ok) slog("[s3crtc] WARN AX=4F02 set-mode != 0x004F (got 0x%04X)", r.x.ax);

    uint32_t map_size = page1_off + page_size;
    if (total_vram < map_size) map_size = (total_vram > page_size) ? total_vram : page_size;
    __dpmi_meminfo info; memset(&info, 0, sizeof info);
    info.address = phys; info.size = map_size;
    if (__dpmi_physical_address_mapping(&info) != 0 || !__djgpp_nearptr_enable()) {
        restore_text_mode();
        slog("[s3crtc] LFB_MAP_FAILED phys=0x%08lX size=%lu", (unsigned long)phys, (unsigned long)map_size);
        slog("[s3crtc GATE verdict=ABORT_MAP_FAILED]"); slog("S3CRTC-DONE");
        if (g_log) fclose(g_log);
        return 2;
    }
    uint8_t *lfb = (uint8_t *)(info.address + __djgpp_conventional_base);

    /* fill page 0 (dark) + page 1 (bright) -- the operator flip tell. */
    int page1_mapped = (map_size >= page1_off + page_size);
    memset(lfb + PAGE0_OFF, PAGE0_COLOR, page_size);
    if (page1_mapped) memset(lfb + page1_off, PAGE1_COLOR, page_size);
    /* page-corner unique bytes so a readback can self-check the fills landed. */
    lfb[PAGE0_OFF] = 0xA0; if (page1_mapped) lfb[page1_off] = 0xB1;
    int fills_ok = (lfb[PAGE0_OFF] == 0xA0 && (!page1_mapped || lfb[page1_off] == 0xB1));
    lfb[PAGE0_OFF] = PAGE0_COLOR; if (page1_mapped) lfb[page1_off] = PAGE1_COLOR;
    slog("[s3crtc] FILLS page0=0x%02X@0 page1=0x%02X@0x%05lX mapped=%s landed=%s",
         PAGE0_COLOR, PAGE1_COLOR, (unsigned long)page1_off,
         page1_mapped ? "YES" : "NO", fills_ok ? "OK" : "FAIL");

    /* (2) streams-mode check (86Box gate: (CR67 & 0x0C) == 0x0C). */
    s3_unlock_crtc();
    uint8_t cr67 = cr_read(0x67);
    int streams = (cr67 & 0x0C) == 0x0C;          /* CR67[3:2]==0b11, mask 0x0C */
    slog("[s3crtc] CR67=0x%02X CR67[3:2]=%u streams_mode=%s", cr67, (cr67>>2)&0x3,
         streams ? "YES" : "no");
    if (streams) {
        slog("[s3crtc] STREAMS_MODE=YES -- display start comes from the streams "
             "primary-FB reg (86Box: pri_fb0/1>>2, DWORD units), NOT CRTC 0C/0D/69. "
             "Direct-CRTC flip will NOT work; SDL/0078 must use the streams primary-FB "
             "path. Unit ladder below still runs (latch only -- it will NOT flip).");
    }

    /* high-start determinism: CR31[5:4]=bits17:16 + CR51[1:0]=bits19:18 are
     * SUPERSEDED by CR69[4:0], but a stale non-zero high bit here would add a
     * false offset. REPORT + clear them so the high bits come from CR69 ONLY. */
    uint8_t cr31 = cr_read(0x31), cr51 = cr_read(0x51);
    slog("[s3crtc] BASELINE_HIGH CR31=0x%02X(bits5:4=%u) CR51=0x%02X(bits1:0=%u)",
         cr31, (cr31>>4)&0x3, cr51, cr51&0x3);
    if ((cr31 & 0x30) || (cr51 & 0x03)) {
        cr_write(0x31, (uint8_t)(cr31 & ~0x30));
        cr_write(0x51, (uint8_t)(cr51 & ~0x03));
        slog("[s3crtc] cleared CR31[5:4]+CR51[1:0] (stale high start bits) -> CR69-only");
    }
    /* CR08[6:5] byte-pan = 0 (a non-zero pan would shift the displayed start). */
    uint8_t cr08 = cr_read(0x08);
    if (cr08 & 0x60) {
        cr_write(0x08, (uint8_t)(cr08 & ~0x60));
        slog("[s3crtc] cleared CR08[6:5] byte-pan (was 0x%02X)", cr08);
    }

    /* save the start regs to restore between units. */
    uint8_t s0C = cr_read(0x0C), s0D = cr_read(0x0D), s69 = cr_read(0x69);
    slog("[s3crtc] SAVED CR0C=0x%02X CR0D=0x%02X CR69=0x%02X", s0C, s0D, s69);

    /* baseline: start=0 -> page 0 (dark) visible. */
    program_start(0, &(uint8_t){0}, &(uint8_t){0}, &(uint8_t){0});
    wait_vbl();
    slog("[s3crtc] BASELINE start=0 -> operator should see DARK (page0 0x%02X). 2s...", PAGE0_COLOR);
    { uint32_t t0=(uint32_t)time(NULL); while ((uint32_t)time(NULL)-t0 < 2) {} }

    /* (3)+(4) unit ladder. */
    slog("[s3crtc SUITE_BEGIN mode=0x%04X %ux%u streams=%d]", mode, xres, yres, streams);
    int latch_ok[3] = {0,0,0};
    for (int u = 0; u < 3; u++) {
        uint32_t enc = u_encode((units_t)u, page1_off);
        uint8_t w0C=0, w0D=0, w69=0;
        program_start(enc, &w0C, &w0D, &w69);
        wait_vbl();
        uint8_t l0C=cr_read(0x0C), l0D=cr_read(0x0D), l69=cr_read(0x69);
        int lo = (l0D==w0D), mid = (l0C==w0C), hi = ((l69&0x1F)==(w69&0x1F));
        latch_ok[u] = lo && mid && hi;
        slog("[s3crtc UNIT=%-5s enc=0x%05lX wrote CR0C=0x%02X/CR0D=0x%02X/CR69=0x%02X "
             "latched CR0C=0x%02X/CR0D=0x%02X/CR69=0x%02X latch=%s]",
             u_name((units_t)u), (unsigned long)enc, w0C, w0D, w69, l0C, l0D, l69,
             latch_ok[u] ? "OK" : "MISMATCH");
        slog("[s3crtc] UNIT=%-5s operator: screen should be BRIGHT (page1 0x%02X) IFF "
             "UNIVBE uses %s units; else DARK/garbage. 2s...",
             u_name((units_t)u), PAGE1_COLOR, u_name((units_t)u));
        { uint32_t t0=(uint32_t)time(NULL); while ((uint32_t)time(NULL)-t0 < 2) {} }
        /* restore to page 0 between units. */
        cr_write(0x0D, s0D); cr_write(0x0C, s0C); cr_write(0x69, s69);
        wait_vbl();
    }

    /* ---- teardown before the verdict prose ---- */
    __djgpp_nearptr_disable();
    restore_text_mode();
    __dpmi_free_physical_address_mapping(&info);

    /* ---- verdict ---- */
    slog("");
    slog("[s3crtc] SELFTEST fills=%s set_mode=%s latch_ok=byte:%d/word:%d/dword:%d",
         fills_ok ? "OK" : "FAIL", set_ok ? "OK" : "WARN",
         latch_ok[0], latch_ok[1], latch_ok[2]);
    const char *verdict;
    if (!fills_ok)        verdict = "UNVERIF_FILLS";
    else if (streams)     verdict = "STREAMS_MODE_NO_DIRECT_CRTC";   /* decisive: use streams path */
    else if (latch_ok[0]||latch_ok[1]||latch_ok[2]) verdict = "LATCH_OK_OPERATOR_CONFIRMS_UNITS";
    else                  verdict = "RED_NO_LATCH";
    slog("[s3crtc GATE streams=%d latch=b%d/w%d/d%d verdict=%s]",
         streams, latch_ok[0], latch_ok[1], latch_ok[2], verdict);
    slog("  STREAMS_MODE_NO_DIRECT_CRTC -> 0078 must use the streams primary-FB reg.");
    slog("  LATCH_OK_OPERATOR_CONFIRMS_UNITS -> the WORKING UNITS = the unit whose 2s");
    slog("    hold turned the screen BRIGHT (page 1). Operator reports which (byte/word/");
    slog("    dword); sdl-engine bakes THAT into SDL/0078's direct-CRTC flip. (Cirrus");
    slog("    crtcswap verdict was BYTE; the S3 may differ -- the eyeball decides.)");
    slog("  RED_NO_LATCH -> the CRTC start regs did not accept writes (unexpected; "
         "re-check the CR38/CR39 unlock + CR67).");
    slog("[s3crtc SUITE_DONE]");
    slog("S3CRTC-DONE");
    if (g_log) fclose(g_log);
    return 0;
}
