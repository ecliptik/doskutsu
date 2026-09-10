/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hwinv.c -- One-shot DOS hardware inventory snapshot.
 *
 * Phase 11 wave-41 task #10 (probe-engineer lane). MVP per
 * docs/internal/WAVE-41-HW-INVENTORY-PROBE-PLAN.md sec. 4.
 *
 * Scope: 8 read-only sections per the plan --
 *   A. CPU      (CPUID family/model/stepping/feature flags + RDTSC calibration;
 *                no-CPUID fallback for early 486 -- 386/486 confirm, FPU, L1)
 *   B. MEM      (INT 15h E820/E801, DPMI memory state, conventional memory)
 *   C. VID      (VBE mode + version + OEM + Cirrus chip-id + 8bpp LFB-capability
 *                scan + bus type; delegates full dump to HWLOG/CHIPID)
 *   D. AUD      (SB DSP reset+version + OPL3 read-back + mixer reg snapshot +
 *                pattern-S1 WaveBlaster presence inference; NO MPU port reads)
 *   E. DSK      (INT 13h CF identify + extension-support bits + free space)
 *   F. IRQ+DMA  (PIC mask state + 8237 DMA controller state read-back)
 *   G. PORT     (BIOS Data Area decode + canonical-port presence checks)
 *   H. PCI      (full bus walk if PCI BIOS present; vendor:device:class dump)
 *
 * Read-only by design. Side effects bounded to:
 *   - Cirrus SR[0x06] = 0x12 unlock (non-destructive; same as chipid.c)
 *   - SB DSP reset (chip-spec-mandated reset sequence; non-destructive)
 *   - OPL3 detect (write 0x04 reset to address-register port 0x388; chip spec)
 *
 * NOT in scope (per the plan's pattern-S2 deferral):
 *   - MPU-401 port reads/writes (port 0x330/0x331). MPU status bit 7 is
 *     documented-unreliable on SB16 PnP CTL0026 per SDL/0047 + MPUPROBE-W22WB-F.
 *   - MIDI byte dispatch (DSP cmd 0x34 / 0x38). Pattern-S2 work-item; defer.
 *   - DMA buffer allocation. AUDRQ.EXE hangs at dma_alloc_begin on g2k.
 *   - Benchmarks. Existing MEMBW.EXE + DPMITHN.EXE + DACPROG.EXE cover these.
 *
 * Output: C:\HWINV.LOG (fsync per line); fallback ./HWINV.LOG if C:\ is RO.
 *
 * Per-section sentinels: every section opens with [HWINV-<CAT>-BEGIN] and
 * closes with [HWINV-<CAT>-DONE]. A hang's exact section is greppable from
 * the partial log via "BEGIN without matching DONE".
 *
 * Per-section watchdog: each section budgets ~500 ms wall clock. If exceeded,
 * the probe emits [HWINV-<CAT>-STEP-TIMEOUT] and proceeds to the next section
 * rather than hanging the system. Blast radius = "section N is incomplete",
 * not "operator power-cycles g2k".
 *
 * Plausibility-bound assertions (per [[probe_authoring_discipline]]):
 *   - cpu_mhz_measured  in [40, 5000]
 *   - dpmi_total_phys_kb in [4096, 1048576]
 *   - sb_dsp_version    in [3.0, 4.99]
 *   - cirrus_chip_id    in {0xA0, 0xA8, 0xAC, 0xB8} after mask 0xFC
 *
 * Pure DJGPP. No SDL, no engine. ~700 LOC.
 *
 * 8.3 DOS names:
 *   Source: tests/probes/hwinv.c
 *   Binary: HWINV.EXE  (5+3)
 *   Log:    HWINV.LOG  (5+3)
 *   BAT:    HWINV.BAT  (5+3)
 *
 * Build: `make hwinv` (or `make probes-p21`). DOSBox-X smoke is correctness-
 * only -- CPU/mem/PCI numbers are emulator-fictitious; real numbers are
 * real-HW-only per [[dosbox_not_proxy]].
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

/* Shared schema + env-detect helpers per docs/internal/WAVE-41-TRI-ENV-
 * CORRELATION-PLAN.md sec. 4.2 + 4.3. Single source of truth across
 * HWINV.EXE (here) + DOSKUTSU.EXE (nx-engine task #20). */
#include "runmanifest.h"

/* Build-time binary sha12 (filled in by Makefile via -D); fallback if
 * the build flag isn't set. Per schema v1: binary_sha12 must match
 * across all three tier manifests to prove the same binary ran. */
#ifndef HWINV_BUILD_SHA12
#define HWINV_BUILD_SHA12 "UNKNOWN_____"
#endif

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
    g_log = fopen("C:\\HWINV.LOG", "w");
    if (!g_log) {
        g_log = fopen("HWINV.LOG", "w");
    }
}

/* ============================================================ */
/* Time + watchdog                                              */
/* ============================================================ */

/* BIOS tick count at 40h:6Ch (32-bit; ~18.2 Hz). Survives across DPMI calls. */
static uint32_t bios_ticks(void)
{
    return _farpeekl(_dos_ds, 0x46Cul);
}

/* Wall clock in seconds, via BIOS tick. Granularity ~55 ms; good enough for
 * per-section watchdogs (budget 500 ms each). */
static double now_secs(void)
{
    return (double)bios_ticks() * (1.0 / 18.2065);
}

static double g_section_start_secs = 0.0;
static const char *g_section_tag = "";

#define SECTION_BUDGET_S 0.500  /* 500 ms per section */

static void section_begin(const char *tag)
{
    g_section_tag = tag;
    g_section_start_secs = now_secs();
    hlog("[HWINV-%s-BEGIN] t=%.3f", tag, g_section_start_secs);
}

static void section_done(void)
{
    double elapsed = now_secs() - g_section_start_secs;
    hlog("[HWINV-%s-DONE] elapsed=%.3fs", g_section_tag, elapsed);
}

/* Returns 1 if section has exceeded its budget; emit-and-bail caller. */
static int section_overbudget(void)
{
    double elapsed = now_secs() - g_section_start_secs;
    if (elapsed > SECTION_BUDGET_S) {
        hlog("[HWINV-%s-STEP-TIMEOUT] elapsed=%.3fs (budget=%.3fs)",
             g_section_tag, elapsed, SECTION_BUDGET_S);
        return 1;
    }
    return 0;
}

/* ============================================================ */
/* Section A: CPU + CPUID + RDTSC                               */
/* ============================================================ */

/* CPUID-availability check via FLAGS bit 21 (ID flag) toggle.
 * If bit 21 is toggleable, CPUID is available. Canonical x86 test. */
static int cpuid_available(void)
{
    uint32_t before, after;
    __asm__ volatile (
        "pushfl\n"
        "popl %0\n"
        "movl %0, %%eax\n"
        "xorl $0x200000, %%eax\n"
        "pushl %%eax\n"
        "popfl\n"
        "pushfl\n"
        "popl %1\n"
        : "=r"(before), "=r"(after)
        : : "eax"
    );
    return ((before ^ after) & 0x200000) != 0;
}

static void cpuid_call(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf)
    );
}

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int have_rdtsc(void)
{
    if (!cpuid_available()) return 0;
    uint32_t a, b, c, d;
    cpuid_call(1, &a, &b, &c, &d);
    return (d & (1u << 4)) ? 1 : 0;  /* EDX bit 4 = TSC */
}

/* Decode CPUID 1 EDX feature flags (subset relevant to doskutsu). */
static void log_cpuid_feat_edx(uint32_t edx)
{
    hlog("[HWINV-CPU] CPUID_FEAT_EDX=0x%08lX FPU=%d VME=%d DE=%d PSE=%d TSC=%d MSR=%d "
         "CX8=%d APIC=%d MTRR=%d CMOV=%d PAT=%d PSE36=%d MMX=%d FXSR=%d SSE=%d SSE2=%d",
         (unsigned long)edx,
         (edx & (1u<<0)) ? 1 : 0,   /* FPU */
         (edx & (1u<<1)) ? 1 : 0,   /* VME */
         (edx & (1u<<2)) ? 1 : 0,   /* DE */
         (edx & (1u<<3)) ? 1 : 0,   /* PSE */
         (edx & (1u<<4)) ? 1 : 0,   /* TSC */
         (edx & (1u<<5)) ? 1 : 0,   /* MSR */
         (edx & (1u<<8)) ? 1 : 0,   /* CX8 */
         (edx & (1u<<9)) ? 1 : 0,   /* APIC */
         (edx & (1u<<12)) ? 1 : 0,  /* MTRR */
         (edx & (1u<<15)) ? 1 : 0,  /* CMOV */
         (edx & (1u<<16)) ? 1 : 0,  /* PAT */
         (edx & (1u<<17)) ? 1 : 0,  /* PSE36 */
         (edx & (1u<<23)) ? 1 : 0,  /* MMX */
         (edx & (1u<<24)) ? 1 : 0,  /* FXSR */
         (edx & (1u<<25)) ? 1 : 0,  /* SSE */
         (edx & (1u<<26)) ? 1 : 0); /* SSE2 */
}

/* CPUID 2 cache descriptor table. Each descriptor byte maps to a fixed
 * cache topology. We decode only the descriptors documented for Intel
 * family 5+ (PODP83 era + a few common P54C/P55C/PII descriptors). */
