/*
 * probe.c — DPMI INT 21h LFN propagation smoke test (Phase 8 prerequisite).
 *
 * Answers: does CWSDPMI's INT 21h reflector pass LFN-family calls (function
 * codes 7140h-71A8h) through to a real-mode TSR (LFNDOS.COM, DOSLFN.COM)
 * loaded under plain MS-DOS 6.22?
 *
 * The architectural concern (raised in DOSLFN's own asm header):
 *
 *   "Most Protected Mode DOS extenders doesn't translate the LFN API to
 *    Real Mode... For DPMI programs, it is up to the programmer to translate
 *    these DOS calls to Real Mode, using DPMI services at INT31."
 *
 * DJGPP-built DOSKUTSU.EXE runs under CWSDPMI. NXEngine-evo issues plain
 * fopen() calls for files like "wavetable.dat" (9-char base, breaks 8.3).
 * If CWSDPMI's INT 21h reflector strips the LFN function codes — or fails
 * to translate the DS:SI ASCIZ pointer across the protected/real-mode
 * boundary correctly — then loading an LFN TSR on g2k won't help, and
 * we have to fall back to source-renaming every long-named asset.
 *
 * This probe runs THREE tests against a paired fixture (same byte-content
 * staged under two names — one 8.3-clean, one not):
 *
 *   1. open(SHORT_NAME, O_RDONLY)         — control. Pure 8.3, always works.
 *   2. open(LONG_NAME,  O_RDONLY)         — DJGPP libc with _use_lfn(1).
 *   3. INT 21h AX=716Ch via __dpmi_int    — bypasses libc, pure DPMI path.
 *
 * Exit code is the count of FAILing assertions (0 = all PASS). Output is
 * line-prefixed for grep-friendly assertion in run-dpmi-lfn-smoke.sh.
 *
 * Interpretation:
 *
 *   Test 1 must pass everywhere (proves the harness reaches a real DOS).
 *   Tests 2/3 pass on DOSBox-X with `lfn = true` — emulator bakes LFN in
 *     at the kernel layer with no DPMI server in the loop. This is the
 *     baseline we run on the dev host.
 *   Tests 2/3 on g2k with no LFN TSR loaded must FAIL (control: confirms
 *     real-HW behaves as theorized).
 *   Tests 2/3 on g2k with LFNDOS.COM (or DOSLFN.COM) loaded — answers the
 *     load-bearing question. If they pass: ship the LFN TSR per option A
 *     of docs/PHASE8-LFN-DECISION.md. If they fail: source-rename per
 *     option B is mandatory.
 *
 * Test 3 (raw INT 21h via __dpmi_int) isolates the DPMI question from the
 * libc question — if test 2 fails but test 3 passes, the failure is in
 * DJGPP's libc, not CWSDPMI. If both fail, CWSDPMI's reflector is the
 * culprit.
 *
 * License: MIT (this file). See LICENSE in repo root.
 */

#include <dos.h>
#include <dpmi.h>
#include <errno.h>
#include <fcntl.h>
#include <go32.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <unistd.h>

/* DJGPP's LFN handling: the LFN= environment variable controls whether
 * path-resolution functions (open, fopen, opendir, ...) issue the
 * LFN-extended INT 21h variants vs. the legacy 8.3 calls. We force it
 * on at probe startup via setenv() so the test result doesn't depend on
 * how the harness was invoked. The libc function _use_lfn(path) is a
 * QUERY — it returns whether LFN is currently active for a given path,
 * not a setter — used here only for diagnostic reporting. */

/* Paired fixtures staged into C:\ by the test harness. Both files contain
 * the same single-byte sentinel ('!'). Each name targets a different LFN
 * code path; the long form's 9-char base (10 incl. dot, plus 3 ext) does
 * not fit 8.3 and must hit the LFN API to open. */
static const char SHORT_NAME[] = "WAVETABL.DAT"; /* 8.3-clean control */
static const char LONG_NAME[]  = "wavetable.dat"; /* breaks 8.3 base */

