/*
 * hwlog.c — One-shot DOS hardware introspection dump.
 *
 * Phase 9 wave 20 task #7. Captures everything we can read about the
 * graphics subsystem from a DOS-side probe — VBE controller info, current
 * mode + a battery of common ModeInfo blocks, Cirrus chip revision, full
 * CRTC register dump, and PCI config space for any VGA-class device. The
 * goal: archive once per machine, reference forever. Future iters can
 * diff against this baseline to spot mid-session register drift, or use
 * it to validate an assumption ("is this really mode 0x101?", "does the
 * chip actually report itself as 5434 not 5430?").
 *
 * Output: C:\HWLOG.LOG (fsync per line). Falls back to ./HWLOG.LOG if
 * C:\ is read-only.
 *
 * Pure DJGPP. Uses __dpmi_int for INT 10h (VBE) and INT 1Ah (PCI BIOS),
 * and outportb/inportb for chip-direct CRTC reads. No SDL.
 *
 * 8.3 DOS filename: HWLOG.EXE (5.3) — fits per memory/dos_filename_8_3.md.
 *
 * Build: `make hwlog` (or `make probes`).
 * Smoke under DOSBox-X for correctness only — DOSBox-X's emulated PCI
 * tree and VBE OEM strings differ from real Mach64/Cirrus/etc, so the
 * actual values are real-HW-only.
 *
 * License: MIT.
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
#include <unistd.h>

/* ============================================================ */
/* Logging                                                      */
/* ============================================================ */

static FILE *g_log = NULL;

