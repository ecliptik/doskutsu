/*
 * profile.c -- host-system detection for SETUP.EXE.
 *
 * DOS detection (CPUID, PIT timing, DPMI memory, BLASTER, VBE, OPL3,
 * MPU-401) is compiled under __DJGPP__. On a host compiler the same
 * profile_detect() returns a representative profile so recommend.c and the
 * UI logic stay testable off-target. ASCII-only.
 *
 * Scope note: the low-level audio port probes here are intentionally
 * conservative (BLASTER type + a light OPL3 timer test + an MPU-401 reset
 * ack). The aggressive WaveBlaster byte-path detection lives in the
 * validated probe binaries (tests/probes); SETUP lets the user confirm /
 * override the detected sound config (recommend-and-confirm).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>  /* mkdir: create the LOGS dir for the profile dump   */
#include <unistd.h>    /* fsync: DOS per-line durability (audiotest idiom)   */

#include "profile.h"

const char *cpu_class_name(cpu_class_t c)
{
  switch (c)
  {
    case CPU_586:      return "Pentium-class";
    case CPU_486_MID:  return "486DX2-66 class";
    case CPU_486_SLOW: return "486DX2-50 or slower";
    default:           return "unknown CPU";
  }
}

/* Dump the detected profile to LOGS\PROFILE.LOG so a real-HW iter captures the
 * displayed values deterministically (the 486 MHz-divisor calibration + the
 * physical-RAM acceptance become log-witnessed instead of hand-transcribed
 * from the screen). One key=value per line, each flushed + fsync'd before the
 * next so a hard freeze cannot lose the tail -- the same DOS durability idiom
 * the audio-test trace uses (nx 0036 / SDL/0024). Unconditional + tiny; the
 * LOGS\ subdir + CWD convention match the engine + realhw's logback probe
 * path. Returns 0 on success, -1 if the file cannot be opened. */
int profile_write_log(const sysprofile_t *p)
{
  FILE *fp;
  mkdir("LOGS", 0777);            /* defensive: harmless if it already exists */
  fp = fopen("LOGS/PROFILE.LOG", "wb");
  if (!fp) return -1;
#define PL(...) do { fprintf(fp, __VA_ARGS__); fputc('\n', fp); \
                     fflush(fp); fsync(fileno(fp)); } while (0)
  PL("cpu_class=%d (%s)", (int)p->cpu_class, cpu_class_name(p->cpu_class));
  PL("cpu_desc=%s",   p->cpu_desc[0]   ? p->cpu_desc   : "unknown");
  PL("cpu_vendor=%s", p->cpu_vendor[0] ? p->cpu_vendor : "unknown");
  PL("cpu_mhz_est=%d", p->cpu_mhz_est);
  PL("has_fpu=%d", p->has_fpu);
  PL("phys_ram_kb=%ld", p->phys_ram_kb);
  PL("xms_free_kb=%ld", p->xms_free_kb);
  PL("snd_detected=%d", p->snd_detected);
  PL("snd=port 0x%X irq %d dma %d hdma %d type T%d",
     p->snd_base, p->snd_irq, p->snd_dma, p->snd_hdma, p->snd_type);
  PL("dsp=%d.%d", p->dsp_major, p->dsp_minor);
  PL("has_opl3=%d", p->has_opl3);
  PL("has_waveblaster=%d", p->has_waveblaster);
  PL("vbe=%x.%x lfb=%d",
     (p->vbe_version >> 8) & 0xff, p->vbe_version & 0xff, p->vbe_lfb);
  PL("video_desc=%s", p->video_desc[0] ? p->video_desc : "unknown");
  /* Phase 2 (DF-UX): timed video-memory fill. The KB/s is g2k-only evidence
   * (DOSBox-X scaling is meaningless per dosbox_not_proxy); the line's mere
   * presence + a non-negative value is the DOSBox witness. path records which
   * fallback measured it (2 mode13h / 3 text-B800 / 0 none). */
  PL("video_speed_kbs=%d", p->video_speed_kbs);
  PL("video_speed_path=%d", p->video_speed_path);
#undef PL
  fclose(fp);
  return 0;
}

