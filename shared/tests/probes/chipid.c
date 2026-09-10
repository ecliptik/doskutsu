/*
 * chipid.c — Cirrus 5434 chip-detect + BLT-engagement forensic dump
 * (Phase 11 wave-27 / iter K, BLTFILL v2 companion).
 *
 * Question: if BLTFILL v2 still skips Scenario A on g2k after the
 * register-encoding corrections, what's the chip actually showing? This
 * probe dumps the full set of Cirrus extension registers + VBE mode
 * + PCI config space + standard VGA register state at the moment
 * BLTFILL would also see them.
 *
 * Lighter than HWLOG.EXE — focused specifically on the BLT-engagement
 * question. Sections:
 *
 *   A. Standard VGA register snapshot (port reads only, no side-effects)
 *   B. Cirrus extension unlock cycle (SR[0x06] before/after writing 0x12)
 *   C. Full CRTC dump CRTC[0x00..0x40]  (chip-id at 0x27)
 *   D. Full SR dump SR[0x00..0x10]      (extension lock at 0x06)
 *   E. Full GR dump GR[0x00..0x33]      (BLT engine extensions + errata)
 *   F. VBE controller info (INT 10h AX=4F00) — OEM strings, version
 *   G. Current VBE mode + ModeInfo (AX=4F03 / AX=4F01)
 *   H. PCI BIOS scan for Cirrus device 0x1013 + 256-byte config space
 *
 * This probe is READ-ONLY for chip state EXCEPT for the SR[0x06] unlock
 * write (which is non-destructive — just enables Cirrus extension regs).
 * VBE mode is NOT changed; probe reads the mode the operator was already
 * in when invoked.
 *
 * Output: CHIPID.LOG (per-line fsync).
 *
 * Pure DJGPP. No SDL, no engine, no chip-state mutation beyond the
 * Cirrus unlock cycle.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/chipid.c
 *   Binary:   CHIPID.EXE  (6+3, fits)
 *   Log:      CHIPID.LOG  (6+3, fits)
 *   BAT:      CHIPID.BAT  (6+3, fits)
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
#include <unistd.h>

/* ============================================================ */
/* Logging                                                       */
/* ============================================================ */

static FILE *g_log = NULL;

static void open_log(void)
{
    g_log = fopen("CHIPID.LOG", "w");
    if (!g_log) g_log = fopen("C:\\CHIPID.LOG", "w");
}

static void clog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
        fsync(fileno(g_log));
    }
}

/* ============================================================ */
/* VGA / Cirrus port helpers                                     */
/* ============================================================ */

#define SR_INDEX  0x3C4
#define SR_DATA   0x3C5
#define GR_INDEX  0x3CE
#define GR_DATA   0x3CF
#define CRTC_IDX  0x3D4
#define CRTC_DATA 0x3D5

static uint8_t sr_read(uint8_t idx)  { outportb(SR_INDEX, idx);  return inportb(SR_DATA); }
static void    sr_write(uint8_t idx, uint8_t v) { outportb(SR_INDEX, idx); outportb(SR_DATA, v); }
static uint8_t gr_read(uint8_t idx)  { outportb(GR_INDEX, idx);  return inportb(GR_DATA); }
static uint8_t cr_read(uint8_t idx)  { outportb(CRTC_IDX, idx);  return inportb(CRTC_DATA); }

/* ============================================================ */
/* Section dumpers                                               */
/* ============================================================ */

static void dump_section_A_vga_status(void)
{
    clog("=== Section A: standard VGA register snapshot ===");
    /* Misc Output Register (read-only, port 0x3CC). */
    uint8_t misc = inportb(0x3CC);
    clog("  Misc Output (0x3CC) = 0x%02X  (bit 0 = 1 -> color CRTC at 0x3D4)",
         misc);

    /* Input Status #1 (port 0x3DA, color). */
    uint8_t status1 = inportb(0x3DA);
    clog("  Input Status #1 (0x3DA) = 0x%02X  (bit 3 = vsync, bit 0 = display enable)",
         status1);
}

