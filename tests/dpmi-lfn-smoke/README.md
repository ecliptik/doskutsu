# tests/dpmi-lfn-smoke — DPMI LFN propagation probe

Smoke test answering a gating empirical question for real-DOS deployment:
**does CWSDPMI's INT 21h reflector pass LFN-family calls (function codes
7140h-71A8h) through to a real-mode TSR loaded under plain MS-DOS 6.22?**

If yes → bundle LFNDOS.COM (or DOSLFN.COM as fallback). If no → source-rename
every long-named asset (the multi-week fallback).

This probe is the cheapest possible way to settle that question. Without it,
Phase 8 would either commit weeks of source-rename engineering against an
uncertain hypothesis, or ship an LFN TSR that turns out to be a no-op under
DPMI.

## What's here

| File | Role |
|---|---|
| `probe.c` | Probe (MIT, doskutsu-authored). Three tests: 8.3 control, libc-LFN via DJGPP `_use_lfn(1)`, and raw INT 21h AX=716Ch via `__dpmi_int`. |
| `README.md` | This file. |

The build target `dpmi-lfn-smoke` produces `build/dpmi-lfn-smoke/probe.exe`
and the runner script `tests/run-dpmi-lfn-smoke.sh` exercises it under
DOSBox-X for the dev-host baseline.

## What the three tests check

The test fixtures are two paired files staged at the test exe's working
directory by the runner — both single-byte sentinels, distinguished only by
filename:

| Filename | 8.3? | Purpose |
|---|---|---|
| `WAVETABL.DAT` | yes (8 char base, 3 char ext) | control — proves the harness reaches a real DOS |
| `wavetable.dat` | no (9 char base) | the actual LFN test |

Both files are content-identical, but a no-LFN DOS sees only the 8.3 form
(or, depending on the host fs, sees the long form mangled to something
like `WAVET~1.DAT` that the probe doesn't ask for). The presence of both
files in the harness lets us distinguish "filesystem doesn't expose the
long name" from "DOS API can't see it through DPMI."

| Test | Call | Expected on DOSBox-X `lfn=true` | Expected on real DOS + LFN TSR | Expected on real DOS + no LFN TSR |
|---|---|---|---|---|
| `short_baseline` | `open("WAVETABL.DAT")` | PASS | PASS | PASS |
| `long_libc_lfn` | `_use_lfn(1); open("wavetable.dat")` | PASS | PASS *(if DPMI propagates)* | FAIL |
| `long_int21_716c` | `__dpmi_int(0x21, AX=716Ch, ...)` | PASS | PASS *(if DPMI propagates)* | FAIL |

The two long-name tests target the same effective question but isolate the
failure surface:

- If `long_libc_lfn` fails but `long_int21_716c` passes → bug is in DJGPP
  libc's path-handling, not DPMI. (Workaround: skip libc, code direct INT 21h
  in the port.)
- If both fail with `doserr=0x57` (invalid function) → CWSDPMI passes the
  call to real mode but the kernel/TSR doesn't recognize it. Either the
  TSR isn't loaded or it doesn't claim function 716Ch. Re-check
  AUTOEXEC.BAT.
- If both fail with `doserr=0x02` (file not found) → call reached a
  non-LFN-aware INT 21h handler that truncated the long name to 8.3 and
  missed. CWSDPMI's reflector likely strips the LFN function or doesn't
  translate the DS:SI pointer. **This is the failure mode that mandates
  source-rename.**
- If both pass → ship the LFN TSR, source-rename is unnecessary.

## How to run on the dev host (DOSBox-X baseline)

```bash
make dpmi-lfn-smoke              # builds probe.exe + runs against DOSBox-X
                                 # parity config (lfn = true, simulating
                                 # an LFN-capable kernel)
```

The runner:
1. Stages `probe.exe` + paired fixtures (WAVETABL.DAT + wavetable.dat) +
   `cwsdpmi.exe` into a temp DOS volume.
2. Invokes DOSBox-X headless with `lfn = true` (already in `tools/dosbox-x.conf`).
3. Captures stdout via `> STDOUT.TXT` (the probe writes via `printf`, so
   no `2>&1` complications — see `tests/sdl3-smoke/README.md` for the
   DOSBox-X stderr-redirection caveat).
4. Asserts each PROBE line. Exit 0 if all PASS.

The dev-host DOSBox-X run is necessary but not sufficient. It proves
the probe code is correct; it does NOT answer the real-HW DPMI question.
For that, see below.

## How to run on real hardware (the actual question)

1. Copy `build/dpmi-lfn-smoke/probe.exe`, `vendor/cwsdpmi/cwsdpmi.exe`,
   `WAVETABL.DAT`, and `wavetable.dat` to a CF card.
2. Boot the target machine normally (no LFN TSR loaded — control run).
3. Run `PROBE.EXE`. Capture stdout to `PROBE0.LOG` (no LFN baseline).
4. Edit `AUTOEXEC.BAT` to add `LH LFNDOS.COM` (or `LH DOSLFN.COM`). Reboot.
5. Re-run `PROBE.EXE`. Capture to `PROBE1.LOG` (with LFN TSR).
6. Diff the two logs. The `long_libc_lfn` and `long_int21_716c` lines
   are the answer.

Decision pivot:

- LFNDOS-loaded run shows `PROBE: long_int21_716c PASS handle=0x????` →
  **option A confirmed. Ship LFNDOS.** Update `Makefile` `dist` target
  and `THIRD-PARTY.md` accordingly.
- LFNDOS-loaded run still shows `PROBE: long_int21_716c FAIL doserr=0x02`
  → re-test with DOSLFN.COM. If DOSLFN passes, ship DOSLFN. If both fail
  → **option B mandatory. Source-rename** every long-named asset.

## Why the probe doesn't ship LFNDOS itself

The probe's job is to ANSWER the LFN-via-DPMI question; the *deployment*
of LFNDOS is taken only after the probe confirms it would actually help.
Until then, the probe runs on a real-DOS machine that has LFNDOS
independently installed by the operator (per `gitlab.com/FreeDOS/drivers/lfndos`'s
release tarball).

## Citations

- LFN INT 21h API spec (function 716Ch and family): Ralf Brown's Interrupt
  List, INT 21h, AX=716Ch ("Extended Open/Create File"). Quoted in the
  probe source comments.
- DPMI LFN-translation caveat: `github.com/adoxa/doslfn` `doslfn.asm`
  header block.
- DJGPP `_use_lfn()` semantics: `delorie.com/djgpp/v2faq/faq22_16.html`.
- DOSBox-X `lfn = true` baseline: `tools/dosbox-x.conf` (Phase 7 fix sweep
  2026-04-25).
