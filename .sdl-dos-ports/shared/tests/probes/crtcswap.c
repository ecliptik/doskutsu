/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * crtcswap.c -- CRTC start-address encoding probe for Cirrus 5434 +
 *               UNIVBE 6.7 page-flip viability.
 *
 * Phase 11 wave-50 cycle 1, gating the SDL/0061 helper authoring.
 * sdl-engine STOP-and-ack flagged: CRTC start-address encoding in
 * VBE 8bpp linear mode is chip-config-dependent. The mode-set BIOS
 * configures the chip in one of {byte/word/dword/scanline} encoding
 * for CRTC[0x0C/0x0D] + Cirrus 5434 extension bits. Patch 0014
 * documents VBE 0x4F07 silently fails on this chip -- so the
 * page-flip mechanism MUST use direct CRTC port programming with
 * the right encoding.
 *
 * This probe writes 4 sentinel patterns to VRAM at offsets {0,
 * 38400, 76800, 153600} (each pattern fills a distinct 320x240-
 * equivalent slab with a recognizable palette index), then for each
 * candidate encoding programs the CRTC start-address registers to
 * point at offset 76800. After a VBL wait, the probe reads back the
 * latched register values + emits the result. Operator additionally
 * watches the screen to confirm visual region change.
 *
 * Encodings tested (per sdl-engine spec):
 *   1. byte units    -- CRTC value = offset / 1   (76800 = 0x12C00 = 17-bit)
 *   2. word units    -- CRTC value = offset / 2   (38400 = 0x9600  = 16-bit)
 *   3. dword units   -- CRTC value = offset / 4   (19200 = 0x4B00  = 15-bit)
 *   4. scanline      -- CRTC value = offset / row_pitch  (240 for 320x240)
 *
 * The chip's actual encoding is set by UniVBE 6.7 at mode-set time.
 * One of the four (most likely byte or word units based on Cirrus
 * 5434 datasheet documentation) will produce a successful latch + a
 * visible region change.
 *
 * Output: CRTCSWAP.LOG (per-line fsync). Falls back to ./CRTCSWAP.LOG
 * if C:\\ is read-only.
 *
 * Pure DJGPP. No SDL. Side effects bounded to:
 *   - VBE mode set (640x480x8 mode 0x101 banked; restored to text on exit)
 *   - Cirrus SR[0x06] = 0x12 unlock (non-destructive; same as chipid.c)
 *   - CRTC[0x0C/0x0D/0x1B/0x1D] writes per encoding scenario; restored
 *     to original values between scenarios + at exit via atexit handler
 *
 * Watchdog: per-scenario 3s budget; total runtime ~30s incl. operator
 * visual confirmation pauses.
 *
 * HAZARD: incorrect register writes can produce screen blanking +
 * scrambled display until restore-on-exit fires. atexit handler
 * restores text mode + zeroes the start-address even on abnormal
 * exit. Operator can also Ctrl-Alt-Del; the chip recovers on POST.
 *
 * 8.3 DOS filenames:
 *   Source:   tests/probes/crtcswap.c
 *   Binary:   CRTCSWAP.EXE  (7+3)
 *   Log:      CRTCSWAP.LOG  (7+3)
 *   BAT:      CRTCSWAP.BAT  (7+3)
 *
 * Build: `make crtcswap`.
 */

#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>          /* outportb / inportb */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <time.h>
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void cwlog(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) {
        fputs(buf, g_log); fputc('\n', g_log);
        fflush(g_log); fsync(fileno(g_log));
    }
}

static void open_log(void)
{
    g_log = fopen("C:\\CRTCSWAP.LOG", "w");
    if (!g_log) g_log = fopen("CRTCSWAP.LOG", "w");
}

/* ============================================================ */
/* Cleanup atexit -- restore text mode + zero CRTC start         */
/* ============================================================ */

static volatile int g_vbe_mode_active = 0;