static void dump_section_B_cirrus_unlock(uint8_t *out_sr06_before,
                                         uint8_t *out_sr06_after)
{
    clog("");
    clog("=== Section B: Cirrus extension unlock cycle ===");
    uint8_t sr06_before = sr_read(0x06);
    *out_sr06_before = sr06_before;
    clog("  SR[0x06] BEFORE unlock write = 0x%02X", sr06_before);
    clog("    (default 0x0F = locked; 0x12 = already-unlocked Cirrus extension)");

    sr_write(0x06, 0x12);

    uint8_t sr06_after = sr_read(0x06);
    *out_sr06_after = sr06_after;
    clog("  SR[0x06] AFTER  unlock write = 0x%02X", sr06_after);
    if (sr06_after == 0x12) {
        clog("  -> ENGAGEMENT: SR[0x06] write-read returned 0x12 (Cirrus signature)");
    } else {
        clog("  -> NO ENGAGEMENT: SR[0x06] readback != 0x12 (chip not Cirrus or");
        clog("     extension-lock register is at a different address on this chip)");
    }
}

static void dump_register_block(const char *label, uint8_t (*read_fn)(uint8_t),
                                int start, int end)
{
    clog("");
    clog("=== %s [0x%02X..0x%02X] ===", label, start, end);
    for (int row = start; row <= end; row += 16) {
        char line[128];
        int n = snprintf(line, sizeof line, "  0x%02X:", row);
        for (int col = 0; col < 16; col++) {
            int idx = row + col;
            if (idx > end) break;
            n += snprintf(line + n, sizeof line - n, " %02X",
                          read_fn((uint8_t)idx));
        }
        clog("%s", line);
    }
}

static void dump_section_C_crtc(uint8_t *out_chip_id)
{
    dump_register_block("Section C: CRTC (port 0x3D4 + 0x3D5)",
                        cr_read, 0x00, 0x3F);
    *out_chip_id = cr_read(0x27);
    clog("");
    clog("  CRTC[0x27] (chip-id) = 0x%02X", *out_chip_id);
    const char *name = "(unknown / not Cirrus 543x)";
    switch (*out_chip_id) {
        case 0xA0: name = "Cirrus CL-GD5430"; break;
        case 0xA8: name = "Cirrus CL-GD5434"; break;
        case 0xAC: name = "Cirrus CL-GD5436"; break;
        case 0xB8: name = "Cirrus CL-GD5446"; break;
    }
    clog("  CRTC[0x27] decoded = %s", name);
}

static void dump_section_D_sr(void)
{
    dump_register_block("Section D: Sequencer SR (port 0x3C4 + 0x3C5)",
                        sr_read, 0x00, 0x0F);
}

static void dump_section_E_gr(void)
{
    dump_register_block("Section E: Graphics GR (port 0x3CE + 0x3CF)",
                        gr_read, 0x00, 0x3F);
    /* Decode key BLT engine extension regs. */
    clog("");
    clog("  GR BLT extension decode:");
    clog("    GR[0x0B]                = 0x%02X  (must have bits 1+4 clear for screen-to-screen BLT)",
         gr_read(0x0B));
    clog("    GR[0x30] BLT mode       = 0x%02X", gr_read(0x30));
    clog("    GR[0x31] BLT status/ctl = 0x%02X  (bit 0 = BUSY, bit 3 = PROGRESS)",
         gr_read(0x31));
    clog("    GR[0x32] ROP            = 0x%02X  (0x0D = SRCCOPY)",
         gr_read(0x32));
    clog("    GR[0x33] mode extension = 0x%02X", gr_read(0x33));
}