/* ---- BLASTER parse (shared host/DOS) -------------------------------- */
static void parse_blaster(sysprofile_t *p)
{
  const char *b = getenv("BLASTER");
  p->snd_detected = 0;
  p->snd_base = p->snd_irq = p->snd_dma = p->snd_hdma = p->snd_type = 0;
  if (!b) return;

  while (*b)
  {
    char c = *b++;
    int  v;
    if (c == ' ' || c == '\t') continue;
    if (c == 'A' || c == 'a') { v = (int)strtol(b, (char **)&b, 16); p->snd_base = v; }
    else if (c == 'I' || c == 'i') { v = (int)strtol(b, (char **)&b, 10); p->snd_irq = v; }
    else if (c == 'D' || c == 'd') { v = (int)strtol(b, (char **)&b, 10); p->snd_dma = v; }
    else if (c == 'H' || c == 'h') { v = (int)strtol(b, (char **)&b, 10); p->snd_hdma = v; }
    else if (c == 'T' || c == 't') { v = (int)strtol(b, (char **)&b, 10); p->snd_type = v; }
    else { while (*b && *b != ' ' && *b != '\t') ++b; }
  }
  if (p->snd_base) p->snd_detected = 1;
  /* SB16 (T6) and SB Pro (T4/5) carry an OPL3/OPL2; treat >=T4 as FM-capable */
  if (p->snd_type >= 4) p->has_opl3 = 1;
}

#if defined(__DJGPP__)

#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <dos.h>
#include <bios.h>
#include <sys/farptr.h>

/* CPUID via inline asm. Returns 1 if CPUID is supported. */
static int cpuid_supported(void)
{
  unsigned long before, after;
  __asm__ __volatile__(
    "pushfl\n\t"
    "pushfl\n\t"
    "popl %0\n\t"
    "movl %0, %1\n\t"
    "xorl $0x200000, %0\n\t"   /* flip ID bit (21) */
    "pushl %0\n\t"
    "popfl\n\t"
    "pushfl\n\t"
    "popl %0\n\t"
    "popfl\n\t"
    : "=&r"(after), "=&r"(before));
  return ((before ^ after) & 0x200000) != 0;
}

static void do_cpuid(unsigned long leaf, unsigned long *a, unsigned long *b,
                     unsigned long *c, unsigned long *d)
{
  __asm__ __volatile__("cpuid"
                       : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                       : "a"(leaf));
}

/* x87 FPU present? fninit then read status word; a real FPU leaves it 0. */
static int fpu_present(void)
{
  unsigned short sw = 0xffff;
  __asm__ __volatile__("fninit\n\t"
                       "fnstsw %0"
                       : "=m"(sw));
  return sw == 0;
}

/* TSC present? Requires CPUID and the TSC feature flag (CPUID.1:EDX bit 4).
 * True on Pentium / Pentium OverDrive; false on every 486-class part. */
static int have_tsc(void)
{
  unsigned long a, b, c, d;
  if (!cpuid_supported()) return 0;
  do_cpuid(1, &a, &b, &c, &d);
  return (d & (1UL << 4)) != 0;
}

static unsigned long long read_tsc(void)
{
  unsigned long lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)hi << 32) | lo;
}

/* A fixed chunk of volatile work for the 486 fallback calibration. `k` is
 * volatile so -O2 cannot hoist or elide the loop out of the timing window. */
/* noinline + 16-byte aligned so this loop's address/alignment is PINNED: its
 * 486 per-iteration cycle cost (and thus the MHz calibration below) no longer
 * drifts when unrelated code changes shift the binary layout. (Removing the
 * LFB bench code had shifted it and made a DX2-66 read ~78 instead of ~66.) */
static void __attribute__((noinline, aligned(16))) busy_chunk(void)
{
  volatile int k;
  for (k = 0; k < 1000; ++k) { /* volatile load/add/store/cmp/branch */ }
}

/* ~219.7 ms timing window (4 BIOS ticks at 18.2065 Hz; 1 tick = 54.925 ms). */
#define MHZ_WIN_TICKS 4
#define MHZ_WIN_US    219700UL

/* 486 fallback: empirical chunks-per-window -> MHz divisor. busy_chunk is
 * ~1000 volatile-int iterations; chunks-per-window scale with the core clock.
 * CALIBRATION: with busy_chunk now PINNED (noinline+aligned, see above) the
 * count is layout-stable, so this constant stays valid across builds. The
 * pre-pin DIV 13900 made a DX2-66 read ~78 once the loop shifted; recalibrated
 * DIV' = 13900 * 78/66 ~ 16427 -> rounded to 16400 to bring it back to ~66.
 * RE-CONFIRM the DX2-66 reading once on real HW and adjust this one constant if
 * it is not ~66 -- it will then hold (the loop no longer drifts). */