static void restore_text_mode(void)
{
    /* Zero the CRTC start-address before mode-restore so any latched
     * non-zero value doesn't carry through to the next mode. */
    outportb(0x3D4, 0x0C); outportb(0x3D5, 0);
    outportb(0x3D4, 0x0D); outportb(0x3D5, 0);
    /* Restore text mode (INT 10h AX=0003). */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
    g_vbe_mode_active = 0;
}

static void atexit_cleanup(void)
{
    if (g_vbe_mode_active) restore_text_mode();
    if (g_log) { fclose(g_log); g_log = NULL; }
}

/* ============================================================ */
/* VBE helpers                                                   */
/* ============================================================ */

typedef struct {
    uint16_t ModeAttributes;
    uint8_t  WinAAttr, WinBAttr;
    uint16_t WinGranularity, WinSize;
    uint16_t WinASegment, WinBSegment;
    uint32_t WinFuncPtr;
    uint16_t BytesPerScanLine;
    /* 1.2+ */
    uint16_t XResolution, YResolution;
    uint8_t  XCharSize, YCharSize;
    uint8_t  NumberOfPlanes;
    uint8_t  BitsPerPixel;
    uint8_t  NumberOfBanks;
    uint8_t  MemoryModel;
    uint8_t  BankSize;
    uint8_t  NumberOfImagePages;
    uint8_t  Reserved1;
    /* DAC + direct-color (skipped) */
    uint8_t  RedMaskSize, RedFieldPosition;
    uint8_t  GreenMaskSize, GreenFieldPosition;
    uint8_t  BlueMaskSize, BlueFieldPosition;
    uint8_t  RsvdMaskSize, RsvdFieldPosition;
    uint8_t  DirectColorModeInfo;
    uint32_t PhysBasePtr;
    uint32_t Reserved2;
    uint16_t Reserved3;
    /* ... 189 reserved bytes follow */
} vbe_mode_info_t;

static int vbe_get_mode_info(uint16_t mode, vbe_mode_info_t *out)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = mode;
    r.x.es = (__tb >> 4) & 0xFFFF;
    r.x.di = __tb & 0x0F;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    if (r.x.ax != 0x004F) return -2;
    memset(out, 0, sizeof *out);
    dosmemget(__tb, sizeof *out, out);
    return 0;
}

static int vbe_set_mode(uint16_t mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = mode;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return (r.x.ax == 0x004F) ? 0 : -2;
}

static int vbe_set_bank(int bank)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F05;
    r.x.bx = 0x0000;
    r.x.dx = (uint16_t)bank;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    return (r.x.ax == 0x004F) ? 0 : -2;
}

/* ============================================================ */
/* CRTC + Cirrus extension register helpers                     */
/*                                                               */
/* Port layout (color mode; MISC OUTPUT bit 0 = 1):              */
/*   0x3D4 = CRTC index port                                     */
/*   0x3D5 = CRTC data port                                      */
/*   0x3DA = Input Status 1 (bit 3 = VSYNC, bit 0 = display enable) */
/*                                                                */
/* Cirrus 5434 extension regs (after SR[0x06] = 0x12 unlock):    */
/*   CRTC[0x1B] = extended display controls; bit 0 = display     */
/*                start bit 16                                   */
/*   CRTC[0x1D] = extended display controls 2; bits per chip rev */
/*                may carry display start bits 17/18             */
/* ============================================================ */

static uint8_t crtc_read(uint8_t idx)
{
    outportb(0x3D4, idx);
    return inportb(0x3D5);
}

static void crtc_write(uint8_t idx, uint8_t val)
{
    outportb(0x3D4, idx);
    outportb(0x3D5, val);
}

/* Unlock Cirrus extension registers. Non-destructive; same as
 * chipid.c. Returns the previous SR[0x06] value so caller can
 * restore. */
static uint8_t cirrus_unlock(void)
{
    outportb(0x3C4, 0x06);
    uint8_t prev = inportb(0x3C5);
    outportb(0x3C4, 0x06);
    outportb(0x3C5, 0x12);
    return prev;
}

static void cirrus_relock(uint8_t prev)
{
    outportb(0x3C4, 0x06);
    outportb(0x3C5, prev);
}