static void dump_section_F_vbe_info(void)
{
    clog("");
    clog("=== Section F: VBE controller info (INT 10h AX=4F00) ===");

    int sel = 0;
    int seg = __dpmi_allocate_dos_memory(32, &sel);
    if (seg < 0) {
        clog("  ERROR: cannot allocate 512-byte real-mode buffer");
        return;
    }

    /* Write VBE2 signature so VBE 2.0+ BIOS returns OEM strings. */
    _farpokew(_dos_ds, ((unsigned long)seg << 4) + 0, 0x4256);  /* 'VB' */
    _farpokew(_dos_ds, ((unsigned long)seg << 4) + 2, 0x3245);  /* 'E2' */
    /* Zero remaining bytes. */
    for (int i = 4; i < 512; i++) _farpokeb(_dos_ds, ((unsigned long)seg << 4) + i, 0);

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00;
    r.x.es = (uint16_t)seg;
    r.x.di = 0;
    if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) {
        clog("  ERROR: INT 10h AX=4F00 failed (AX=0x%04X)", r.x.ax);
        __dpmi_free_dos_memory(sel);
        return;
    }

    uint8_t buf[512];
    for (int i = 0; i < 512; i++) {
        buf[i] = _farpeekb(_dos_ds, ((unsigned long)seg << 4) + i);
    }

    clog("  Signature: '%c%c%c%c'", buf[0], buf[1], buf[2], buf[3]);
    clog("  Version:   0x%04X (BCD %d.%d)",
         *(uint16_t *)(buf + 4), buf[5], buf[4]);

    /* OEM strings — point to far ptrs at offsets 6/22/26/30. */
    uint16_t oem_off = *(uint16_t *)(buf + 6);
    uint16_t oem_seg = *(uint16_t *)(buf + 8);
    if (oem_seg) {
        char oem_str[128] = {0};
        for (int i = 0; i < 127; i++) {
            uint8_t c = _farpeekb(_dos_ds,
                                  ((unsigned long)oem_seg << 4) + oem_off + i);
            if (c == 0) break;
            oem_str[i] = (c >= 32 && c < 127) ? c : '?';
        }
        clog("  OEM string: '%s'", oem_str);
    }

    __dpmi_free_dos_memory(sel);
}

static void dump_section_G_current_mode(void)
{
    clog("");
    clog("=== Section G: current VBE mode + ModeInfo ===");
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F03;
    if (__dpmi_int(0x10, &r) < 0 || r.x.ax != 0x004F) {
        clog("  WARN: INT 10h AX=4F03 (get current mode) failed");
        return;
    }
    uint16_t mode = r.x.bx & 0x3FFF;
    clog("  Current mode: 0x%04X", mode);
    if (mode < 0x100) {
        clog("  (mode < 0x100 -> legacy text/EGA; ModeInfo not VBE-applicable)");
        return;
    }
    /* Could fetch full ModeInfo via AX=4F01 but it's a big block; keep it
     * to the headline mode number for CHIPID's tighter scope. HWLOG.EXE
     * has the full ModeInfo dumper if we need it. */
}

static void dump_section_H_pci(void)
{
    clog("");
    clog("=== Section H: PCI scan for Cirrus Logic (vendor 0x1013) ===");
    /* INT 1Ah AH=B102 = PCI BIOS Find Device by ID. */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB102;
    r.x.cx = 0x00A8;  /* device ID for CL-GD5434 (0xA8) */
    r.x.dx = 0x1013;  /* vendor: Cirrus Logic */
    r.x.si = 0;       /* index: first match */
    if (__dpmi_int(0x1A, &r) < 0) {
        clog("  ERROR: INT 1Ah PCI BIOS call failed (DPMI dispatch)");
        return;
    }
    if ((r.h.ah) != 0) {
        clog("  No CL-GD5434 (0x1013:0x00A8) found via PCI BIOS scan");
        clog("  (status AH=0x%02X). Trying CL-GD5430 (0xA0)...", r.h.ah);
        memset(&r, 0, sizeof r);
        r.x.ax = 0xB102;
        r.x.cx = 0x00A0;
        r.x.dx = 0x1013;
        r.x.si = 0;
        if (__dpmi_int(0x1A, &r) < 0 || r.h.ah != 0) {
            clog("  No CL-GD5430 either. Chip may not be PCI-attached or BIOS scan failed.");
            return;
        }
    }
    uint8_t bus = r.h.bh;
    uint8_t devfunc = r.h.bl;
    clog("  Found Cirrus device at bus=0x%02X devfunc=0x%02X", bus, devfunc);

    /* Dump 64 bytes of PCI config space. */
    clog("  PCI config space (offsets 0x00..0x3F):");
    for (int off = 0; off < 64; off += 16) {
        char line[128];
        int n = snprintf(line, sizeof line, "    0x%02X:", off);
        for (int col = 0; col < 16; col++) {
            __dpmi_regs cr;
            memset(&cr, 0, sizeof cr);
            cr.x.ax = 0xB108;  /* read config byte */
            cr.x.bx = (bus << 8) | devfunc;
            cr.x.di = off + col;
            if (__dpmi_int(0x1A, &cr) < 0) break;
            n += snprintf(line + n, sizeof line - n, " %02X", cr.h.cl);
        }
        clog("%s", line);
    }

    /* Decode key fields. */
    __dpmi_regs cr;
    memset(&cr, 0, sizeof cr);
    cr.x.ax = 0xB108; cr.x.bx = (bus << 8) | devfunc; cr.x.di = 0x08;
    __dpmi_int(0x1A, &cr);
    uint8_t rev = cr.h.cl;

    memset(&cr, 0, sizeof cr);
    cr.x.ax = 0xB10A; cr.x.bx = (bus << 8) | devfunc; cr.x.di = 0x00;
    __dpmi_int(0x1A, &cr);
    uint32_t vendev = cr.d.ecx;

    clog("");
    clog("  PCI vendor:device = 0x%04lX:0x%04lX  rev=0x%02X",
         (unsigned long)(vendev & 0xFFFF),
         (unsigned long)((vendev >> 16) & 0xFFFF),
         rev);
}