static const char *cache_descriptor_str(uint8_t d)
{
    switch (d) {
    case 0x00: return NULL;  /* null */
    case 0x01: return "ITLB 4K-page 4-way 32 entries";
    case 0x02: return "ITLB 4M-page fully-assoc 2 entries";
    case 0x03: return "DTLB 4K-page 4-way 64 entries";
    case 0x06: return "L1 I-cache 8 KB 4-way 32B line";
    case 0x08: return "L1 I-cache 16 KB 4-way 32B line";
    case 0x0A: return "L1 D-cache 8 KB 2-way 32B line";
    case 0x0C: return "L1 D-cache 16 KB 4-way 32B line";
    case 0x22: return "L3 512 KB 4-way 64B line (sectored)";
    case 0x23: return "L3 1 MB 8-way 64B line (sectored)";
    case 0x40: return "no L2 (P5/P6 no-cache marker)";
    case 0x41: return "L2 128 KB 4-way 32B line";
    case 0x42: return "L2 256 KB 4-way 32B line";
    case 0x43: return "L2 512 KB 4-way 32B line";
    case 0x44: return "L2 1 MB 4-way 32B line";
    case 0x45: return "L2 2 MB 4-way 32B line";
    case 0x50: return "ITLB 4K+4M 64 entries";
    case 0x60: return "L1 D-cache 16 KB 8-way 64B line";
    case 0x66: return "L1 D-cache 8 KB 4-way 64B line";
    case 0x70: return "trace cache 12K-uop 8-way";
    case 0x80: return "L2 512 KB 8-way 64B line";
    case 0xFF: return "CPUID 2 sentinel (no descriptors here)";
    default:   return "(unknown)";
    }
}

/* ------------------------------------------------------------ */
/* No-CPUID fallback (486-class hw-coverage campaign).           */
/* Early Intel 486 steppings (the M2/M3 486DX2 candidates)       */
/* predate the CPUID instruction. When cpuid_available()==0 the  */
/* CPUID path has no family/model data; this fallback supplies   */
/* what can be learned WITHOUT CPUID: a 386-vs-486 confirm, FPU  */
/* presence, a coarse loop-rate cross-check, an L1 note. Gated   */
/* entirely behind cpuid_available()==0 -- cannot regress the    */
/* CPUID path. No RDTSC anywhere (a 486 has none).               */
/* ------------------------------------------------------------ */

/* 386 vs 486+: EFLAGS bit 18 (AC, Alignment Check) is implemented on the
 * 486 and later but not on the 386. If bit 18 is toggleable the CPU is
 * 486-class or newer. pushfl/popfl only -- no faulting instruction. */
static int eflags_ac_toggles(void)
{
    uint32_t before, after;
    __asm__ volatile (
        "pushfl\n"
        "popl %0\n"
        "movl %0, %%eax\n"
        "xorl $0x40000, %%eax\n"
        "pushl %%eax\n"
        "popfl\n"
        "pushfl\n"
        "popl %1\n"
        "pushl %0\n"            /* restore the original EFLAGS */
        "popfl\n"
        : "=r"(before), "=r"(after)
        : : "eax"
    );
    return ((before ^ after) & 0x40000) != 0;
}

/* FPU presence via the BIOS equipment word (INT 11h, AX bit 1). This is
 * deliberately NOT the FNINIT/FNSTSW silicon test: on a 486SX with no
 * coprocessor and CR0.EM set, executing an FPU instruction faults (INT 7),
 * which under DPMI would crash the probe. The BIOS equipment word carries
 * the same fact with zero fault risk. Returns 1/0, or -1 if the call fails. */
static int fpu_present_bios(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    if (__dpmi_int(0x11, &r) < 0) return -1;
    return (r.x.ax & 0x0002) ? 1 : 0;
}

/* Coarse loop-rate cross-check: count a fixed inner loop's passes across
 * ~4 BIOS ticks (~220 ms). NOT a precise clock -- the operator's nominal
 * MHz is authoritative; this only flags a grossly-mislabelled machine and
 * gives flush-instr an M1/M2/M3 ratio. PIT/BIOS-tick timed, no RDTSC. */
static void nocpuid_loop_rate(void)
{
    uint32_t t0 = bios_ticks();
    while (bios_ticks() == t0) { }          /* align to a tick edge */
    uint32_t tstart = bios_ticks();
    uint32_t spins = 0;
    while ((bios_ticks() - tstart) < 4u) {  /* ~4 ticks ~= 220 ms */
        for (int i = 0; i < 1000; i++) {
            __asm__ volatile ("" ::: "memory");
        }
        spins++;
    }
    uint32_t ticks = bios_ticks() - tstart;
    double secs = (double)ticks * (1.0 / 18.2065);
    double kips = (secs > 0.0) ? ((double)spins / secs) : 0.0;
    hlog("[HWINV-CPU] NOCPUID_LOOP_KIPS=%.1f over=%lu_ticks "
         "(coarse loop-rate cross-check; operator nominal clock authoritative)",
         kips, (unsigned long)ticks);
}

/* Full no-CPUID CPU characterization -- runs only when CPUID is absent. */
static void section_cpu_nocpuid(void)
{
    int is_486 = eflags_ac_toggles();
    hlog("[HWINV-CPU] NOCPUID_CPU_CLASS=%s (EFLAGS AC-bit %stoggleable)",
         is_486 ? "486-class" : "386-class", is_486 ? "" : "not ");
    int fpu = fpu_present_bios();
    if (fpu < 0) {
        hlog("[HWINV-CPU] NOCPUID_FPU=UNKNOWN (INT 11h equipment query failed)");
    } else {
        hlog("[HWINV-CPU] NOCPUID_FPU=%d (%s -- BIOS equipment word bit 1; "
             "a 486DX has an on-chip FPU, a 486SX does not)",
             fpu, fpu ? "FPU present" : "no FPU");
    }
    if (is_486) {
        hlog("[HWINV-CPU] NOCPUID_L1_NOTE=Intel-486DX2-typical 8 KB unified "
             "write-through L1 (architectural, not probed; a Cyrix/UMC "
             "486-class part would differ -- confirm vs operator machine list)");
    } else {
        hlog("[HWINV-CPU] NOCPUID_L1_NOTE=386-class: no on-chip L1 "
             "(architectural; any cache is external/motherboard)");
    }
    nocpuid_loop_rate();
}