static int test_short_baseline(void)
{
    int fd = open(SHORT_NAME, O_RDONLY);
    if (fd >= 0) {
        printf("PROBE: short_baseline PASS fd=%d\n", fd);
        close(fd);
        return 0;
    }
    printf("PROBE: short_baseline FAIL errno=%d (%s)\n",
           errno, strerror(errno));
    return 1;
}

static int test_long_libc_lfn(void)
{
    /* Diagnostic: report whether DJGPP libc thinks LFN is active for the
     * target. _use_lfn() returns 1 if LFN is enabled for the given path,
     * 0 otherwise. This depends on LFN= env var + filesystem capability
     * of the volume containing the path. */
    int lfn_active = _use_lfn(LONG_NAME);
    printf("PROBE: long_libc_lfn _use_lfn(%s)=%d\n", LONG_NAME, lfn_active);

    int fd = open(LONG_NAME, O_RDONLY);
    if (fd >= 0) {
        printf("PROBE: long_libc_lfn PASS fd=%d\n", fd);
        close(fd);
        return 0;
    }
    printf("PROBE: long_libc_lfn FAIL errno=%d (%s)\n",
           errno, strerror(errno));
    return 1;
}

/* Issue INT 21h AX=716Ch (LFN Extended Open/Create) directly via DPMI,
 * bypassing DJGPP libc. The filename ASCIZ string must live in DOS-addressable
 * memory below 1 MB — DJGPP's __tb (transfer buffer) is the canonical
 * place for that. */
static int test_long_int21_716c(void)
{
    unsigned long tbuf = __tb; /* permanent DOS-segment scratch */
    size_t i;

    for (i = 0; LONG_NAME[i] != '\0'; i++) {
        _farpokeb(_dos_ds, tbuf + i, (unsigned char)LONG_NAME[i]);
    }
    _farpokeb(_dos_ds, tbuf + i, 0); /* NUL terminator */

    __dpmi_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x716C; /* function: extended open/create */
    regs.x.bx = 0x0000; /* mode: read-only, no sharing flags */
    regs.x.cx = 0x0000; /* attr: normal (only relevant on create) */
    regs.x.dx = 0x0001; /* action: open existing, fail if missing */
    regs.x.ds = (unsigned)(tbuf >> 4);  /* real-mode segment */
    regs.x.si = (unsigned)(tbuf & 0xF); /* real-mode offset */
    regs.x.di = 1;      /* autoencode (default) */

    int rc = __dpmi_int(0x21, &regs);
    if (rc != 0) {
        printf("PROBE: long_int21_716c FAIL __dpmi_int rc=%d\n", rc);
        return 1;
    }
    if (regs.x.flags & 1) {
        /* Carry set → AX is a DOS error code. 0x02 = file not found,
         * 0x03 = path not found, 0x05 = access denied, 0x57 = invalid
         * function (no LFN driver), etc. */
        printf("PROBE: long_int21_716c FAIL doserr=0x%04X\n", regs.x.ax);
        return 1;
    }

    unsigned int handle = regs.x.ax;
    printf("PROBE: long_int21_716c PASS handle=0x%04X\n", handle);

    /* Close via INT 21h AH=3Eh so we don't leak the handle. */
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x3E;
    regs.x.bx = handle;
    __dpmi_int(0x21, &regs);
    return 0;
}

int main(void)
{
    printf("PROBE: dpmi-lfn-smoke v1\n");

    /* Force LFN= on for the libc path. setenv() takes immediate effect on
     * the next path-handling call. The runner could also `SET LFN=y` in
     * RUN.BAT, but doing it here keeps the probe self-contained and means
     * its result doesn't depend on the harness configuration. */
    setenv("LFN", "y", 1);

    int fails = 0;
    fails += test_short_baseline();
    fails += test_long_libc_lfn();
    fails += test_long_int21_716c();
    printf("PROBE: done fails=%d\n", fails);
    return fails;
}