static void hlog(const char *fmt, ...)
{
    char buf[1024];
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

static void open_log(void)
{
    g_log = fopen("C:\\HWLOG.LOG", "w");
    if (!g_log) {
        g_log = fopen("HWLOG.LOG", "w");
    }
}

/* Hex dump n bytes of `buf` to log, 16 per line, with a per-line offset. */
static void hex_dump(const uint8_t *buf, int n, const char *prefix)
{
    char line[128];
    for (int i = 0; i < n; i += 16) {
        int len = 0;
        len += snprintf(line + len, sizeof line - len, "%s %03X:", prefix, i);
        for (int j = 0; j < 16 && i + j < n; j++) {
            len += snprintf(line + len, sizeof line - len, " %02X", buf[i + j]);
        }
        hlog("%s", line);
    }
}

/* ============================================================ */
/* VBE controller info + mode info                              */
/* ============================================================ */

/* INT 10h AX=4F00h — VBE controller info. Pulls a 256-byte VBE block
 * with VRAM size, OEM strings, video-mode-list pointer. */
static int dump_vbe_info(uint16_t *out_oem_modes_seg, uint16_t *out_oem_modes_ofs)
{
    unsigned long tbuf = __tb;

    /* Write "VBE2" signature so VBE 2.0+ BIOS returns extended OEM strings. */
    _farpokeb(_dos_ds, tbuf + 0, 'V');
    _farpokeb(_dos_ds, tbuf + 1, 'B');
    _farpokeb(_dos_ds, tbuf + 2, 'E');
    _farpokeb(_dos_ds, tbuf + 3, '2');

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00;
    r.x.di = tbuf & 0xF;
    r.x.es = (tbuf >> 4) & 0xFFFF;

    if (__dpmi_int(0x10, &r) < 0) {
        hlog("VBE-INFO: __dpmi_int failed");
        return -1;
    }
    if (r.x.ax != 0x004F) {
        hlog("VBE-INFO: AX=%04X (expected 004F)  status=fail", r.x.ax);
        return -1;
    }

    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = _farpeekb(_dos_ds, tbuf + i);

    uint16_t ver       = *(const uint16_t *)(buf + 0x04);
    uint16_t oem_ofs   = *(const uint16_t *)(buf + 0x06);
    uint16_t oem_seg   = *(const uint16_t *)(buf + 0x08);
    uint32_t cap       = *(const uint32_t *)(buf + 0x0A);
    uint16_t modes_ofs = *(const uint16_t *)(buf + 0x0E);
    uint16_t modes_seg = *(const uint16_t *)(buf + 0x10);
    uint16_t total64k  = *(const uint16_t *)(buf + 0x12);

    /* OEM software vendor / product / revision strings (VBE >= 2.0). */
    uint16_t v_ofs = *(const uint16_t *)(buf + 0x16);
    uint16_t v_seg = *(const uint16_t *)(buf + 0x18);
    uint16_t p_ofs = *(const uint16_t *)(buf + 0x1A);
    uint16_t p_seg = *(const uint16_t *)(buf + 0x1C);
    uint16_t rev_ofs = *(const uint16_t *)(buf + 0x1E);
    uint16_t rev_seg = *(const uint16_t *)(buf + 0x20);

    hlog("");
    hlog("=== VBE CONTROLLER INFO (INT 10h AX=4F00) ===");
    hlog("Signature   : %.4s", (const char *)buf);
    hlog("Version     : %d.%d", (ver >> 8) & 0xFF, ver & 0xFF);
    hlog("Capabilities: 0x%08lX", (unsigned long)cap);
    hlog("  bit0 (DAC switchable 6/8 bit): %d", (cap & 0x01) ? 1 : 0);
    hlog("  bit1 (controller is non-VGA) : %d", (cap & 0x02) ? 1 : 0);
    hlog("  bit2 (snow-free DAC writes)  : %d", (cap & 0x04) ? 1 : 0);
    hlog("  bit3 (stereo support)        : %d", (cap & 0x08) ? 1 : 0);
    hlog("Total VRAM  : %u KB (%u 64K blocks)",
         (unsigned)total64k * 64, (unsigned)total64k);

    /* Read OEM string. */
    uint32_t oem_lin = ((uint32_t)oem_seg << 4) + oem_ofs;
    char tmp[256];
    for (int i = 0; i < (int)sizeof tmp - 1; i++) {
        char c = (char)_farpeekb(_dos_ds, oem_lin + i);
        tmp[i] = c;
        if (c == 0) break;
    }
    tmp[sizeof tmp - 1] = 0;
    hlog("OEM string  : '%s'", tmp);

    if ((ver >> 8) >= 2) {
        uint32_t v_lin = ((uint32_t)v_seg << 4) + v_ofs;
        for (int i = 0; i < (int)sizeof tmp - 1; i++) {
            char c = (char)_farpeekb(_dos_ds, v_lin + i);
            tmp[i] = c;
            if (c == 0) break;
        }
        tmp[sizeof tmp - 1] = 0;
        hlog("OEM vendor  : '%s'", tmp);

        uint32_t p_lin = ((uint32_t)p_seg << 4) + p_ofs;
        for (int i = 0; i < (int)sizeof tmp - 1; i++) {
            char c = (char)_farpeekb(_dos_ds, p_lin + i);
            tmp[i] = c;
            if (c == 0) break;
        }
        tmp[sizeof tmp - 1] = 0;
        hlog("OEM product : '%s'", tmp);

        uint32_t r_lin = ((uint32_t)rev_seg << 4) + rev_ofs;
        for (int i = 0; i < (int)sizeof tmp - 1; i++) {
            char c = (char)_farpeekb(_dos_ds, r_lin + i);
            tmp[i] = c;
            if (c == 0) break;
        }
        tmp[sizeof tmp - 1] = 0;
        hlog("OEM revision: '%s'", tmp);
    }

    hlog("Mode list   : %04X:%04X (linear=0x%08lX)",
         modes_seg, modes_ofs,
         (unsigned long)(((uint32_t)modes_seg << 4) + modes_ofs));

    /* Dump first 64 bytes of the raw block as hex for archival. */
    hlog("Raw bytes 0x00-0x3F:");
    hex_dump(buf, 64, "  vbe");

    *out_oem_modes_seg = modes_seg;
    *out_oem_modes_ofs = modes_ofs;
    return 0;
}

/* INT 10h AX=4F03h — get current VBE mode. Returns the VBE mode number
 * in BX, or -1 on failure. Bit 14 of BX is the LFB-flag; bit 15 the
 * preserve-content flag. We strip both for the lookup. */
static int get_current_mode(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F03;
    if (__dpmi_int(0x10, &r) < 0) return -1;
    if (r.x.ax != 0x004F) return -1;
    return r.x.bx & 0x3FFF;
}

/* INT 10h AX=4F01h — VBE ModeInfo for a given mode. Returns a 256-byte
 * block; we log the interesting fields. */
static int dump_mode_info(uint16_t mode)
{
    unsigned long tbuf = __tb;
    /* Zero the buffer first (some BIOSes only fill defined fields). */
    for (int i = 0; i < 256; i++) _farpokeb(_dos_ds, tbuf + i, 0);

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = mode;
    r.x.di = tbuf & 0xF;
    r.x.es = (tbuf >> 4) & 0xFFFF;

    if (__dpmi_int(0x10, &r) < 0) {
        hlog("MODEINFO mode=0x%04X: __dpmi_int failed", mode);
        return -1;
    }
    if (r.x.ax != 0x004F) {
        hlog("MODEINFO mode=0x%04X: AX=%04X (mode not supported)", mode, r.x.ax);
        return -1;
    }

    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = _farpeekb(_dos_ds, tbuf + i);

    uint16_t attrs    = *(const uint16_t *)(buf + 0x00);
    uint16_t winA_seg = *(const uint16_t *)(buf + 0x08);
    uint16_t bytes_pl = *(const uint16_t *)(buf + 0x10);
    uint16_t xres     = *(const uint16_t *)(buf + 0x12);
    uint16_t yres     = *(const uint16_t *)(buf + 0x14);
    uint8_t  bpp      = buf[0x19];
    uint8_t  memmodel = buf[0x1B];
    uint32_t lfb_phys = *(const uint32_t *)(buf + 0x28);

    const char *mm_str = "unknown";
    switch (memmodel) {
    case 0: mm_str = "text"; break;
    case 1: mm_str = "CGA"; break;
    case 2: mm_str = "Hercules"; break;
    case 3: mm_str = "planar"; break;
    case 4: mm_str = "packed-pixel"; break;
    case 5: mm_str = "non-chain-4"; break;
    case 6: mm_str = "directcolor"; break;
    case 7: mm_str = "YUV"; break;
    }

    hlog("MODE 0x%04X: %dx%dx%dbpp memmodel=%d(%s) attrs=0x%04X bytes/line=%d winA=0x%04X lfb=0x%08lX",
         mode, xres, yres, bpp, memmodel, mm_str, attrs, bytes_pl, winA_seg,
         (unsigned long)lfb_phys);
    hlog("  attrs:  hw_supported=%d color=%d graphics=%d non_VGA=%d banked=%d lfb=%d",
         (attrs & 0x01) ? 1 : 0,
         (attrs & 0x08) ? 1 : 0,
         (attrs & 0x10) ? 1 : 0,
         (attrs & 0x20) ? 1 : 0,
         (attrs & 0x40) == 0 ? 1 : 0,    /* bit6: 1 = no banked window */
         (attrs & 0x80) ? 1 : 0);
    return 0;
}

/* ============================================================ */
/* CRTC + Cirrus chip-revision register dump                    */
/* ============================================================ */

#define CRTC_INDEX_COLOR  0x3D4
#define CRTC_DATA_COLOR   0x3D5
#define CRTC_INDEX_MONO   0x3B4
#define CRTC_DATA_MONO    0x3B5
#define MISC_OUTPUT_R     0x3CC
#define SEQ_INDEX         0x3C4
#define SEQ_DATA          0x3C5
#define GR_INDEX          0x3CE
#define GR_DATA           0x3CF
#define ATC_INDEX         0x3C0
#define ATC_DATA_R        0x3C1

/* MISC OUTPUT bit 0: 1 = color (CRTC at 3D4); 0 = mono (CRTC at 3B4). */
static void detect_crtc_ports(uint16_t *idx, uint16_t *dat)
{
    uint8_t misc = inportb(MISC_OUTPUT_R);
    if (misc & 0x01) {
        *idx = CRTC_INDEX_COLOR;
        *dat = CRTC_DATA_COLOR;
    } else {
        *idx = CRTC_INDEX_MONO;
        *dat = CRTC_DATA_MONO;
    }
}

static uint8_t crtc_read(uint16_t idx_port, uint16_t dat_port, uint8_t idx)
{
    outportb(idx_port, idx);
    return inportb(dat_port);
}

static uint8_t seq_read(uint8_t idx)
{
    outportb(SEQ_INDEX, idx);
    return inportb(SEQ_DATA);
}

static uint8_t gr_read(uint8_t idx)
{
    outportb(GR_INDEX, idx);
    return inportb(GR_DATA);
}

static void dump_chip_regs(void)
{
    hlog("");
    hlog("=== CHIP-DIRECT REGISTER DUMP ===");

    uint8_t misc = inportb(MISC_OUTPUT_R);
    hlog("MISC OUTPUT (0x3CC) = 0x%02X (color=%d, ext_clk_sel=%d, page=%d)",
         misc, (misc & 0x01) ? 1 : 0,
         (misc >> 2) & 0x03, (misc >> 5) & 0x01);

    uint16_t crtc_idx, crtc_dat;
    detect_crtc_ports(&crtc_idx, &crtc_dat);
    hlog("CRTC ports  : idx=0x%X data=0x%X", crtc_idx, crtc_dat);

    /* Unlock Cirrus extension registers. SR06=0x12 unlocks; any other
     * value re-locks. We restore SR06 at the end. SR06 is at 0x3C5
     * after writing 0x06 to 0x3C4. Side-effect-free if not Cirrus. */
    uint8_t sr06_orig = seq_read(0x06);
    outportb(SEQ_INDEX, 0x06);
    outportb(SEQ_DATA,  0x12);

    /* Standard CRTC registers 0x00..0x18 (25 regs). */
    hlog("CRTC standard registers (idx 0x00-0x18):");
    for (int i = 0; i <= 0x18; i++) {
        uint8_t v = crtc_read(crtc_idx, crtc_dat, (uint8_t)i);
        hlog("  CRTC[0x%02X] = 0x%02X", i, v);
    }

    /* Cirrus 5434 extension CRTC registers — chip-revision lives at 0x27.
     * Other Cirrus-specific indices: 0x19/0x1A/0x1B (interlace), 0x1D
     * (overlay extra bits), 0x25 (display-page lower), 0x27 (chip ID). */
    hlog("CRTC extension registers (likely Cirrus, 0x19-0x27):");
    for (int i = 0x19; i <= 0x27; i++) {
        uint8_t v = crtc_read(crtc_idx, crtc_dat, (uint8_t)i);
        hlog("  CRTC[0x%02X] = 0x%02X%s", i, v,
             (i == 0x27) ? "  <-- chip ID/revision (Cirrus 543x)" : "");
    }

    /* Decode CRTC[0x27] if it looks like a Cirrus chip ID. The Cirrus
     * 5434 datasheet documents the high 6 bits as the chip-class ID:
     *   5430 = 0xA0 | rev   (101000xx)
     *   5434 = 0xA8 | rev   (101010xx)
     *   5436 = 0xAC | rev   (101011xx)
     *   5446 = 0xB8 | rev   (101110xx)
     * If we see one of these, name it. */
    uint8_t chip27 = crtc_read(crtc_idx, crtc_dat, 0x27);
    const char *chip_name = "(unknown / not Cirrus 543x)";
    switch (chip27 & 0xFC) {
    case 0xA0: chip_name = "Cirrus CL-GD5430"; break;
    case 0xA8: chip_name = "Cirrus CL-GD5434"; break;
    case 0xAC: chip_name = "Cirrus CL-GD5436"; break;
    case 0xB8: chip_name = "Cirrus CL-GD5446"; break;
    default: break;
    }
    hlog("Chip ID (CRTC[0x27]=0x%02X): %s  rev=%d",
         chip27, chip_name, chip27 & 0x03);

    /* Sequencer registers 0x00..0x07 (standard) + 0x08..0x1F (often
     * extension on Cirrus). */
    hlog("Sequencer registers (idx 0x00-0x1F):");
    for (int i = 0; i <= 0x1F; i++) {
        uint8_t v = seq_read((uint8_t)i);
        hlog("  SEQ[0x%02X]  = 0x%02X", i, v);
    }

    /* Graphics controller registers 0x00..0x08 (standard) + 0x09..0x3F
     * (Cirrus BLT-engine extension space). */
    hlog("Graphics controller registers (idx 0x00-0x18):");
    for (int i = 0; i <= 0x18; i++) {
        uint8_t v = gr_read((uint8_t)i);
        hlog("  GR[0x%02X]   = 0x%02X", i, v);
    }
    hlog("Graphics extension registers (idx 0x20-0x33, Cirrus BLT engine):");
    for (int i = 0x20; i <= 0x33; i++) {
        uint8_t v = gr_read((uint8_t)i);
        hlog("  GR[0x%02X]   = 0x%02X", i, v);
    }

    /* Restore SR06 unlock state to whatever we found it at. */
    outportb(SEQ_INDEX, 0x06);
    outportb(SEQ_DATA,  sr06_orig);
}

/* ============================================================ */
/* PCI BIOS — find VGA-class device, dump 256 bytes config space */
/* ============================================================ */

/* INT 1Ah AX=B101h — PCI BIOS installation check.
 * On success: AH=0, AL=config mech bits, BH=major BCD, BL=minor BCD,
 *             CL=last PCI bus, EDX='PCI ' tag. */
static int pci_bios_check(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB101;
    if (__dpmi_int(0x1A, &r) < 0) {
        hlog("PCI-BIOS: __dpmi_int failed");
        return -1;
    }
    if (r.h.ah != 0) {
        hlog("PCI-BIOS: AH=%02X (not present)", r.h.ah);
        return -1;
    }
    hlog("PCI-BIOS: present  ver=%d.%d  mech=0x%02X  last_bus=%d",
         r.h.bh, r.h.bl, r.h.al, r.h.cl);
    return 0;
}

/* INT 1Ah AX=B103h — find PCI device by class code. Returns BH:bus,
 * BL:dev<<3|func on success, -1 if no VGA device found. */
static int pci_find_vga(uint8_t *out_bus, uint8_t *out_devfn)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB103;
    /* ECX = 24-bit class code. 0x030000 = VGA-compatible controller
     * (base class 0x03, subclass 0x00, prog interface 0x00). */
    r.d.ecx = 0x00030000UL;
    r.x.si = 0;  /* index — we want the first match */
    if (__dpmi_int(0x1A, &r) < 0) return -1;
    if (r.h.ah != 0) {
        hlog("PCI-FIND VGA (class 0x030000): AH=%02X (no match)", r.h.ah);
        return -1;
    }
    *out_bus   = r.h.bh;
    *out_devfn = r.h.bl;
    return 0;
}