/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    clog("=== CHIPID wave-27 / iter K starting ===");
    clog("DJGPP build, target = Cirrus 5434 BLT-engagement forensic dump");
    clog("");
    clog("Read-only chip state probe. Only side-effect: SR[0x06] write 0x12");
    clog("(Cirrus extension unlock — non-destructive, just enables ext regs).");
    clog("");

    dump_section_A_vga_status();
    uint8_t sr06_before = 0, sr06_after = 0;
    dump_section_B_cirrus_unlock(&sr06_before, &sr06_after);
    uint8_t chip_id = 0;
    dump_section_C_crtc(&chip_id);
    dump_section_D_sr();
    dump_section_E_gr();
    dump_section_F_vbe_info();
    dump_section_G_current_mode();
    dump_section_H_pci();

    clog("");
    clog("=== Summary ===");
    clog("  CRTC[0x27] chip-id        = 0x%02X", chip_id);
    clog("  SR[0x06] unlock readback  = 0x%02X (0x12 = engaged Cirrus ext)",
         sr06_after);
    clog("  GR[0x0B] errata bits 1/4  = 0x%02X (must clear bits 1 + 4 for BLT)",
         gr_read(0x0B));
    clog("  GR[0x30] BLT mode at probe= 0x%02X (idle state -- 0x00 expected)",
         gr_read(0x30));
    clog("  GR[0x31] BLT status       = 0x%02X (bit 0 = BUSY, bit 3 = PROGRESS)",
         gr_read(0x31));

    int cirrus_signal = (chip_id == 0xA0 || chip_id == 0xA8 || chip_id == 0xAC ||
                        chip_id == 0xB8 || sr06_after == 0x12);
    clog("");
    if (cirrus_signal) {
        clog("=== HEADLINE: Cirrus engagement signal PRESENT ===");
        clog("  -> If BLTFILL.LOG also shows engagement but BLT hung anyway, the");
        clog("     issue is in BLT register encoding, not chip identification.");
        clog("     Cross-reference GR[0x10..0x33] dump above with the wave-19");
        clog("     cirrus audit (/tmp/wave19-cirrus-audit.md).");
    } else {
        clog("=== HEADLINE: NO Cirrus engagement signal ===");
        clog("  -> CRTC[0x27] not in {0xA0,0xA8,0xAC,0xB8} AND SR[0x06] readback != 0x12.");
        clog("     Either chip is not Cirrus 543x, OR a TSR / VBE BIOS is intercepting");
        clog("     port reads. Check Section F's OEM string for 'SciTech' / 'UNIVBE'");
        clog("     interception markers.");
    }
    clog("");
    clog("=== CHIPID done ===");

    if (g_log) fclose(g_log);
    return 0;
}