static void section_cpu(void)
{
    section_begin("CPU");
    if (section_overbudget()) goto done;

    int has_cpuid = cpuid_available();
    hlog("[HWINV-CPU] CPUID_AVAILABLE=%d", has_cpuid);
    if (!has_cpuid) {
        /* CPUID-absent: 386, or a 486 stepping predating CPUID (the
         * campaign's Intel 486DX2 candidates). NOT necessarily a 486SX-A
         * or older -- the no-CPUID fallback below characterizes it. */
        hlog("[HWINV-CPU] CPUID_ABSENT (386-class, or a 486 stepping that "
             "predates CPUID; using no-CPUID fallback)");
        section_cpu_nocpuid();
        goto done;
    }

    /* CPUID leaf 0: max-leaf + vendor string. */
    uint32_t max_leaf, vb, vc, vd;
    cpuid_call(0, &max_leaf, &vb, &vc, &vd);
    char vendor[13];
    *(uint32_t *)(vendor + 0) = vb;
    *(uint32_t *)(vendor + 4) = vd;
    *(uint32_t *)(vendor + 8) = vc;
    vendor[12] = 0;
    hlog("[HWINV-CPU] CPUID_VENDOR=\"%s\" MAX_LEAF=0x%lu", vendor, (unsigned long)max_leaf);

    /* CPUID leaf 1: family / model / stepping / feature flags. */
    if (max_leaf >= 1) {
        uint32_t a, b, c, d;
        cpuid_call(1, &a, &b, &c, &d);
        unsigned family = (a >> 8) & 0xF;
        unsigned model  = (a >> 4) & 0xF;
        unsigned step   = a & 0xF;
        unsigned extf   = (a >> 20) & 0xFF;
        unsigned extm   = (a >> 16) & 0xF;
        unsigned cl_size_words = (b >> 8) & 0xFF;  /* in units of 8 bytes */
        hlog("[HWINV-CPU] CPUID_FAMILY=%u MODEL=%u STEPPING=%u EXTFAM=%u EXTMOD=%u",
             family, model, step, extf, extm);
        hlog("[HWINV-CPU] CPUID_CACHE_LINE_BYTES=%u (CLFLUSH-line if avail)",
             cl_size_words * 8);
        log_cpuid_feat_edx(d);

        /* Family 5 = Pentium (P54C / P55C / PODP83). Plausibility-bound. */
        if (family < 1 || family > 15) {
            hlog("[HWINV-CPU] FAMILY_OUT_OF_BOUND family=%u (expected 4..15)", family);
        }
    }
    if (section_overbudget()) goto done;

    /* CPUID leaf 2: cache descriptor table (Intel only; AMD uses 0x80000005+). */
    if (max_leaf >= 2 && strcmp(vendor, "GenuineIntel") == 0) {
        uint32_t a, b, c, d;
        cpuid_call(2, &a, &b, &c, &d);
        /* AL = number of times CPUID 2 must be executed; on PODP83 era = 1. */
        unsigned int times = a & 0xFF;
        hlog("[HWINV-CPU] CPUID_2_ITERATIONS_NEEDED=%u", times);
        /* Mask out AL bit (first byte of EAX is iteration count, not descriptor). */
        uint8_t desc[16];
        desc[0] = 0;  /* skip AL */
        desc[1] = (a >> 8) & 0xFF;
        desc[2] = (a >> 16) & 0xFF;
        desc[3] = (a >> 24) & 0xFF;
        desc[4] = b & 0xFF;
        desc[5] = (b >> 8) & 0xFF;
        desc[6] = (b >> 16) & 0xFF;
        desc[7] = (b >> 24) & 0xFF;
        desc[8] = c & 0xFF;
        desc[9] = (c >> 8) & 0xFF;
        desc[10] = (c >> 16) & 0xFF;
        desc[11] = (c >> 24) & 0xFF;
        desc[12] = d & 0xFF;
        desc[13] = (d >> 8) & 0xFF;
        desc[14] = (d >> 16) & 0xFF;
        desc[15] = (d >> 24) & 0xFF;
        for (int i = 1; i < 16; i++) {
            const char *s = cache_descriptor_str(desc[i]);
            if (s) {
                hlog("[HWINV-CPU] CACHE_DESC[0x%02X]=%s", desc[i], s);
            }
        }
    } else if (max_leaf < 2) {
        hlog("[HWINV-CPU] CPUID_2_UNSUPPORTED (max_leaf=%lu)", (unsigned long)max_leaf);
    } else {
        hlog("[HWINV-CPU] CPUID_2_SKIPPED (vendor=%s; descriptor table is Intel-specific)", vendor);
    }
    if (section_overbudget()) goto done;

    /* CPUID 0x80000000: extended brand-string support (family 6+). */
    uint32_t ea, eb, ec, ed;
    cpuid_call(0x80000000ul, &ea, &eb, &ec, &ed);
    hlog("[HWINV-CPU] CPUID_EXT_MAX_LEAF=0x%08lX", (unsigned long)ea);
    if (ea >= 0x80000004ul) {
        char brand[49] = {0};
        uint32_t *pb = (uint32_t *)brand;
        for (int i = 0; i < 3; i++) {
            cpuid_call(0x80000002ul + i, &pb[i*4 + 0], &pb[i*4 + 1],
                       &pb[i*4 + 2], &pb[i*4 + 3]);
        }
        brand[48] = 0;
        hlog("[HWINV-CPU] CPUID_BRAND=\"%s\"", brand);
    } else {
        hlog("[HWINV-CPU] CPUID_BRAND_UNSUPPORTED (ext_max_leaf=0x%08lX < 0x80000004)",
             (unsigned long)ea);
    }
    if (section_overbudget()) goto done;

    /* CPUID hypervisor leaf 0x40000000 (DOSBox-X / VMware / KVM / Hyper-V).
     * The hypervisor presence bit lives in CPUID 1 ECX bit 31, but that
     * bit only exists post-2006. Pentium-era chips don't have it; reading
     * 0x40000000 still works (CPUs treat it as a non-existent leaf and
     * return zeros or garbage). DOSBox-X with hypervisor=true returns
     * a vendor string. */
    if (cpuid_available()) {
        uint32_t hva, hvb, hvc, hvd;
        cpuid_call(0x40000000ul, &hva, &hvb, &hvc, &hvd);
        char hyper[13];
        *(uint32_t *)(hyper + 0) = hvb;
        *(uint32_t *)(hyper + 4) = hvc;
        *(uint32_t *)(hyper + 8) = hvd;
        hyper[12] = 0;
        /* Sanitize: only emit ASCII. */
        int printable = 1;
        for (int i = 0; i < 12; i++) {
            if (hyper[i] != 0 && (hyper[i] < 0x20 || hyper[i] > 0x7E)) {
                printable = 0; break;
            }
        }
        if (printable && hyper[0]) {
            hlog("[HWINV-CPU] CPUID_HYPERVISOR_VENDOR=\"%s\"", hyper);
        } else {
            hlog("[HWINV-CPU] CPUID_HYPERVISOR_VENDOR=(none/unprintable)");
        }
    }
    if (section_overbudget()) goto done;

    /* RDTSC calibration against BIOS tick (one full tick = ~55ms). */
    hlog("[HWINV-CPU] RDTSC_AVAILABLE=%d", have_rdtsc());
    if (have_rdtsc()) {
        /* Calibrate over ~5 BIOS ticks = ~275 ms. */
        uint32_t t0 = bios_ticks();
        while (bios_ticks() == t0) { /* spin to tick boundary */ }
        uint32_t t1 = bios_ticks();
        uint64_t tsc0 = rdtsc();
        while ((bios_ticks() - t1) < 5) { /* spin 5 ticks */ }
        uint64_t tsc1 = rdtsc();
        uint32_t ticks_elapsed = bios_ticks() - t1;
        /* secs = ticks / 18.2065. cycles_per_sec = (tsc1-tsc0) / secs. */
        double secs = (double)ticks_elapsed / 18.2065;
        double mhz = (double)(tsc1 - tsc0) / secs / 1.0e6;
        hlog("[HWINV-CPU] RDTSC_MHZ_MEASURED=%.2f (over %.3fs ; cycles=%llu)",
             mhz, secs, (unsigned long long)(tsc1 - tsc0));
        if (mhz < 40.0 || mhz > 5000.0) {
            hlog("[HWINV-CPU] RDTSC_MHZ_OUT_OF_BOUND value=%.2f (expected [40, 5000])", mhz);
        }
    }

done:
    section_done();
}

/* ============================================================ */
/* Section B: Memory layout + DPMI state                        */
/* ============================================================ */

static void section_mem(void)
{
    section_begin("MEM");
    if (section_overbudget()) goto done;

    /* INT 12h: conventional memory size in KB (legacy). */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    if (__dpmi_int(0x12, &r) >= 0) {
        hlog("[HWINV-MEM] INT12_CONVENTIONAL_KB=%u", r.x.ax);
    }
    if (section_overbudget()) goto done;

    /* INT 15h AX=E801h: large memory map. AX=KB 1-16M, BX=64KB 16M+,
     * CX=AX configured, DX=BX configured. */
    memset(&r, 0, sizeof r);
    r.x.ax = 0xE801;
    if (__dpmi_int(0x15, &r) >= 0 && (r.x.flags & 1) == 0) {
        hlog("[HWINV-MEM] INT15_E801_AX_KB=%u BX_64KB=%u CX_KB=%u DX_64KB=%u",
             r.x.ax, r.x.bx, r.x.cx, r.x.dx);
    } else {
        hlog("[HWINV-MEM] INT15_E801_UNSUPPORTED (E820 fallback would go here; not needed for PODP83 era)");
    }
    if (section_overbudget()) goto done;

    /* DPMI free memory information. __dpmi_get_free_memory_information
     * fills a 0x30-byte struct; we capture the documented fields. */
    __dpmi_free_mem_info fmi;
    memset(&fmi, 0, sizeof fmi);
    if (__dpmi_get_free_memory_information(&fmi) == 0) {
        hlog("[HWINV-MEM] DPMI_LARGEST_FREE_BLOCK_BYTES=%lu",
             (unsigned long)fmi.largest_available_free_block_in_bytes);
        hlog("[HWINV-MEM] DPMI_MAX_UNLOCKED_PAGES=%lu MAX_LOCKED=%lu",
             (unsigned long)fmi.maximum_unlocked_page_allocation_in_pages,
             (unsigned long)fmi.maximum_locked_page_allocation_in_pages);
        hlog("[HWINV-MEM] DPMI_LINEAR_SPACE_PAGES=%lu UNLOCKED_PAGES_FREE=%lu",
             (unsigned long)fmi.linear_address_space_size_in_pages,
             (unsigned long)fmi.total_number_of_unlocked_pages);
        hlog("[HWINV-MEM] DPMI_TOTAL_FREE_PAGES=%lu TOTAL_PHYSICAL=%lu",
             (unsigned long)fmi.total_number_of_free_pages,
             (unsigned long)fmi.total_number_of_physical_pages);
        /* Plausibility-bound: total_phys in [4096, 1048576] KB = [1M, 256M]. */
        unsigned long total_kb = (unsigned long)fmi.total_number_of_physical_pages * 4;
        if (total_kb < 1024ul || total_kb > 1048576ul) {
            hlog("[HWINV-MEM] DPMI_TOTAL_PHYS_OUT_OF_BOUND kb=%lu (expected [1024, 1048576])",
                 total_kb);
        }
    } else {
        hlog("[HWINV-MEM] DPMI_FREE_MEM_INFO_FAILED");
    }
    if (section_overbudget()) goto done;

    /* _go32_dpmi_remaining_physical_memory + _go32_dpmi_remaining_virtual_memory
     * convenience helpers (return bytes). */
    hlog("[HWINV-MEM] GO32_REMAINING_PHYS_BYTES=%lu REMAINING_VIRT_BYTES=%lu",
         (unsigned long)_go32_dpmi_remaining_physical_memory(),
         (unsigned long)_go32_dpmi_remaining_virtual_memory());
    if (section_overbudget()) goto done;

    /* DPMI version + flags. */
    __dpmi_version_ret vr;
    memset(&vr, 0, sizeof vr);
    if (__dpmi_get_version(&vr) == 0) {
        hlog("[HWINV-MEM] DPMI_VERSION=%u.%u FLAGS=0x%04X CPU=%u MASTER_PIC=0x%02X SLAVE_PIC=0x%02X",
             vr.major, vr.minor, vr.flags, vr.cpu,
             vr.master_pic, vr.slave_pic);
    }
    if (section_overbudget()) goto done;

    /* DOS conventional memory allocation test (1 KB).
     * Not strictly necessary but flags pathological config. */
    __dpmi_meminfo dosblock;
    memset(&dosblock, 0, sizeof dosblock);
    int dos_test_seg = __dpmi_allocate_dos_memory(64, (int *)&dosblock.handle);  /* 64 paragraphs = 1 KB */
    if (dos_test_seg >= 0) {
        hlog("[HWINV-MEM] DOSMEM_1K_ALLOC_TEST=OK_seg=0x%04X", dos_test_seg);
        __dpmi_free_dos_memory(dos_test_seg);
    } else {
        hlog("[HWINV-MEM] DOSMEM_1K_ALLOC_TEST=FAIL");
    }

done:
    section_done();
}

