# vendor/lfndos/

**LFNDOS** is a TSR (Terminate and Stay Resident) driver that adds the Windows 95 long-filename API (`INT 21h` function family `7140h-71A8h`) to plain MS-DOS 6.22, where it doesn't exist natively. We vendor it to use as the *primary candidate* for Phase 8's LFN strategy (see `docs/PHASE8-LFN-DECISION.md`) and as the loaded driver for the DPMI propagation probe at `tests/dpmi-lfn-smoke/`.

## Expected files

```
lfndos.exe    LFNDOS TSR binary (40,910 bytes; ~10 KB resident after load)
lfndos.doc    Author's documentation — required to redistribute under GPLv2's "no warranty" notice section (also has usage instructions)
COPYING       Full text of the GNU General Public License, Version 2 — required by GPLv2 § 1 ("you must keep intact all the notices that refer to this License")
lfndos.lsm    FreeDOS Linux Software Map metadata — declares the license + provenance
```

All four files are **tracked in git** (the `.gitignore` has explicit exceptions) — this matches the `vendor/cwsdpmi/` precedent. The build's `dist` target may eventually depend on them being present at known paths if the Phase 8 g2k probe (`tests/dpmi-lfn-smoke/probe.exe`) confirms LFNDOS-via-CWSDPMI works under real DOS.

## How to obtain

The canonical distribution is the FreeDOS package at `gitlab.com/FreeDOS/drivers/lfndos`. Fetched 2026-04-25 directly from the repo's raw blobs:

```bash
mkdir -p vendor/lfndos
curl -sLo vendor/lfndos/lfndos.exe "https://gitlab.com/FreeDOS/drivers/lfndos/-/raw/master/BIN/LFNDOS.EXE"
curl -sLo vendor/lfndos/lfndos.doc "https://gitlab.com/FreeDOS/drivers/lfndos/-/raw/master/DOC/LFNDOS/LFNDOS.DOC"
curl -sLo vendor/lfndos/COPYING    "https://gitlab.com/FreeDOS/drivers/lfndos/-/raw/master/SOURCE/LFNDOS/COPYING"
curl -sLo vendor/lfndos/lfndos.lsm "https://gitlab.com/FreeDOS/drivers/lfndos/-/raw/master/APPINFO/LFNDOS.LSM"
```

Verify:
- `lfndos.exe` is ~40 KB and starts with the MZ header (`4d 5a` at offset 0). `file vendor/lfndos/lfndos.exe` should print `MS-DOS executable, MZ for MS-DOS`.
- `COPYING` opens with `GNU GENERAL PUBLIC LICENSE / Version 2, June 1991`.
- `lfndos.doc` opens with `LFNDOS: DOS LFN driver (c) 1998, 1999 Chris Jones`.
- `lfndos.lsm` `Copying-policy:` line reads `GNU General Public License, Version 2`.

## License

LFNDOS is **GNU GPL v2** per its bundled `COPYING` file and its FreeDOS LSM declaration. Original author Chris Jones (1998-1999); FreeDOS-maintained since.

GPLv2 redistribution requires:
- Conspicuously publish copyright + disclaimer on each copy → satisfied by shipping `lfndos.doc` (which includes the copyright + warranty disclaimer)
- Distribute under the same license → satisfied by shipping `COPYING` verbatim
- Provide source or written offer to provide source → for the unmodified upstream binary, FreeDOS's own `gitlab.com/FreeDOS/drivers/lfndos/-/tree/master/SOURCE/LFNDOS` satisfies the source-availability requirement; we cite that URL in any downstream `THIRD-PARTY.md` row.

The doskutsu repo's `LICENSE` (MIT) is **not** changed by vendoring LFNDOS — `LFNDOS.EXE` is a standalone runtime binary, not linked into `DOSKUTSU.EXE`. GPLv2 LFNDOS + GPLv3 DOSKUTSU.EXE in the same archive is **mere aggregation** per the GNU GPL FAQ (the license stays scoped to its own file).

See `docs/PHASE8-LFN-DECISION.md` § "License clarification follow-up — 2026-04-25" for the full reasoning that established LFNDOS as the primary LFN-TSR candidate.

## Why this is not in the build graph

`LFNDOS.EXE` is not compiled as part of `make` — it's a pre-built binary we redistribute unmodified. Rebuilding from source would require Borland TASM and TLINK (the upstream's chosen toolchain) plus a Win9x-class DOS environment. We use the FreeDOS-built binary directly.

## Why we vendor a binary at all (rather than asking users to fetch it themselves)

Two reasons:
1. The Phase 8 DPMI-propagation probe (`tests/dpmi-lfn-smoke/`) needs LFNDOS.COM to load as a TSR before the probe runs, in order to exercise the actual question being tested. Without a vendored copy, the probe runner would have to fetch LFNDOS at test time — a network round-trip every CI run, fragile.
2. If/when Phase 8 confirms LFNDOS works under DPMI on g2k, `make dist` will need to bundle LFNDOS.EXE + LFNDOS.DOC + COPYING into `dist/doskutsu-cf.zip` so end users don't have to track down a separate driver. Vendoring now means that step is one Makefile line away when the time comes.