/* Wait for one VBL boundary using standard VGA Input Status 1.
 * Bounded poll (max ~50 ms = one frame at 20 Hz worst case). */
static void wait_vbl(void)
{
    /* Wait until display-active (out of vblank), then until vblank
     * starts. This gives a precise vblank-onset edge. */
    int i = 0;
    while ((inportb(0x3DA) & 0x08) != 0) { if (++i > 100000) return; }
    i = 0;
    while ((inportb(0x3DA) & 0x08) == 0) { if (++i > 100000) return; }
}

/* ============================================================ */
/* Encoding scenarios                                            */
/* ============================================================ */

typedef enum {
    ENC_BYTE     = 0,  /* CRTC = byte_offset / 1 */
    ENC_WORD     = 1,  /* CRTC = byte_offset / 2 */
    ENC_DWORD    = 2,  /* CRTC = byte_offset / 4 */
    ENC_SCANLINE = 3,  /* CRTC = byte_offset / row_pitch */
} encoding_t;

static const char *enc_name(encoding_t e)
{
    switch (e) {
    case ENC_BYTE:     return "byte";
    case ENC_WORD:     return "word";
    case ENC_DWORD:    return "dword";
    case ENC_SCANLINE: return "scanline";
    }
    return "?";
}

/* Compute encoded value for a given byte offset under each encoding. */
static uint32_t encode_offset(encoding_t e, uint32_t byte_offset, uint32_t row_pitch)
{
    switch (e) {
    case ENC_BYTE:     return byte_offset;
    case ENC_WORD:     return byte_offset / 2;
    case ENC_DWORD:    return byte_offset / 4;
    case ENC_SCANLINE: return (row_pitch > 0) ? byte_offset / row_pitch : 0;
    }
    return 0;
}

/* Program CRTC[0x0C/0x0D] + Cirrus ext [0x1B/0x1D] with the encoded
 * value. Returns the bytes written to each reg via out-params so the
 * caller can compare against latched-readback. */
static void program_crtc_start(uint32_t enc_val,
                                uint8_t *out_0C, uint8_t *out_0D,
                                uint8_t *out_1B, uint8_t *out_1D)
{
    /* Standard 16-bit start-address: bits 0-7 in 0x0D, bits 8-15 in
     * 0x0C. Bits 16+ go into Cirrus extension regs. We preserve the
     * non-display-start bits of 0x1B / 0x1D by read-modify-write. */
    *out_0D = (uint8_t)(enc_val & 0xFF);
    *out_0C = (uint8_t)((enc_val >> 8) & 0xFF);

    /* Cirrus 5434: CRTC[0x1B] bit 0 = display start bit 16. Per Cirrus
     * 5434 datasheet "Memory Mapping Register"; mask off bit 0 of the
     * existing value, OR-in our bit. */
    uint8_t v1B = crtc_read(0x1B);
    *out_1B = (uint8_t)((v1B & ~0x01) | ((enc_val >> 16) & 0x01));

    /* Cirrus 5434: CRTC[0x1D] -- additional display-start bits per
     * chip rev. We zero-out bits 6+7 for simplicity (the higher
     * encoding values up to dword unit fit in 17 bits anyway). */
    uint8_t v1D = crtc_read(0x1D);
    *out_1D = (uint8_t)(v1D & ~0xC0);

    /* Order of writes per Cirrus 5434 + IBM VGA convention:
     *   0x0D (lo)  -- write first
     *   0x0C (hi)
     *   0x1B (ext bit 16)
     *   0x1D (ext bits 17+)
     * Some chips latch on 0x0C write (after the lo byte is staged in
     * the chip); others latch on 0x0D. Write all four in this order
     * for compatibility. */
    crtc_write(0x0D, *out_0D);
    crtc_write(0x0C, *out_0C);
    crtc_write(0x1B, *out_1B);
    crtc_write(0x1D, *out_1D);
}

/* ============================================================ */
/* Fill VRAM with 4 sentinel slabs via banked dosmemput          */
/* ============================================================ */