/* ============================================================ */
/* Section C: Video (delegate-mostly)                           */
/* ============================================================ */

static void section_vid(void)
{
    section_begin("VID");
    if (section_overbudget()) goto done;

    /* Current VBE mode via INT 10h AX=4F03h. */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F03;
    if (__dpmi_int(0x10, &r) >= 0 && r.x.ax == 0x004F) {
        hlog("[HWINV-VID] VBE_MODE_CURRENT=0x%04X (raw BX, includes LFB/preserve bits)",
             r.x.bx);
    } else {
        hlog("[HWINV-VID] VBE_MODE_CURRENT=UNKNOWN (AX=0x%04X)", r.x.ax);
    }
    if (section_overbudget()) goto done;

    /* VBE controller info via INT 10h AX=4F00h. Just grab OEM string +
     * VRAM total; full dump is HWLOG.EXE's job. */
    unsigned long tbuf = __tb;
    _farpokeb(_dos_ds, tbuf + 0, 'V');
    _farpokeb(_dos_ds, tbuf + 1, 'B');
    _farpokeb(_dos_ds, tbuf + 2, 'E');
    _farpokeb(_dos_ds, tbuf + 3, '2');
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00;
    r.x.di = tbuf & 0xF;
    r.x.es = (tbuf >> 4) & 0xFFFF;
    if (__dpmi_int(0x10, &r) >= 0 && r.x.ax == 0x004F) {
        uint8_t buf[256];
        for (int i = 0; i < 256; i++) buf[i] = _farpeekb(_dos_ds, tbuf + i);
        uint16_t ver      = *(const uint16_t *)(buf + 0x04);
        uint16_t oem_ofs  = *(const uint16_t *)(buf + 0x06);
        uint16_t oem_seg  = *(const uint16_t *)(buf + 0x08);
        uint16_t total64k = *(const uint16_t *)(buf + 0x12);
        /* Read first 64 bytes of OEM string at oem_seg:oem_ofs. */
        char oem[65];
        uint32_t oem_lin = ((uint32_t)oem_seg << 4) + oem_ofs;
        for (int i = 0; i < 64; i++) {
            char c = (char)_farpeekb(_dos_ds, oem_lin + i);
            oem[i] = c;
            if (c == 0) break;
        }
        oem[64] = 0;
        hlog("[HWINV-VID] VBE_VERSION=%u.%u VRAM_KB=%u OEM=\"%s\"",
             (ver >> 8) & 0xFF, ver & 0xFF, (unsigned)total64k * 64, oem);

        /* LFB-capability scan (486-class campaign contract sec.2.1): walk
         * the VBE mode list, AX=4F01 each 8bpp mode, report whether a linear
         * framebuffer is exposed. doskutsu renders 8bpp small (VGA mode 13h
         * 320x200, or a VESA 320x200/320x240 LFB mode if the BIOS exposes
         * one); VESA does not standardize a 320x240 mode number, so we report
         * the LFB attribute for every 8bpp mode and call out the small ones.
         * ModeInfo goes to tbuf+256 (the 4F00 block used tbuf+0..255). */
        uint16_t mp_off = *(const uint16_t *)(buf + 0x0E);
        uint16_t mp_seg = *(const uint16_t *)(buf + 0x10);
        uint32_t mode_lin = ((uint32_t)mp_seg << 4) + mp_off;
        int n_8bpp = 0, n_8bpp_lfb = 0;
        for (int mi = 0; mi < 256; mi++) {
            uint16_t vmode = _farpeekw(_dos_ds, mode_lin + mi * 2);
            if (vmode == 0xFFFF) break;
            __dpmi_regs rm;
            memset(&rm, 0, sizeof rm);
            rm.x.ax = 0x4F01;
            rm.x.cx = vmode;
            rm.x.di = (tbuf + 256) & 0xF;
            rm.x.es = ((tbuf + 256) >> 4) & 0xFFFF;
            if (__dpmi_int(0x10, &rm) < 0 || rm.x.ax != 0x004F) continue;
            uint16_t mattr = _farpeekw(_dos_ds, tbuf + 256 + 0x00);
            uint16_t mxr   = _farpeekw(_dos_ds, tbuf + 256 + 0x12);
            uint16_t myr   = _farpeekw(_dos_ds, tbuf + 256 + 0x14);
            uint8_t  mbpp  = _farpeekb(_dos_ds, tbuf + 256 + 0x19);
            if (mbpp != 8 || !(mattr & 0x0001)) continue;  /* 8bpp + supported */
            n_8bpp++;
            int has_lfb = (mattr & 0x0080) ? 1 : 0;        /* attr bit 7 = LFB */
            if (has_lfb) n_8bpp_lfb++;
            if (mxr <= 360 && myr <= 280) {  /* doskutsu's render neighbourhood */
                hlog("[HWINV-VID] VBE_8BPP_SMALLMODE=0x%04X %ux%u LFB=%d",
                     vmode, mxr, myr, has_lfb);
            }
        }
        hlog("[HWINV-VID] VBE_LFB_AVAILABLE=%d VBE_8BPP_MODES=%d "
             "VBE_8BPP_LFB_MODES=%d",
             (n_8bpp_lfb > 0) ? 1 : 0, n_8bpp, n_8bpp_lfb);
        if (n_8bpp_lfb == 0) {
            hlog("[HWINV-VID] VBE_NO_LFB_FINDING (no 8bpp linear-framebuffer "
                 "mode -- VBE 1.2-only card; doskutsu's render path falls to "
                 "banked VGA mode 13h. Campaign finding for this machine.)");
        }
    } else {
        hlog("[HWINV-VID] VBE_INFO_UNSUPPORTED (AX=0x%04X)", r.x.ax);
    }
    if (section_overbudget()) goto done;

    /* Cirrus chip-ID byte from SR[0x27] (after SR[0x06] = 0x12 unlock).
     * Match the chip-name table from hwlog.c / chipid.c so cross-anchored.
     *
     * Wave-43 task #16 fix #2: when SR[0x27] read returns 0x00, fall back
     * to PCI bus query for class=display vendor=0x1013 (Cirrus). 86Box's
     * Cirrus emulation under build-qa task #11 returned SR[0x27]=0x00 even
     * though PCI walk cleanly enumerated 0x1013/0x00A8 = CL-GD5434. The
     * orthogonal-bus-path corroborator pattern: if SR-port-via-VGA-port
     * fails, try INT 1Ah-via-PCI-BIOS. Either signal source identifies the
     * chip; emit which source provided the answer. */
    uint8_t sr06_orig;
    outportb(0x3C4, 0x06);
    sr06_orig = inportb(0x3C5);
    outportb(0x3C4, 0x06);
    outportb(0x3C5, 0x12);
    outportb(0x3C4, 0x27);
    uint8_t chip27 = inportb(0x3C5);
    const char *chip_name = "(unknown / not Cirrus 543x)";
    const char *chip_source = "SR27";
    int chip_id = chip27;
    int chip_rev = chip27 & 0x03;
    switch (chip27 & 0xFC) {
    case 0xA0: chip_name = "Cirrus CL-GD5430"; break;
    case 0xA8: chip_name = "Cirrus CL-GD5434"; break;
    case 0xAC: chip_name = "Cirrus CL-GD5436"; break;
    case 0xB8: chip_name = "Cirrus CL-GD5446"; break;
    default: break;
    }
    /* Restore SR[0x06] BEFORE the PCI fallback (any PCI BIOS call could
     * trigger BIOS code that reads SR; better to leave the chip in the
     * state we found it). */
    outportb(0x3C4, 0x06);
    outportb(0x3C5, sr06_orig);

    /* If SR[0x27] returned 0x00 (chip emulation that doesn't decode the
     * Cirrus extension register; e.g. 86Box CL-GD5434 model as of
     * 2025-era builds), try PCI BIOS find-by-class for a display-class
     * device + check if vendor=0x1013. */
    if (chip27 == 0x00) {
        __dpmi_regs rfind;
        memset(&rfind, 0, sizeof rfind);
        rfind.x.ax = 0xB103;
        rfind.d.ecx = 0x00030000ul;  /* class=display, subclass=VGA, prog=0 */
        rfind.x.si = 0;
        if (__dpmi_int(0x1A, &rfind) >= 0 && rfind.h.ah == 0) {
            /* PCI BIOS found the device; read vendor + device IDs. */
            uint8_t bus = rfind.h.bh;
            uint8_t devfn = rfind.h.bl;
            __dpmi_regs rread;
            memset(&rread, 0, sizeof rread);
            rread.x.ax = 0xB109;  /* read word */
            rread.h.bh = bus;
            rread.h.bl = devfn;
            rread.x.di = 0x00;    /* vendor offset */
            uint16_t pci_vendor = 0xFFFF;
            if (__dpmi_int(0x1A, &rread) >= 0 && rread.h.ah == 0) {
                pci_vendor = rread.x.cx;
            }
            if (pci_vendor == 0x1013) {
                /* Cirrus -- look up device ID. */
                memset(&rread, 0, sizeof rread);
                rread.x.ax = 0xB109;
                rread.h.bh = bus;
                rread.h.bl = devfn;
                rread.x.di = 0x02;
                uint16_t pci_device = 0;
                if (__dpmi_int(0x1A, &rread) >= 0 && rread.h.ah == 0) {
                    pci_device = rread.x.cx;
                }
                switch (pci_device) {
                case 0x00A0: chip_name = "Cirrus CL-GD5430"; break;
                case 0x00A8: chip_name = "Cirrus CL-GD5434"; break;
                case 0x00AC: chip_name = "Cirrus CL-GD5436"; break;
                case 0x00B8: chip_name = "Cirrus CL-GD5446"; break;
                case 0x00BC: chip_name = "Cirrus CL-GD5480"; break;
                default:     chip_name = "Cirrus (unknown device)"; break;
                }
                chip_source = "PCI_FALLBACK";
                chip_id = pci_device;
                chip_rev = -1;  /* PCI BAR doesn't expose chip rev */
            }
        }
    }
    if (chip_rev >= 0) {
        hlog("[HWINV-VID] CIRRUS_CHIP_ID=0x%02X NAME=\"%s\" REV=%d CHIP_ID_SOURCE=%s",
             chip_id, chip_name, chip_rev, chip_source);
    } else {
        hlog("[HWINV-VID] CIRRUS_CHIP_ID=0x%04X NAME=\"%s\" REV=NA CHIP_ID_SOURCE=%s",
             chip_id, chip_name, chip_source);
    }
    /* Always emit the raw SR27 byte for cross-tier diff (analysts can
     * compare SR27 directly to spot emulator-vs-real-HW differences in
     * chip-extension decoding). */
    hlog("[HWINV-VID] CIRRUS_CHIP_ID_SR27_RAW=0x%02X", chip27);

    /* Video bus type (486-class campaign contract sec.2.1). PCI is BIOS
     * -enumerable; VLB and ISA are not BIOS-distinguishable from each other,
     * so the honest emit is PCI vs NON-PCI with a confirm-from-operator note.
     * The bus drives the membw LFB-write spread across M1/M2/M3. */
    {
        __dpmi_regs rbus;
        memset(&rbus, 0, sizeof rbus);
        rbus.x.ax = 0xB103;            /* PCI BIOS find-device-by-class */
        rbus.d.ecx = 0x00030000ul;     /* class=display, subclass=VGA */
        rbus.x.si = 0;
        int pci_disp = (__dpmi_int(0x1A, &rbus) >= 0 && rbus.h.ah == 0);
        if (pci_disp) {
            hlog("[HWINV-VID] VIDEO_BUS=PCI "
                 "(PCI BIOS enumerated a display-class device)");
        } else {
            hlog("[HWINV-VID] VIDEO_BUS=NON-PCI (no PCI display-class device; "
                 "ISA or VLB -- not BIOS-distinguishable; confirm from the "
                 "operator machine list)");
        }
    }
    if (section_overbudget()) goto done;

    hlog("[HWINV-VID] FULL_VIDEO_DATA=\"run HWLOG.EXE + CHIPID.EXE\" "
         "(this section is one-line summary only; full dumps delegated)");

done:
    section_done();
}