/* Read one config-space byte at (bus, devfn, offset) via INT 1Ah B108. */
static int pci_read_byte(uint8_t bus, uint8_t devfn, uint16_t offset, uint8_t *out)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB108;
    r.h.bh = bus;
    r.h.bl = devfn;
    r.x.di = offset;
    if (__dpmi_int(0x1A, &r) < 0) return -1;
    if (r.h.ah != 0) return -1;
    *out = r.h.cl;
    return 0;
}

static void dump_pci_vga(void)
{
    hlog("");
    hlog("=== PCI CONFIG SPACE (VGA-class device) ===");

    if (pci_bios_check() < 0) {
        hlog("PCI: no PCI BIOS — skipping config-space dump");
        return;
    }

    uint8_t bus, devfn;
    if (pci_find_vga(&bus, &devfn) < 0) {
        hlog("PCI: no VGA-class device — skipping config-space dump");
        return;
    }

    hlog("PCI: VGA found at bus=%d  dev=%d  fn=%d",
         bus, devfn >> 3, devfn & 0x07);

    /* Read 256 bytes of config space. */
    uint8_t cfg[256];
    memset(cfg, 0xFF, sizeof cfg);
    int n_read = 0;
    for (int i = 0; i < 256; i++) {
        if (pci_read_byte(bus, devfn, (uint16_t)i, &cfg[i]) == 0) {
            n_read++;
        } else {
            cfg[i] = 0xFF;  /* mark unreadable */
        }
    }
    hlog("PCI: %d/256 config bytes read", n_read);

    uint16_t vendor = *(const uint16_t *)(cfg + 0x00);
    uint16_t device = *(const uint16_t *)(cfg + 0x02);
    uint16_t cmd    = *(const uint16_t *)(cfg + 0x04);
    uint16_t status = *(const uint16_t *)(cfg + 0x06);
    uint8_t  rev    = cfg[0x08];
    uint8_t  classp = cfg[0x09];   /* prog interface */
    uint8_t  classs = cfg[0x0A];   /* subclass */
    uint8_t  classb = cfg[0x0B];   /* base class */
    uint8_t  hdr    = cfg[0x0E];
    uint32_t bar0   = *(const uint32_t *)(cfg + 0x10);
    uint32_t bar1   = *(const uint32_t *)(cfg + 0x14);
    uint32_t bar2   = *(const uint32_t *)(cfg + 0x18);
    uint32_t bar3   = *(const uint32_t *)(cfg + 0x1C);
    uint32_t bar4   = *(const uint32_t *)(cfg + 0x20);
    uint32_t bar5   = *(const uint32_t *)(cfg + 0x24);
    uint16_t subv   = *(const uint16_t *)(cfg + 0x2C);
    uint16_t subd   = *(const uint16_t *)(cfg + 0x2E);

    /* Vendor decode for the chips we know about. */
    const char *vname = "(unknown)";
    if (vendor == 0x1013) vname = "Cirrus Logic";
    else if (vendor == 0x1002) vname = "ATI Technologies";
    else if (vendor == 0x102B) vname = "Matrox";
    else if (vendor == 0x10DE) vname = "NVIDIA";
    else if (vendor == 0x121A) vname = "3dfx";
    else if (vendor == 0x5333) vname = "S3 Graphics";
    else if (vendor == 0x100C) vname = "Tseng Labs";

    hlog("PCI: vendor=0x%04X (%s)  device=0x%04X  rev=0x%02X",
         vendor, vname, device, rev);
    hlog("PCI: class=%02X.%02X.%02X  hdr=0x%02X", classb, classs, classp, hdr);
    hlog("PCI: cmd=0x%04X  status=0x%04X", cmd, status);
    hlog("PCI:   cmd:io=%d mem=%d busmaster=%d  status:cap_list=%d 66mhz=%d",
         (cmd & 0x01) ? 1 : 0, (cmd & 0x02) ? 1 : 0, (cmd & 0x04) ? 1 : 0,
         (status & 0x10) ? 1 : 0, (status & 0x20) ? 1 : 0);
    hlog("PCI: BAR0=0x%08lX  BAR1=0x%08lX  BAR2=0x%08lX",
         (unsigned long)bar0, (unsigned long)bar1, (unsigned long)bar2);
    hlog("PCI: BAR3=0x%08lX  BAR4=0x%08lX  BAR5=0x%08lX",
         (unsigned long)bar3, (unsigned long)bar4, (unsigned long)bar5);
    hlog("PCI: subsys vendor=0x%04X  subsys device=0x%04X", subv, subd);

    hlog("PCI: full 256-byte config space:");
    hex_dump(cfg, 256, "  pci");
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    hlog("=== HWLOG wave-20 task #7 (P0) starting ===");
    hlog("DJGPP build, target = real-HW DOS introspection");

    /* VBE controller info. */
    uint16_t modes_seg = 0, modes_ofs = 0;
    dump_vbe_info(&modes_seg, &modes_ofs);

    /* Current mode + a battery of common ModeInfo blocks. */
    int cur = get_current_mode();
    hlog("");
    hlog("=== CURRENT MODE + COMMON MODEINFO ===");
    if (cur >= 0) {
        hlog("Current mode (INT 10h AX=4F03h): 0x%04X", cur);
        if (cur >= 0x100) {
            dump_mode_info((uint16_t)cur);
        } else {
            hlog("  (mode < 0x100 — likely a legacy text/EGA mode; ModeInfo not VBE-applicable)");
        }
    } else {
        hlog("Current mode query failed");
    }

    /* Common 8bpp + 16bpp + 24bpp modes the engine might pick. The DOS
     * port runtime-locks to 320x240; SDL3-DOS picks 0x101 (640x480x8) on
     * Mach64/Cirrus per memory/mach64_real_hw_state.md.  */
    static const uint16_t common_modes[] = {
        0x100,  /* 640x400x8 */
        0x101,  /* 640x480x8 */
        0x103,  /* 800x600x8 */
        0x105,  /* 1024x768x8 */
        0x110,  /* 640x480x15 */
        0x111,  /* 640x480x16 */
        0x112,  /* 640x480x24 */
        0x114,  /* 800x600x16 */
        0x117,  /* 1024x768x16 */
        0x132,  /* SDL3-DOS-tested 320x200x8 (sometimes) */
        0x152,  /* SDL3-DOS-tested 320x240x8 (sometimes) */
        0
    };
    for (int i = 0; common_modes[i]; i++) {
        if ((int)common_modes[i] != cur) {
            dump_mode_info(common_modes[i]);
        }
    }

    /* Chip-direct register dump. */
    dump_chip_regs();

    /* PCI config space for any VGA-class device. */
    dump_pci_vga();

    hlog("");
    hlog("=== HWLOG done ===");

    if (g_log) fclose(g_log);
    return 0;
}