#define MHZ_486_LOOP_DIV 16400UL

/* Estimate CPU MHz.
 *
 *   Pentium-class (TSC present): EXACT -- count RDTSC cycles across the fixed
 *   BIOS-tick window. A Pentium OverDrive 83 reads ~83.
 *
 *   486-class (no TSC): a calibrated busy-loop. We count fixed-size work
 *   chunks across the same window; chunks scale with the clock. The previous
 *   version called biostime() (INT 1Ah, ~100 us in protected mode) once PER
 *   inner iteration, so it measured INT-1Ah latency rather than the CPU --
 *   which is why a DX2-66 read ~3 MHz. Here biostime() is read once per chunk
 *   (amortized ~1000:1) over a multi-tick window, so the count reflects real
 *   CPU work.
 */
static int estimate_mhz(void)
{
  long t0, t1;
  int  ticks = 0;

  /* Align to a BIOS-tick edge so the window is a whole number of ticks. */
  t0 = biostime(0, 0L);
  while ((t1 = biostime(0, 0L)) == t0) { /* spin to the edge */ }
  t0 = t1;

  if (have_tsc())
  {
    unsigned long long c0 = read_tsc(), c1;
    while (ticks < MHZ_WIN_TICKS)
    {
      t1 = biostime(0, 0L);
      if (t1 != t0) { t0 = t1; ++ticks; }
    }
    c1 = read_tsc();
    return (int)((c1 - c0) / MHZ_WIN_US);   /* cycles / us = MHz */
  }
  else
  {
    unsigned long chunks = 0;
    while (ticks < MHZ_WIN_TICKS)
    {
      busy_chunk();
      ++chunks;
      t1 = biostime(0, 0L);
      if (t1 != t0) { t0 = t1; ++ticks; }
    }
    return (int)((chunks * 1000UL) / MHZ_486_LOOP_DIV);
  }
}