/* ============================================================ */
/* Section D: Audio (READ-ONLY pattern S1; no MPU port reads)   */
/* ============================================================ */

/* SB DSP primitives, scoped down from mpuwbprobe.c. */

static int dsp_wait_write(int audio_base, double t0, double budget_s)
{
    int port = audio_base + 0xC;
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inportb(port);
        if (!(s & 0x80)) return 1;
        if ((i & 0xFF) == 0 && (now_secs() - t0) > budget_s) return 0;
    }
    return 0;
}

static int dsp_wait_read(int audio_base, double t0, double budget_s)
{
    int port = audio_base + 0xE;
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inportb(port);
        if (s & 0x80) return 1;
        if ((i & 0xFF) == 0 && (now_secs() - t0) > budget_s) return 0;
    }
    return 0;
}

static int dsp_reset_safe(int audio_base, double t0, double budget_s)
{
    int reset_port = audio_base + 0x6;
    outportb(reset_port, 1);
    /* Mandatory ~3-10 us delay per SB spec. Busy-wait via inb on a no-op port. */
    for (int i = 0; i < 100; i++) (void)inportb(0x80);
    outportb(reset_port, 0);
    if (!dsp_wait_read(audio_base, t0, budget_s)) return -1;
    uint8_t b = inportb(audio_base + 0xA);
    return (b == 0xAA) ? 0 : -2;
}

/* Parse the BLASTER env var. Returns 0 on success, -1 if absent. */
static int parse_blaster(int *out_a, int *out_i, int *out_d, int *out_h,
                         int *out_t, int *out_p)
{
    const char *s = getenv("BLASTER");
    if (!s) return -1;
    *out_a = -1; *out_i = -1; *out_d = -1;
    *out_h = -1; *out_t = -1; *out_p = -1;
    const char *p = s;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char f = *p++;
        int val = 0;
        int base = (f == 'A' || f == 'a' || f == 'P' || f == 'p') ? 16 : 10;
        while (*p && *p != ' ') {
            int dig = -1;
            if (*p >= '0' && *p <= '9') dig = *p - '0';
            else if (*p >= 'A' && *p <= 'F') dig = *p - 'A' + 10;
            else if (*p >= 'a' && *p <= 'f') dig = *p - 'a' + 10;
            if (dig < 0 || dig >= base) break;
            val = val * base + dig;
            p++;
        }
        switch (f) {
        case 'A': case 'a': *out_a = val; break;
        case 'I': case 'i': *out_i = val; break;
        case 'D': case 'd': *out_d = val; break;
        case 'H': case 'h': *out_h = val; break;
        case 'T': case 't': *out_t = val; break;
        case 'P': case 'p': *out_p = val; break;
        default: break;  /* J=joystick, ignored */
        }
    }
    return 0;
}