#define SLAB0_OFFSET  0u
#define SLAB1_OFFSET  38400u
#define SLAB2_OFFSET  76800u
#define SLAB3_OFFSET  153600u
#define SLAB_BYTES    38400u   /* 120 rows x 320 = 38400 bytes per slab */

#define BANK_SIZE     65536u

/* Fill an in-VRAM 38400-byte slab via banked dosmemput. dst is a byte
 * offset within VRAM; values are written to the 0xA0000 window via
 * VBE bank switching. */
static int vram_fill_slab(uint32_t dst_byte_offset, uint8_t pixel_val)
{
    /* Source buffer: 1 KB of repeating pixel_val (chunked write keeps
     * the DOS conventional-memory buffer small). */
    enum { CHUNK = 1024 };
    static uint8_t chunk_buf[CHUNK];
    memset(chunk_buf, pixel_val, CHUNK);

    /* Allocate a DOS conventional-memory buffer for the dosmemput
     * source. Using __tb (transfer buffer) is fine for our 1KB chunk. */
    unsigned long tb = __tb;
    /* Write the chunk_buf contents into the DPMI transfer buffer. */
    dosmemput(chunk_buf, CHUNK, tb);

    uint32_t remaining = SLAB_BYTES;
    uint32_t cur_off = dst_byte_offset;
    while (remaining > 0) {
        uint32_t this_chunk = (remaining < CHUNK) ? remaining : CHUNK;
        /* Bank-switch to the bank containing cur_off. */
        int bank = (int)(cur_off / BANK_SIZE);
        if (vbe_set_bank(bank) != 0) return -1;
        uint32_t in_bank = cur_off % BANK_SIZE;
        uint32_t can_write = BANK_SIZE - in_bank;
        if (this_chunk > can_write) this_chunk = can_write;

        /* dosmemput from the transfer buffer to 0xA0000 + in_bank. */
        unsigned long dst_real = 0xA0000ul + in_bank;
        movedata(_dos_ds, tb, _dos_ds, dst_real, this_chunk);

        cur_off += this_chunk;
        remaining -= this_chunk;
    }
    return 0;
}

/* ============================================================ */
/* Per-scenario runner                                           */
/* ============================================================ */

typedef struct {
    encoding_t enc;
    uint8_t target_pixel;     /* sentinel value at target offset */
    uint32_t target_offset;   /* 76800 = slab 2 */
} scenario_t;