static void detect_cpu(sysprofile_t *p)
{
  strcpy(p->cpu_vendor, "unknown");
  strcpy(p->cpu_desc, "");
  p->has_fpu = fpu_present();
  p->cpu_mhz_est = estimate_mhz();

  if (cpuid_supported())
  {
    unsigned long a, b, c, d;
    char vendor[13];
    unsigned family;

    do_cpuid(0, &a, &b, &c, &d);
    memcpy(vendor + 0, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';
    strncpy(p->cpu_vendor, vendor, sizeof(p->cpu_vendor) - 1);

    do_cpuid(1, &a, &b, &c, &d);
    family = (unsigned)((a >> 8) & 0xf);

    if (family >= 5)
      p->cpu_class = CPU_586;
    else
      p->cpu_class = (p->cpu_mhz_est >= 60) ? CPU_486_MID : CPU_486_SLOW;
  }
  else
  {
    /* No CPUID -> 486 or earlier. Bucket by the MHz estimate. */
    p->cpu_class = (p->cpu_mhz_est >= 60) ? CPU_486_MID : CPU_486_SLOW;
  }

  if (!p->cpu_desc[0])
    snprintf(p->cpu_desc, sizeof(p->cpu_desc), "%s ~%d MHz%s",
             cpu_class_name(p->cpu_class), p->cpu_mhz_est,
             p->has_fpu ? "" : " (NO FPU!)");
}

static void detect_memory(sysprofile_t *p)
{
  __dpmi_free_mem_info info;
  long dpmi_phys = -1;   /* RAM the DPMI host reports it manages */
  long e801_phys = -1;   /* RAM the BIOS reports is installed    */

  p->phys_ram_kb = -1;
  p->xms_free_kb = -1;

  /* DPMI free-memory info: keep largest_available_free_block as the xms_free
   * reference figure. total_number_of_physical_pages is NOT machine RAM under
   * CWSDPMI -- it reports the host's own managed/mapped page count, which on
   * g2k read 8192 pages = 32 MB on a 48 MB box. So it's only a fallback +
   * sanity floor here, not the primary source. */
  if (__dpmi_get_free_memory_information(&info) == 0)
  {
    if (info.largest_available_free_block_in_bytes != 0xFFFFFFFFUL)
      p->xms_free_kb = (long)(info.largest_available_free_block_in_bytes / 1024UL);
    if (info.total_number_of_physical_pages != 0 &&
        info.total_number_of_physical_pages != 0xFFFFFFFFUL)
      dpmi_phys = (long)(info.total_number_of_physical_pages * 4UL); /* pages x 4 KB */
  }

  /* PRIMARY: raw BIOS INT 15h AX=E801h -- the actual installed RAM.
   * AX = contiguous KB in the 1..16 MB region; BX = contiguous 64 KB blocks
   * above 16 MB. Classic E801 quirk: some BIOSes leave AX/BX zero and report
   * only in CX/DX, so fall back to CX/DX when AX is 0. Total installed RAM =
   * 1 MB base + (1..16 MB KB) + (>16 MB 64 KB blocks x 64 KB).
   * E.g. 48 MB box: AX=15360 (15 MB), BX=512 (32 MB) -> 1024+15360+32768 = 49152. */
  {
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0xE801;
    __dpmi_int(0x15, &r);
    if ((r.x.flags & 1) == 0) /* carry clear == supported */
    {
      unsigned long below16 = r.x.ax;                 /* KB           */
      unsigned long above16 = (unsigned long)r.x.bx;  /* 64 KB blocks */
      if (below16 == 0) { below16 = r.x.cx; above16 = r.x.dx; } /* CX/DX fallback */
      if (below16 || above16)
      {
        unsigned long kb = 1024UL + below16 + above16 * 64UL;
        /* plausibility guard: 1 MB .. 512 MB (period hardware) -- rejects a
         * garbage E801 (e.g. 0xFFFF blocks) so we fall back to DPMI instead. */
        if (kb >= 1024UL && kb <= 0x80000UL)
          e801_phys = (long)kb;
      }
    }
  }

  /* Prefer the BIOS figure; fall back to the DPMI page count if E801 is
   * absent/implausible. Sanity-clamp UP to the DPMI figure -- installed RAM is
   * at least what the host actually mapped, so a low/bogus E801 can't under-
   * report below the DPMI pool. */
  p->phys_ram_kb = (e801_phys > 0) ? e801_phys : dpmi_phys;
  if (dpmi_phys > 0 && p->phys_ram_kb < dpmi_phys)
    p->phys_ram_kb = dpmi_phys;
}

/* VBE controller info via real-mode INT 10h AX=4F00h into a transfer
 * buffer in conventional memory (__tb). */
static void detect_video(sysprofile_t *p)
{
  __dpmi_regs r;
  unsigned long tb = __tb & 0xfffff;

  p->vbe_version = 0;
  p->vbe_lfb     = 0;
  strcpy(p->video_desc, "VGA/VBE (unknown)");

  /* "VBE2" signature request for VBE 2.0+ extended info */
  dosmemput("VBE2", 4, tb);
  memset(&r, 0, sizeof(r));
  r.x.ax = 0x4f00;
  r.x.es = (unsigned short)(tb >> 4);
  r.x.di = (unsigned short)(tb & 0xf);
  __dpmi_int(0x10, &r);

  if (r.x.ax == 0x004f)
  {
    unsigned char buf[64];
    dosmemget(tb, sizeof(buf), buf);
    if (memcmp(buf, "VESA", 4) == 0)
    {
      p->vbe_version = (buf[5] << 8) | buf[4];      /* BCD version */
      /* OEM string pointer at offset 6 (seg:off real-mode far ptr) */
      {
        unsigned short off = (unsigned short)(buf[6] | (buf[7] << 8));
        unsigned short seg = (unsigned short)(buf[8] | (buf[9] << 8));
        unsigned long  lin = ((unsigned long)seg << 4) + off;
        char oem[40];
        int  i;
        dosmemget(lin & 0xfffff, sizeof(oem), oem);
        for (i = 0; i < (int)sizeof(oem) - 1 && oem[i]; ++i) { }
        oem[i] = '\0';
        if (oem[0])
        {
          size_t vn = strlen(oem);
          if (vn >= sizeof(p->video_desc)) vn = sizeof(p->video_desc) - 1;
          memcpy(p->video_desc, oem, vn);
          p->video_desc[vn] = '\0';
        }
      }
      p->vbe_lfb = (p->vbe_version >= 0x0200); /* LFB modes exist in VBE 2.0+ */
    }
  }
}

/* OPL3 presence: classic Adlib timer test on the FM base (0x388, or the
 * card's base+8). Reset timers, set timer 1, poll status for the overflow
 * bits. Conservative: only sets has_opl3 on a clean pass. */
static int opl_detect(int fm_base)
{
  unsigned char s1, s2;
  int i;

  outportb(fm_base, 0x04); outportb(fm_base + 1, 0x60); /* reset T1,T2 */
  outportb(fm_base, 0x04); outportb(fm_base + 1, 0x80); /* reset IRQ   */
  s1 = inportb(fm_base) & 0xe0;
  outportb(fm_base, 0x02); outportb(fm_base + 1, 0xff); /* T1 = 0xff   */
  outportb(fm_base, 0x04); outportb(fm_base + 1, 0x21); /* start T1    */
  for (i = 0; i < 200; ++i) inportb(fm_base);           /* short delay */
  s2 = inportb(fm_base) & 0xe0;
  outportb(fm_base, 0x04); outportb(fm_base + 1, 0x60);
  outportb(fm_base, 0x04); outportb(fm_base + 1, 0x80);
  return (s1 == 0x00) && (s2 == 0xc0);
}

/* SB16 DSP version via command 0xE1 (the third dsp=0.0 sighting fix). The
 * profiler declared + logged dsp_major/dsp_minor but never populated them, so
 * PROFILE.LOG always read "dsp=0.0". Standard SB16 DSP I/O on the BLASTER base
 * port: reset the DSP, send Get-DSP-Version (0xE1), read major.minor. Every
 * poll is bounded -- a non-responding DSP simply leaves the version at 0.0,
 * it never hangs. DSP ports: base+0x6 reset, base+0xA read-data, base+0xC
 * write-command (read bit7 = write-busy), base+0xE read-status (bit7 = data
 * available). g2k (SB16 PnP, DSP v4.13) should now log dsp=4.13. */
static void dsp_version(sysprofile_t *p)
{
  int base = p->snd_base;
  int i, ready = 0;

  if (base <= 0)
    return;

  /* DSP reset: 1 -> reset port, >=3 us settle, 0 -> reset port, expect 0xAA. */
  outportb(base + 0x6, 1);
  for (i = 0; i < 64; ++i) (void)inportb(base + 0x6); /* settle delay */
  outportb(base + 0x6, 0);
  for (i = 0; i < 4000; ++i)
  {
    if (inportb(base + 0xE) & 0x80) /* data available */
    {
      ready = (inportb(base + 0xA) == 0xAA); /* consume the one byte */
      break;
    }
  }
  if (!ready)
    return; /* no DSP responding at this base */

  /* Get-DSP-Version (0xE1) once the write port is ready. */
  for (i = 0; i < 4000; ++i)
    if ((inportb(base + 0xC) & 0x80) == 0)
      break;
  outportb(base + 0xC, 0xE1);

  /* read major then minor, each gated on read-buffer-status bit 7. */
  for (i = 0; i < 4000; ++i)
    if (inportb(base + 0xE) & 0x80) { p->dsp_major = inportb(base + 0xA); break; }
  for (i = 0; i < 4000; ++i)
    if (inportb(base + 0xE) & 0x80) { p->dsp_minor = inportb(base + 0xA); break; }
}

/* MPU-401 (WaveBlaster header) presence: reset via the command port and
 * look for an ACK (0xFE) on the data port. Conservative; the user can
 * override in the UI. Default MPU base 0x330. */
static int mpu401_present(int base)
{
  int i;
  outportb(base + 1, 0xff); /* UART reset command */
  for (i = 0; i < 1000; ++i)
  {
    if ((inportb(base + 1) & 0x80) == 0) /* DSR: data available */
    {
      if (inportb(base) == 0xfe) return 1; /* ACK */
    }
  }
  return 0;
}

/* ---- Video Speed benchmark (DF-UX Phase 2) -------------------------------
 *
 * Timed REP STOSL fill of video memory -> KB/s, the "Machine Video Speed"
 * figure (mirrors DF's installer speed read). Two paths, primary-then-fallback:
 *   (2) mode 13h A000 -- set mode 13h (universal 320x200x8 VGA), fill the
 *       64000-byte A000 page bounded by a BIOS-tick window, restore text mode 3
 *       (one brief visible flash). Same banked-write class the game uses for its
 *       banked copy-present, proven safe on the S3 ViRGE. This is the PRIMARY.
 *   (3) text B800 fill -- no mode change, bus-width-only (NOT representative);
 *       last resort if the mode 13h set fails.
 * The old VBE-LFB path (path 1: a scattered 0x4F02 linear-framebuffer fill
 * during scanout) was REMOVED -- it wedged the S3 ViRGE memory controller, a
 * HW-IO failure DOSBox cannot reproduce, so it cannot be timeout-guarded. Both
 * paths flat-access video RAM through __djgpp_nearptr (CWSDPMI maps the low
 * 1 MB). Integer-only (no FPU assumption -- SETUP runs on no-FPU boxes to print
 * the FPU warning), bounded by a fixed BIOS-tick window (never hangs), text mode
 * 3 restored after the graphics path. The KB/s is g2k-only evidence; under
 * DOSBox-X it just proves the path runs + a number is produced (dosbox_not_proxy).
 * Runs once at startup BEFORE tui_init, so the mode flash precedes the TUI
 * taking the screen. */

#include <sys/nearptr.h>

#define VBENCH_WIN_TICKS 4         /* ~219.7 ms fixed timing window           */
#define VBENCH_US_PER_TICK 54925ULL /* 1 BIOS tick @ 18.2065 Hz = 54.925 ms   */

/* REP STOSL `count` dwords of `val` to the flat linear address `dst` (valid as
 * a near pointer with nearptr enabled: DS base 0, ES==DS). */
static void vbench_fill(unsigned long dst, unsigned long val, unsigned long count)
{
  __asm__ __volatile__("cld\n\trep stosl"
                       : "+D"(dst), "+c"(count)
                       : "a"(val)
                       : "memory");
}

/* Fill [base_flat, +region_bytes) repeatedly for VBENCH_WIN_TICKS BIOS ticks;
 * return KB/s. region_bytes is rounded down to a dword multiple. 64-bit math so
 * the byte*1e6 product cannot overflow a 32-bit long. */
static int vbench_measure(unsigned long base_flat, unsigned long region_bytes)
{
  unsigned long dwords = region_bytes >> 2;
  unsigned long passes = 0, val = 0;
  long t0, t1;
  int  ticks = 0;
  unsigned long long total_bytes, elapsed_us;

  if (dwords == 0) return 0;
  t0 = biostime(0, 0L);
  while ((t1 = biostime(0, 0L)) == t0) { /* align to a tick edge */ }
  t0 = t1;
  while (ticks < VBENCH_WIN_TICKS)
  {
    vbench_fill(base_flat, val, dwords);
    val += 0x04040404UL;                  /* vary so it is a real fill        */
    ++passes;
    t1 = biostime(0, 0L);
    if (t1 != t0) { t0 = t1; ++ticks; }
  }
  total_bytes = (unsigned long long)passes * (unsigned long long)(dwords << 2);
  elapsed_us  = (unsigned long long)ticks * VBENCH_US_PER_TICK;
  if (elapsed_us == 0) return 0;
  return (int)((total_bytes * 1000000ULL) / elapsed_us / 1024ULL);
}

/* Set text mode 3 (80x25 color) -- restore after any graphics-mode bench. */
static void vbench_restore_text(void)
{
  __dpmi_regs r;
  memset(&r, 0, sizeof r);
  r.x.ax = 0x0003;
  __dpmi_int(0x10, &r);
}

/* Run the video-speed benchmark, filling p->video_speed_kbs + _path. */
static void detect_video_speed(sysprofile_t *p)
{
  static int s_done = 0, s_kbs = 0, s_path = 0;
  __dpmi_regs r;
  const char *vbench;

  /* Measure ONCE and cache. The result is fixed hardware, and re-running the
   * mode-13h flash mid-session (profile_detect is re-called when Express
   * re-probes) scrambles the live text screen -- the redraw afterward leaves a
   * broken box border + patchy backdrop. So the one flash happens at startup
   * (before tui_init); later calls reuse the cached value, no flash. */
  if (s_done)
  {
    p->video_speed_kbs  = s_kbs;
    p->video_speed_path = s_path;
    return;
  }
  s_done = 1;   /* whatever happens below, never re-run the flash */

  p->video_speed_kbs  = 0;
  p->video_speed_path = 0;

  /* DEFAULT ON. The old VBE-LFB path (scattered 0x4F02 linear-framebuffer fill
   * during scanout) wedged the S3 ViRGE memory controller and was REMOVED; this
   * bench uses only the safe mode-13h A000 fill (+ text-B800 fallback), the same
   * banked-write class the game's banked copy-present uses. Validated no-hang
   * with real KB/s on the S3 ViRGE, Cirrus CL-GD5430, and ATI Mach64 (operator,
   * 2026-06-19), so it runs by default (one brief mode-13h flash at startup).
   * Killswitch DOSKUTSU_SETUP_VIDEOBENCH=0 disables it -> panel "(speed n/a)". */
  vbench = getenv("DOSKUTSU_SETUP_VIDEOBENCH");
  if (vbench && strcmp(vbench, "0") == 0)
    return;   /* killswitch: bench disabled (kbs=0/path=0) */

  if (!__djgpp_nearptr_enable())
    return;   /* DPMI host forbids flat access -> no representative bench */

  /* (2) PRIMARY: mode 13h A000 -- universal 320x200x8 VGA, the same banked-
   * write class the game's banked copy-present uses (proven safe on the S3).
   * Set mode 13h, fill the 64000-byte A000 page bounded by the tick window,
   * restore text mode 3. */
  memset(&r, 0, sizeof r);
  r.x.ax = 0x0013;
  if (__dpmi_int(0x10, &r) >= 0)
  {
    p->video_speed_kbs  = vbench_measure(0xA0000UL + __djgpp_conventional_base, 63999UL & ~3UL);
    p->video_speed_path = 2;
    vbench_restore_text();
  }

  /* (3) FALLBACK: text B800 -- bus-width only, no mode change. Last resort if
   * the mode 13h set above failed. */
  if (p->video_speed_kbs <= 0)
  {
    p->video_speed_kbs  = vbench_measure(0xB8000UL + __djgpp_conventional_base, 0x8000UL);
    p->video_speed_path = 3;
  }

  __djgpp_nearptr_disable();   /* restore memory protection */
  s_kbs  = p->video_speed_kbs;   /* cache so re-probes never re-flash the screen */
  s_path = p->video_speed_path;
}

void profile_detect(sysprofile_t *p)
{
  int fm_base;
  memset(p, 0, sizeof(*p));

  detect_cpu(p);
  detect_memory(p);
  parse_blaster(p);
  detect_video(p);

  /* OPL3 lives at FM base = sound base + 8, or the standard 0x388. */
  fm_base = p->snd_base ? (p->snd_base + 8) : 0x388;
  if (opl_detect(fm_base)) p->has_opl3 = 1;

  /* WaveBlaster MPU-401: standard intelligent-mode base 0x330. */
  if (mpu401_present(0x330)) p->has_waveblaster = 1;

  /* SB16 DSP version (0xE1) on the BLASTER base port -- fixes dsp=0.0. */
  dsp_version(p);

  /* Video Speed fill bench LAST (it leaves a mode flash; runs before tui_init
   * which retakes the screen). DF-UX Phase 2. */
  detect_video_speed(p);
}

#else /* host build: representative stub so logic stays testable */

void profile_detect(sysprofile_t *p)
{
  memset(p, 0, sizeof(*p));
  p->cpu_class = CPU_586;
  p->has_fpu = 1;
  p->cpu_mhz_est = 83;
  strcpy(p->cpu_vendor, "GenuineIntel");
  strcpy(p->cpu_desc, "Pentium OverDrive 83 (host stub)");
  p->xms_free_kb = 48L * 1024L;
  p->phys_ram_kb = 48L * 1024L;
  parse_blaster(p);
  if (!p->snd_detected)
  {
    p->snd_detected = 1; p->snd_base = 0x220; p->snd_irq = 5;
    p->snd_dma = 1; p->snd_hdma = 5; p->snd_type = 6;
  }
  p->dsp_major = 4; p->dsp_minor = 13; /* representative SB16 DSP (g2k) */
  p->has_opl3 = 1;
  p->has_waveblaster = 1;
  p->vbe_version = 0x0102;
  p->vbe_lfb = 1;
  strcpy(p->video_desc, "S3 ViRGE (host stub)");
  p->video_speed_kbs  = 17000; /* representative POD83 sysmem-ish fill (host) */
  p->video_speed_path = 2;     /* mode13h -- the only graphics path now */
}

#endif /* __DJGPP__ */