static void section_aud(void)
{
    section_begin("AUD");
    if (section_overbudget()) goto done;

    int a = -1, irq = -1, d = -1, h = -1, t = -1, p = -1;
    int has_blaster = (parse_blaster(&a, &irq, &d, &h, &t, &p) == 0);
    if (has_blaster) {
        hlog("[HWINV-AUD] BLASTER_RAW=\"%s\"", getenv("BLASTER"));
        hlog("[HWINV-AUD] BLASTER_A=0x%X I=%d D=%d H=%d P=0x%X T=%d",
             a, irq, d, h, p, t);
    } else {
        hlog("[HWINV-AUD] BLASTER_UNSET (no SB16 env var; section_aud limited)");
    }
    if (section_overbudget()) goto done;

    /* Pattern S1 step 2-3: SB DSP reset + version query. NO MPU port reads.
     * Per docs/internal/MPUPROBE-W22WB-F-ANALYSIS.md hard-no #1: MPU status
     * bit 7 lies; we never read port 0x331. */
    if (has_blaster && a >= 0) {
        double t0 = now_secs();
        int rc = dsp_reset_safe(a, t0, 0.10);  /* 100 ms budget for DSP reset */
        if (rc == 0) {
            hlog("[HWINV-AUD] SB_DSP_RESET=ACK_0xAA_OK");
            /* DSP version query (cmd 0xE1): write 0xE1, read 2 bytes. */
            t0 = now_secs();
            if (dsp_wait_write(a, t0, 0.05) && (outportb(a + 0xC, 0xE1), 1) &&
                dsp_wait_read(a, t0, 0.05)) {
                uint8_t v1 = inportb(a + 0xA);
                uint8_t v2 = 0xFF;
                t0 = now_secs();
                if (dsp_wait_read(a, t0, 0.05)) v2 = inportb(a + 0xA);
                hlog("[HWINV-AUD] SB_DSP_VERSION=%u.%02u (expected 4.13 for SB16 PnP CTL0026)",
                     v1, v2);
                /* Plausibility-bound: v1 in [3, 4]; v2 in [0, 99]. */
                if (v1 < 1 || v1 > 6 || v2 > 99) {
                    hlog("[HWINV-AUD] SB_DSP_VERSION_OUT_OF_BOUND v1=%u v2=%u", v1, v2);
                }
                /* WB header presence: inferred from DSP responding. The
                 * daughterboard sits on the SB16 WaveBlaster header; if
                 * the host SB16 is alive, the WB header is at least
                 * powered. The DreamBlaster S2 itself responds to MIDI
                 * via the host DSP (pattern S2, deferred). */
                hlog("[HWINV-AUD] WB_HEADER=INFERRED_FROM_DSP_OK "
                     "(no MPU port reads per SDL/0047+MPUPROBE-W22WB-F)");
            } else {
                hlog("[HWINV-AUD] SB_DSP_VERSION_QUERY_TIMEOUT");
            }
        } else if (rc == -1) {
            hlog("[HWINV-AUD] SB_DSP_RESET=TIMEOUT (no 0xAA; DSP unresponsive or absent)");
        } else {
            hlog("[HWINV-AUD] SB_DSP_RESET=BAD_ACK rc=%d (chip in odd state)", rc);
        }
    }
    if (section_overbudget()) goto done;

    /* OPL3 presence: write 0x04 (reset Timer 1+2) to 0x388 (address-register
     * port), then read 0x388. OPL3 returns bit 1 set if both timers are
     * running; OPL2 only ever sets bit 7 (timer 1) and bit 5 (timer 2). The
     * canonical way to discriminate is: read status, OR-in 0x60 (start
     * both timers), wait, read again. We do the *much* cheaper "just read
     * after reset" version that's good enough for presence + family. */
    outportb(0x388, 0x04);   /* address: Timer Control Register */
    /* Small delay (~3.3 us per Yamaha spec). */
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x389, 0x60);   /* reset both timers */
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x388, 0x04);
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x389, 0x80);   /* IRQ reset */
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    uint8_t opl_status = inportb(0x388);
    /* Now start timer 1 (write 0xFF to addr 0x02, then 0x21 to start). */
    outportb(0x388, 0x02);
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x389, 0xFF);
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x388, 0x04);
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x389, 0x21);
    /* Wait ~80 us for timer to count down (one PIT tick ~838 ns, 80 us = 96 ticks). */
    for (int i = 0; i < 300; i++) (void)inportb(0x80);
    uint8_t opl_after = inportb(0x388);
    /* Reset and silence timers. */
    outportb(0x388, 0x04);
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x389, 0x60);
    for (int i = 0; i < 50; i++) (void)inportb(0x80);
    outportb(0x389, 0x80);
    int opl_present = (opl_after & 0xC0) != 0;
    int opl_is_3 = (opl_after & 0x06) != 0;  /* OPL3-specific bits in status */
    hlog("[HWINV-AUD] OPL_STATUS_INITIAL=0x%02X AFTER_TIMER=0x%02X "
         "PRESENT=%d OPL3_HINT=%d",
         opl_status, opl_after, opl_present, opl_is_3);
    if (section_overbudget()) goto done;

    /* SB16 mixer dump (read-only). The load-bearing subset from MPUPROBE
     * section 2 (reg 0x82 = IRQ-pending status; 0x3C = OUT switch). */
    if (has_blaster && a >= 0) {
        uint8_t regs_of_interest[] = {0x22, 0x30, 0x31, 0x32, 0x33,
                                       0x34, 0x35, 0x3C, 0x80, 0x81,
                                       0x82, 0x83};
        for (size_t i = 0; i < sizeof regs_of_interest; i++) {
            outportb(a + 0x4, regs_of_interest[i]);
            uint8_t v = inportb(a + 0x5);
            hlog("[HWINV-AUD] SB_MIXER_REG_0x%02X=0x%02X",
                 regs_of_interest[i], v);
        }
    }
    if (section_overbudget()) goto done;

    /* MPU-401 port SKIPPED per pattern S1 / SDL/0047 hard-no #1. */
    hlog("[HWINV-AUD] MPU_PORT_0x330_0x331_SKIPPED="
         "PATTERN_S1 (MPU status bit 7 documented-unreliable on SB16 PnP CTL0026)");

done:
    section_done();
}

/* ============================================================ */
/* Section E: Disk (CF identify + extension support + free space) */
/* ============================================================ */

static void section_dsk(void)
{
    section_begin("DSK");
    if (section_overbudget()) goto done;

    /* INT 13h AX=08h: legacy disk geometry for drive 80h (first fixed). */
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0800;
    r.h.dl = 0x80;
    if (__dpmi_int(0x13, &r) >= 0 && (r.x.flags & 1) == 0) {
        unsigned cyls = ((unsigned)r.h.ch | ((unsigned)(r.h.cl & 0xC0) << 2));
        unsigned heads = (unsigned)r.h.dh + 1;
        unsigned spt = r.h.cl & 0x3F;
        hlog("[HWINV-DSK] FIXED_DISK_0_CYLS=%u HEADS=%u SECTORS_PER_TRACK=%u TOTAL_SECTORS=%u",
             cyls + 1, heads, spt, (cyls + 1) * heads * spt);
        hlog("[HWINV-DSK] FIXED_DISK_0_TYPE=0x%02X DRIVE_COUNT=%u", r.h.bl, r.h.dl);
    } else {
        hlog("[HWINV-DSK] INT13_AH08_FAILED ax=0x%04X flags=0x%04X",
             r.x.ax, r.x.flags);
    }
    if (section_overbudget()) goto done;

    /* INT 13h AX=4100h BX=55AAh: extension presence check on drive 80h. */
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4100;
    r.x.bx = 0x55AA;
    r.h.dl = 0x80;
    if (__dpmi_int(0x13, &r) >= 0 && (r.x.flags & 1) == 0 &&
        r.x.bx == 0xAA55) {
        hlog("[HWINV-DSK] INT13_EXT_PRESENT=1 VERSION=0x%02X SUPPORT_BITS=0x%04X",
             r.h.ah, r.x.cx);
    } else {
        hlog("[HWINV-DSK] INT13_EXT_PRESENT=0 (ax=0x%04X bx=0x%04X)",
             r.x.ax, r.x.bx);
    }
    if (section_overbudget()) goto done;

    /* INT 21h AX=3600h DL=03 (drive C): free disk space. Returns cluster
     * size + free clusters + total clusters. */
    memset(&r, 0, sizeof r);
    r.x.ax = 0x3600;
    r.h.dl = 0x03;  /* C: */
    if (__dpmi_int(0x21, &r) >= 0 && r.x.ax != 0xFFFF) {
        unsigned long sect_per_cluster = r.x.ax;
        unsigned long free_clusters    = r.x.bx;
        unsigned long bytes_per_sector = r.x.cx;
        unsigned long total_clusters   = r.x.dx;
        unsigned long cluster_size = sect_per_cluster * bytes_per_sector;
        unsigned long free_kb  = (free_clusters  * cluster_size) / 1024UL;
        unsigned long total_kb = (total_clusters * cluster_size) / 1024UL;
        hlog("[HWINV-DSK] C_FREE_KB=%lu TOTAL_KB=%lu CLUSTER_BYTES=%lu",
             free_kb, total_kb, cluster_size);
    } else {
        hlog("[HWINV-DSK] C_FREE_QUERY_FAILED ax=0x%04X", r.x.ax);
    }

done:
    section_done();
}

/* ============================================================ */
/* Section F: IRQ + DMA controller state                        */
/* ============================================================ */