static void run_scenario(const scenario_t *s, uint32_t row_pitch,
                          uint8_t *saved_0C, uint8_t *saved_0D,
                          uint8_t *saved_1B, uint8_t *saved_1D)
{
    uint32_t enc_val = encode_offset(s->enc, s->target_offset, row_pitch);

    cwlog("");
    cwlog("[CRTCSWAP-SCENARIO-BEGIN] encoding=%s enc_val=0x%05lX (decimal=%lu)",
         enc_name(s->enc), (unsigned long)enc_val, (unsigned long)enc_val);

    uint8_t wrote_0C = 0, wrote_0D = 0, wrote_1B = 0, wrote_1D = 0;
    program_crtc_start(enc_val, &wrote_0C, &wrote_0D, &wrote_1B, &wrote_1D);

    /* Wait one VBL for the chip to latch the new start-address.
     * Standard VGA latches start-address on vblank-onset. */
    wait_vbl();

    uint8_t latched_0C = crtc_read(0x0C);
    uint8_t latched_0D = crtc_read(0x0D);
    uint8_t latched_1B = crtc_read(0x1B);
    uint8_t latched_1D = crtc_read(0x1D);

    cwlog("[CRTCSWAP-SCENARIO] encoding=%s wrote_0C=0x%02X wrote_0D=0x%02X "
         "wrote_1B=0x%02X wrote_1D=0x%02X",
         enc_name(s->enc), wrote_0C, wrote_0D, wrote_1B, wrote_1D);
    cwlog("[CRTCSWAP-SCENARIO] encoding=%s latched_0C=0x%02X latched_0D=0x%02X "
         "latched_1B=0x%02X latched_1D=0x%02X",
         enc_name(s->enc), latched_0C, latched_0D, latched_1B, latched_1D);

    /* Latch verification: 0x0C/0x0D should match wrote-vs-latched for
     * any chip that accepts the write. Cirrus ext regs (0x1B/0x1D)
     * may have other bits set by the chip; check only the bits we
     * touched (bit 0 of 0x1B; bits 6-7 of 0x1D). */
    int match_lo = (latched_0D == wrote_0D);
    int match_hi = (latched_0C == wrote_0C);
    int match_ext = ((latched_1B & 0x01) == (wrote_1B & 0x01))
                 && ((latched_1D & 0xC0) == (wrote_1D & 0xC0));

    const char *latch_result;
    if (match_lo && match_hi && match_ext) latch_result = "FULL_LATCH";
    else if (match_lo && match_hi)         latch_result = "STANDARD_LATCH_EXT_UNCERTAIN";
    else                                    latch_result = "LATCH_FAIL";

    cwlog("[CRTCSWAP-SCENARIO] encoding=%s latch_result=%s "
         "match_lo=%d match_hi=%d match_ext=%d",
         enc_name(s->enc), latch_result, match_lo, match_hi, match_ext);
    cwlog("[CRTCSWAP-SCENARIO] encoding=%s operator_visual_target_pixel=0x%02X (expected_visible_region_color_index)",
         enc_name(s->enc), s->target_pixel);

    /* Hold the chip in this state for ~2 seconds so operator can see
     * the screen. NOT a microbench; just human-eye time. */
    {
        uint32_t t0 = (uint32_t)time(NULL);
        while ((uint32_t)time(NULL) - t0 < 2) { /* spin */ }
    }

    /* Restore start-address to original (0 typically) between scenarios
     * so the next encoding test starts from a known state. */
    crtc_write(0x0D, *saved_0D);
    crtc_write(0x0C, *saved_0C);
    crtc_write(0x1B, *saved_1B);
    crtc_write(0x1D, *saved_1D);
    wait_vbl();

    cwlog("[CRTCSWAP-SCENARIO-DONE] encoding=%s", enc_name(s->enc));
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    atexit(atexit_cleanup);
    open_log();

    cwlog("=== CRTCSWAP wave-50 cycle 1 probe (probe-engineer) ===");
    cwlog("=== output: %s ; per-line fsync; per-scenario sentinels ===",
         g_log ? "C:\\CRTCSWAP.LOG (or fallback ./CRTCSWAP.LOG)" : "(stdout only; log open failed)");
    cwlog("=== build date: " __DATE__ " " __TIME__ " ===");

    /* Probe target mode: VBE 8bpp mode wide enough to hold 4 slabs of
     * 38400 bytes = 153600 bytes total. Mode 0x101 = 640x480x8 banked
     * = 307200 bytes total VRAM mapped. Fits our 4 slabs cleanly.
     * Visible region in this mode covers offsets [0, 307200); display
     * start = 76800 (slab 2) shows slab 2 starting at the top of
     * the visible screen with wrap-around at offset 307200. */
    const uint16_t TEST_MODE = 0x0101;

    vbe_mode_info_t mi;
    if (vbe_get_mode_info(TEST_MODE, &mi) != 0) {
        cwlog("[CRTCSWAP-FAIL] vbe_get_mode_info(0x%04X) failed; aborting", TEST_MODE);
        return 2;
    }
    cwlog("[CRTCSWAP-MODE] mode=0x%04X x=%u y=%u bpp=%u bytes_per_scanline=%u memmodel=%u",
         TEST_MODE, mi.XResolution, mi.YResolution, mi.BitsPerPixel,
         mi.BytesPerScanLine, mi.MemoryModel);
    if (mi.BitsPerPixel != 8) {
        cwlog("[CRTCSWAP-FAIL] mode 0x%04X not 8bpp; aborting (got bpp=%u)",
             TEST_MODE, mi.BitsPerPixel);
        return 2;
    }
    const uint32_t row_pitch = mi.BytesPerScanLine;

    if (vbe_set_mode(TEST_MODE) != 0) {
        cwlog("[CRTCSWAP-FAIL] vbe_set_mode(0x%04X) failed; aborting", TEST_MODE);
        return 2;
    }
    g_vbe_mode_active = 1;
    cwlog("[CRTCSWAP-MODE] entered mode 0x%04X successfully", TEST_MODE);

    /* Fill the 4 sentinel slabs. */
    static const struct { uint32_t off; uint8_t val; const char *name; } slabs[4] = {
        { SLAB0_OFFSET,  0x10, "slab0_dark_blue" },
        { SLAB1_OFFSET,  0x20, "slab1_red"       },
        { SLAB2_OFFSET,  0x30, "slab2_orange"    },
        { SLAB3_OFFSET,  0x40, "slab3_green"     }
    };
    for (int i = 0; i < 4; i++) {
        if (vram_fill_slab(slabs[i].off, slabs[i].val) != 0) {
            cwlog("[CRTCSWAP-FAIL] vram_fill_slab(off=%lu val=0x%02X) failed",
                 (unsigned long)slabs[i].off, slabs[i].val);
            return 3;
        }
        cwlog("[CRTCSWAP-FILL] %s offset=%lu pixel=0x%02X",
             slabs[i].name, (unsigned long)slabs[i].off, slabs[i].val);
    }

    /* Unlock Cirrus extension registers. */
    uint8_t saved_sr06 = cirrus_unlock();
    cwlog("[CRTCSWAP-CIRRUS] SR[0x06] unlocked (was 0x%02X; now 0x12)", saved_sr06);

    /* Save initial CRTC display-start regs so we can restore between
     * scenarios. */
    uint8_t saved_0C = crtc_read(0x0C);
    uint8_t saved_0D = crtc_read(0x0D);
    uint8_t saved_1B = crtc_read(0x1B);
    uint8_t saved_1D = crtc_read(0x1D);
    cwlog("[CRTCSWAP-SAVED] crtc_0C=0x%02X crtc_0D=0x%02X crtc_1B=0x%02X crtc_1D=0x%02X",
         saved_0C, saved_0D, saved_1B, saved_1D);

    /* Operator visual: at this point screen should show slab 0 (dark
     * blue) at top of visible region (since display-start = 0). Pause
     * 2 sec for operator to confirm baseline. */
    cwlog("[CRTCSWAP-BASELINE] operator should see slab 0 (dark blue 0x10) at top");
    {
        uint32_t t0 = (uint32_t)time(NULL);
        while ((uint32_t)time(NULL) - t0 < 2) { /* spin */ }
    }

    /* Per-encoding scenarios. Target = slab 2 (orange 0x30) at offset
     * 76800. Operator looks for orange-colored visible region during
     * the 2 sec pause inside run_scenario. */
    static const scenario_t scenarios[4] = {
        { ENC_BYTE,     0x30, SLAB2_OFFSET },
        { ENC_WORD,     0x30, SLAB2_OFFSET },
        { ENC_DWORD,    0x30, SLAB2_OFFSET },
        { ENC_SCANLINE, 0x30, SLAB2_OFFSET },
    };
    for (int i = 0; i < 4; i++) {
        run_scenario(&scenarios[i], row_pitch,
                     &saved_0C, &saved_0D, &saved_1B, &saved_1D);
    }

    /* Restore SR[0x06] to original lock state. */
    cirrus_relock(saved_sr06);
    cwlog("[CRTCSWAP-CIRRUS] SR[0x06] restored to 0x%02X", saved_sr06);

    /* Restore text mode (atexit also does this; explicit here too). */
    restore_text_mode();

    cwlog("");
    cwlog("[CRTCSWAP-EXIT_OK] all 4 encoding scenarios completed");
    cwlog("[CRTCSWAP-NEXT] operator: capture this log + a screen photo of the");
    cwlog("[CRTCSWAP-NEXT] BYTE scenario (or whichever produced visible-region change)");
    cwlog("[CRTCSWAP-NEXT] for sdl-engine SDL/0061 to bake into the helper constants");

    return 0;
}