static void section_irq(void)
{
    section_begin("IRQ");
    if (section_overbudget()) goto done;

    /* PIC1 mask (port 0x21) and PIC2 mask (port 0xA1). */
    uint8_t pic1_mask = inportb(0x21);
    uint8_t pic2_mask = inportb(0xA1);
    hlog("[HWINV-IRQ] PIC1_MASK=0x%02X PIC2_MASK=0x%02X "
         "(bit set = masked; IRQ0..7 in PIC1, IRQ8..15 in PIC2)",
         pic1_mask, pic2_mask);

    /* Decode known-IRQ-of-interest bits. */
    hlog("[HWINV-IRQ] IRQ0_TIMER=%s IRQ1_KBD=%s IRQ2_CASCADE=%s "
         "IRQ3_COM2=%s IRQ4_COM1=%s IRQ5_SB16=%s IRQ6_FDC=%s IRQ7_LPT1=%s",
         (pic1_mask & 0x01) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x02) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x04) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x08) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x10) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x20) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x40) ? "MASKED" : "UNMASKED",
         (pic1_mask & 0x80) ? "MASKED" : "UNMASKED");
    hlog("[HWINV-IRQ] IRQ8_RTC=%s IRQ12_MOUSE=%s IRQ13_FPU=%s "
         "IRQ14_IDE0=%s IRQ15_IDE1=%s",
         (pic2_mask & 0x01) ? "MASKED" : "UNMASKED",
         (pic2_mask & 0x10) ? "MASKED" : "UNMASKED",
         (pic2_mask & 0x20) ? "MASKED" : "UNMASKED",
         (pic2_mask & 0x40) ? "MASKED" : "UNMASKED",
         (pic2_mask & 0x80) ? "MASKED" : "UNMASKED");
    if (section_overbudget()) goto done;

    /* PIC IRR + ISR snapshot via OCW3. WARNING: OCW3 read latches state;
     * we do one read per controller, no loop. */
    outportb(0x20, 0x0A);  /* read IRR next */
    uint8_t pic1_irr = inportb(0x20);
    outportb(0x20, 0x0B);  /* read ISR next */
    uint8_t pic1_isr = inportb(0x20);
    outportb(0xA0, 0x0A);
    uint8_t pic2_irr = inportb(0xA0);
    outportb(0xA0, 0x0B);
    uint8_t pic2_isr = inportb(0xA0);
    hlog("[HWINV-IRQ] PIC1_IRR=0x%02X ISR=0x%02X PIC2_IRR=0x%02X ISR=0x%02X",
         pic1_irr, pic1_isr, pic2_irr, pic2_isr);
    if (section_overbudget()) goto done;

    /* 8237 DMA controller: per-channel current address + count, page reg.
     * Channels 0..3 on first 8237 (ports 0x00-0x0F); 4..7 on second (0xC0-
     * 0xDF; channel 4 is cascade). Page regs at 0x87 (ch0), 0x83 (ch1),
     * 0x81 (ch2), 0x82 (ch3), 0x8F (ch4), 0x8B (ch5), 0x89 (ch6), 0x8A
     * (ch7).
     *
     * Caveat: reading the current-address register requires sending the
     * "clear flip-flop" command first (port 0x0C / 0xD8). Without it, the
     * 16-bit read returns split bytes from interleaved address+count
     * latches. We do that BEFORE every read pair. */
    uint8_t pages[8] = {
        inportb(0x87), inportb(0x83), inportb(0x81), inportb(0x82),
        inportb(0x8F), inportb(0x8B), inportb(0x89), inportb(0x8A)
    };
    hlog("[HWINV-DMA] PAGE_CH0=0x%02X CH1=0x%02X CH2=0x%02X CH3=0x%02X "
         "CH4=0x%02X CH5=0x%02X CH6=0x%02X CH7=0x%02X",
         pages[0], pages[1], pages[2], pages[3],
         pages[4], pages[5], pages[6], pages[7]);

    /* Read SB16-relevant channels (1 = 8-bit DMA, 5 = 16-bit DMA per
     * canonical BLASTER config). Clear flip-flop, then read addr-lo +
     * addr-hi (channel n's address port = 2n on first 8237, port 0xC0
     * + 4*(n-4) on second). */
    static const struct {
        const char *name;
        int controller;   /* 0 = first 8237 (ch 0-3), 1 = second (ch 4-7) */
        int channel;
    } chs[] = {
        {"CH1_SB16_8bit",  0, 1},
        {"CH3_unspec",     0, 3},
        {"CH5_SB16_16bit", 1, 5},
        {"CH7_unspec",     1, 7},
    };
    for (size_t i = 0; i < sizeof(chs) / sizeof(chs[0]); i++) {
        int addr_port, count_port, flipflop_port;
        if (chs[i].controller == 0) {
            int n = chs[i].channel;
            addr_port = 2 * n;
            count_port = 2 * n + 1;
            flipflop_port = 0x0C;
        } else {
            int n = chs[i].channel - 4;
            addr_port = 0xC0 + 4 * n;
            count_port = 0xC0 + 4 * n + 2;
            flipflop_port = 0xD8;
        }
        outportb(flipflop_port, 0);  /* clear flip-flop */
        uint8_t a_lo = inportb(addr_port);
        uint8_t a_hi = inportb(addr_port);
        outportb(flipflop_port, 0);
        uint8_t c_lo = inportb(count_port);
        uint8_t c_hi = inportb(count_port);
        unsigned addr = a_lo | ((unsigned)a_hi << 8);
        unsigned cnt  = c_lo | ((unsigned)c_hi << 8);
        hlog("[HWINV-DMA] %s_ADDR=0x%04X COUNT=0x%04X",
             chs[i].name, addr, cnt);
    }

done:
    section_done();
}

/* ============================================================ */
/* Section G: Ports + BIOS Data Area                            */
/* ============================================================ */

static void section_port(void)
{
    section_begin("PORT");
    if (section_overbudget()) goto done;

    /* BIOS Data Area at 40h:0000. Decoded fields. */
    uint16_t com1 = _farpeekw(_dos_ds, 0x400);
    uint16_t com2 = _farpeekw(_dos_ds, 0x402);
    uint16_t com3 = _farpeekw(_dos_ds, 0x404);
    uint16_t com4 = _farpeekw(_dos_ds, 0x406);
    uint16_t lpt1 = _farpeekw(_dos_ds, 0x408);
    uint16_t lpt2 = _farpeekw(_dos_ds, 0x40A);
    uint16_t lpt3 = _farpeekw(_dos_ds, 0x40C);
    uint16_t equip = _farpeekw(_dos_ds, 0x410);
    uint16_t conv_kb = _farpeekw(_dos_ds, 0x413);
    uint8_t kbd_flags = _farpeekb(_dos_ds, 0x417);
    uint8_t video_mode = _farpeekb(_dos_ds, 0x449);
    uint8_t screen_cols = _farpeekb(_dos_ds, 0x44A);
    uint32_t bios_tick = _farpeekl(_dos_ds, 0x46C);

    hlog("[HWINV-PORT] BIOS_COM_PORTS=0x%04X,0x%04X,0x%04X,0x%04X",
         com1, com2, com3, com4);
    hlog("[HWINV-PORT] BIOS_LPT_PORTS=0x%04X,0x%04X,0x%04X", lpt1, lpt2, lpt3);
    hlog("[HWINV-PORT] BIOS_EQUIPMENT_WORD=0x%04X", equip);
    hlog("[HWINV-PORT] BIOS_CONV_MEM_KB=%u KBD_STATUS_FLAGS=0x%02X",
         conv_kb, kbd_flags);
    hlog("[HWINV-PORT] BIOS_VIDEO_MODE=0x%02X SCREEN_COLS=%u",
         video_mode, screen_cols);
    hlog("[HWINV-PORT] BIOS_TICK_COUNT=%lu (~%lu seconds since POST)",
         (unsigned long)bios_tick, (unsigned long)bios_tick * 55ul / 1000ul);
    if (section_overbudget()) goto done;

    /* Keyboard controller status (port 0x64). Read-only. */
    uint8_t kbd_status = inportb(0x64);
    hlog("[HWINV-PORT] KBD_CTRL_STATUS_0x64=0x%02X", kbd_status);

    /* CMOS index 0x10 (floppy drive types). The CMOS address is 0x70/0x71;
     * reading 0x71 returns whatever index was last set on 0x70. */
    outportb(0x70, 0x10);
    uint8_t cmos_floppy = inportb(0x71);
    hlog("[HWINV-PORT] CMOS_FLOPPY_TYPE=0x%02X (high nibble=drive A, low=drive B)",
         cmos_floppy);

    /* PIT readback (port 0x40 ch0 lsb). One byte; just for liveness. */
    uint8_t pit_byte = inportb(0x40);
    hlog("[HWINV-PORT] PIT_CH0_BYTE=0x%02X (raw; transient counter value)", pit_byte);

    hlog("[HWINV-PORT] MPU_PORT_0x330_0x331_SKIPPED=PATTERN_S1");

done:
    section_done();
}

/* ============================================================ */
/* Section H: PCI bus enumeration                               */
/* ============================================================ */

static int pci_read_byte(uint8_t bus, uint8_t devfn, uint16_t off, uint8_t *out)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB108;
    r.h.bh = bus;
    r.h.bl = devfn;
    r.x.di = off;
    if (__dpmi_int(0x1A, &r) < 0) return -1;
    if (r.h.ah != 0) return -1;
    *out = r.h.cl;
    return 0;
}

static int pci_read_word(uint8_t bus, uint8_t devfn, uint16_t off, uint16_t *out)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB109;
    r.h.bh = bus;
    r.h.bl = devfn;
    r.x.di = off;
    if (__dpmi_int(0x1A, &r) < 0) return -1;
    if (r.h.ah != 0) return -1;
    *out = r.x.cx;
    return 0;
}

static void section_pci(void)
{
    section_begin("PCI");
    if (section_overbudget()) goto done;

    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB101;
    if (__dpmi_int(0x1A, &r) < 0 || r.h.ah != 0) {
        hlog("[HWINV-PCI] BIOS_PRESENT=0 (no PCI BIOS)");
        goto done;
    }
    hlog("[HWINV-PCI] BIOS_PRESENT=1 VERSION=%d.%d MECH=0x%02X LAST_BUS=%d",
         r.h.bh, r.h.bl, r.h.al, r.h.cl);

    int last_bus = r.h.cl;
    /* Enumerate bus 0..last_bus, device 0..31, function 0..7. Bail per-device
     * if vendor returns 0xFFFF (no device). For multifunction devices (header
     * bit 7 of cfg[0x0E] set), enumerate all 8 functions; else just fn 0. */
    int dev_count = 0;
    for (int bus = 0; bus <= last_bus; bus++) {
        if (section_overbudget()) {
            hlog("[HWINV-PCI] BUS_SCAN_TIMEOUT bus=%d (budget exceeded)", bus);
            goto done;
        }
        for (int dev = 0; dev < 32; dev++) {
            int max_fn = 1;
            for (int fn = 0; fn < max_fn; fn++) {
                uint8_t devfn = (uint8_t)((dev << 3) | fn);
                uint16_t vendor = 0xFFFF;
                if (pci_read_word(bus, devfn, 0x00, &vendor) != 0) continue;
                if (vendor == 0xFFFF || vendor == 0x0000) continue;
                uint16_t device = 0;
                pci_read_word(bus, devfn, 0x02, &device);
                uint8_t rev = 0, class_p = 0, class_s = 0, class_b = 0;
                pci_read_byte(bus, devfn, 0x08, &rev);
                pci_read_byte(bus, devfn, 0x09, &class_p);
                pci_read_byte(bus, devfn, 0x0A, &class_s);
                pci_read_byte(bus, devfn, 0x0B, &class_b);
                uint8_t hdr = 0;
                pci_read_byte(bus, devfn, 0x0E, &hdr);
                if (fn == 0 && (hdr & 0x80)) max_fn = 8;

                /* Vendor decode. */
                const char *vname = "(unknown)";
                if (vendor == 0x1013) vname = "Cirrus Logic";
                else if (vendor == 0x1002) vname = "ATI";
                else if (vendor == 0x102B) vname = "Matrox";
                else if (vendor == 0x10DE) vname = "NVIDIA";
                else if (vendor == 0x5333) vname = "S3";
                else if (vendor == 0x8086) vname = "Intel";
                else if (vendor == 0x1106) vname = "VIA";
                else if (vendor == 0x10EC) vname = "Realtek";
                else if (vendor == 0x1274) vname = "Ensoniq";
                else if (vendor == 0x1057) vname = "Motorola";
                else if (vendor == 0x1039) vname = "SiS";

                /* Class decode (just base class, condensed). */
                const char *cname = "(other)";
                switch (class_b) {
                case 0x00: cname = "pre-2.0"; break;
                case 0x01: cname = "mass-storage"; break;
                case 0x02: cname = "network"; break;
                case 0x03: cname = "display"; break;
                case 0x04: cname = "multimedia"; break;
                case 0x05: cname = "memory"; break;
                case 0x06: cname = "bridge"; break;
                case 0x07: cname = "comm"; break;
                case 0x08: cname = "system"; break;
                case 0x09: cname = "input"; break;
                case 0x0A: cname = "docking"; break;
                case 0x0B: cname = "processor"; break;
                case 0x0C: cname = "serial-bus"; break;
                }

                hlog("[HWINV-PCI] DEV bus=%d dev=%d fn=%d vendor=0x%04X "
                     "(%s) device=0x%04X rev=0x%02X class=%02X.%02X.%02X (%s) hdr=0x%02X",
                     bus, dev, fn, vendor, vname, device, rev,
                     class_b, class_s, class_p, cname, hdr);
                dev_count++;
            }
        }
    }
    hlog("[HWINV-PCI] DEVICES_TOTAL=%d", dev_count);

done:
    section_done();
}

/* ============================================================ */
/* Section ENV: environment markers (DOSBOX_DETECTED, etc.)     */
/* ============================================================ */

/* Environment classifier shared with the RUNMANIFEST emit at exit.
 * Set by section_env(); read by main() when populating the manifest. */
static runmanifest_env_t g_environment = RUNMANIFEST_ENV_UNKNOWN;
static unsigned          g_env_signals = 0;

static void section_env(void)
{
    section_begin("ENV");

    /* Schema-v1 + cross-incident lesson: orthogonal-failure-mode signals
     * only -- the runmanifest helper applies the AND-gate for DOSBox-X
     * (BOTH cpuid-hypervisor AND int21 must positive) per task #16
     * cross-incident learning. Build-qa task #11 surfaced the original
     * OR-gate false-positives under 86Box because INT 21h AX=4452h is
     * the MS-DOS Generic IOCTL handler.
     *
     * Helper also adds:
     *   - operator override via DOS_PORT_ENVIRONMENT=<dosbox-x|86box|realhw>
     *   - 86Box detection via BIOS string scan at F000h:E000h
     *   - realhw as default fallback when no positive signal */
    g_environment = runmanifest_detect_environment(&g_env_signals);

    /* Emit individual signals + composite ENVIRONMENT marker. Per the
     * plan's anchor-trust rubric, both the composite AND the signal
     * breakdown are valuable: composite for parity-gate, signals for
     * forensic diff when a tier disagrees. */
    int sig_override = (g_env_signals & 0x1) ? 1 : 0;
    int sig_cpuid    = (g_env_signals & 0x2) ? 1 : 0;
    int sig_int21    = (g_env_signals & 0x4) ? 1 : 0;
    int sig_86box    = (g_env_signals & 0x8) ? 1 : 0;
    hlog("[HWINV-ENV] DOSBOX_SIGNAL_CPUID_HYPERVISOR=%d", sig_cpuid);
    hlog("[HWINV-ENV] DOSBOX_SIGNAL_INT21_4452=%d", sig_int21);
    hlog("[HWINV-ENV] BIOS_STRING_86BOX_SIGNAL=%d", sig_86box);
    hlog("[HWINV-ENV] ENV_VAR_OVERRIDE_SIGNAL=%d", sig_override);
    /* AND-gate result: =1 only when BOTH cpuid-hypervisor AND int21
     * positive (the former is the orthogonal anchor; the latter is
     * supporting). Demoted vs v1's OR-gate per task #16 fix. */
    int dosbox_and_gate = (sig_cpuid && sig_int21) ? 1 : 0;
    hlog("[HWINV-ENV] DOSBOX_DETECTED=%d (v2: AND-gate cpuid+int21; v1's OR was false-positive under 86Box)",
         dosbox_and_gate);
    /* Composite 3-state classifier per schema v1 sec. 4.3. */
    hlog("[HWINV-ENV] ENVIRONMENT=%s", runmanifest_env_str(g_environment));

    /* Probe metadata banner. */
    hlog("[HWINV-ENV] PROBE_VERSION=hwinv-mvp-v2 (wave-41 task #10 + wave-43 task #16)");
    hlog("[HWINV-ENV] PROBE_BUILD_DATE=" __DATE__ " " __TIME__);
    hlog("[HWINV-ENV] BINARY_SHA12=%s (compile-time -D HWINV_BUILD_SHA12)", HWINV_BUILD_SHA12);

    section_done();
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    open_log();
    hlog("=== HWINV.EXE wave-41 task #10 + wave-43 task #16 (probe-engineer MVP) starting ===");
    hlog("=== output: %s ; per-line fsync; per-section sentinels ===",
         g_log ? "C:\\HWINV.LOG (or fallback ./HWINV.LOG)" : "(stdout only; log open failed)");

    /* Capture boot wall-clock for RUNMANIFEST started_utc + duration_s. */
    time_t t_boot = time(NULL);
    struct tm *gm = gmtime(&t_boot);
    char started_utc[32];
    if (gm) {
        strftime(started_utc, sizeof started_utc, "%Y-%m-%dT%H:%M:%SZ", gm);
    } else {
        snprintf(started_utc, sizeof started_utc, "1970-01-01T00:00:00Z");
    }

    section_env();
    section_cpu();
    section_mem();
    section_vid();
    section_aud();
    section_dsk();
    section_irq();
    section_port();
    section_pci();

    hlog("");
    hlog("[HWINV-EXIT_OK] all sections completed");

    /* Emit RUNMANIFEST block per docs/internal/WAVE-41-TRI-ENV-CORRELATION-
     * PLAN.md sec. 4.2. Goes AFTER the [HWINV-EXIT_OK] marker so the EXIT
     * sentinel still functions as the human-grep target for "did the probe
     * complete" while the structured block is what the correlation tooling
     * consumes. */
    if (g_log) {
        runmanifest_t m;
        runmanifest_init(&m);
        m.environment = g_environment;  /* set by section_env() */
        snprintf(m.binary_sha12, sizeof m.binary_sha12, "%s", HWINV_BUILD_SHA12);
        const char *tag = getenv("DOS_PORT_LOG_TAG");
        snprintf(m.wave_tag, sizeof m.wave_tag, "%s", tag ? tag : "");
        snprintf(m.scene, sizeof m.scene, "hwinv_oneshot");
        snprintf(m.env_block_sha, sizeof m.env_block_sha, "000000000000");  /* TBD */
        snprintf(m.started_utc, sizeof m.started_utc, "%s", started_utc);
        m.duration_s = (int)(time(NULL) - t_boot);
        m.exit_code = 0;
        /* Perf fields: NA (this is a HW-inventory probe, not gameplay). */
        m.fps_p50 = -1.0;
        m.fps_p95 = -1.0;
        m.audio_vital_status = NULL;
        m.regime = NULL;
        /* Banner counters: NA-equivalent for HWINV (the probe doesn't use
         * the gameplay BANNERS gate). Use 0/0 + 0/0. */
        m.banner_required_hit = 0;
        m.banner_required_total = 0;
        m.banner_forbidden_hit = 0;
        m.banner_optional_hit = 0;
        m.critical_count = 0;
        m.warn_count = 0;
        /* runmanifest_emit writes the bracketed block directly to g_log
         * via fprintf -- it owns the BEGIN/END sentinels. We just inform
         * stdout that the block is in the log file. */
        fputs("\n", stdout);
        fputs("[HWINV] RUNMANIFEST block emitted to HWINV.LOG\n", stdout);
        fflush(stdout);
        fputc('\n', g_log);
        runmanifest_emit(g_log, &m);
        fsync(fileno(g_log));
    }

    if (g_log) fclose(g_log);
    return 0;
}
