# vendor/doslfn/

**DOSLFN** is the *fallback* TSR LFN driver candidate for Phase 8 (LFNDOS at `vendor/lfndos/` is the primary). It implements the Windows 95 long-filename API for plain DOS and shipped originally from Henrik Haftmann (2001-2003), now maintained by Jason Hood (2003-present).

Vendored here for the DPMI propagation probe at `tests/dpmi-lfn-smoke/`. LFNDOS is preferred when both work because LFNDOS has an explicit GPLv2 license declaration; DOSLFN's only license declaration is FreeDOS's "Freeware w/sources" LSM line. But DOSLFN is more actively maintained and has broader filesystem coverage (Joliet CD support, network mounts) — relevant when LFNDOS's "must have direct disk access" requirement excludes a target environment (notably DOSBox-X's `MOUNT C` host-redirector — see `tests/dpmi-lfn-smoke/README.md`).

## Expected files

```
doslfn.com    DOSLFN TSR binary (21,316 bytes; FreeDOS-default build, broader DOS-version coverage)
doslfnms.com  DOSLFN TSR binary (20,396 bytes; MS-DOS-specific build per upstream's makefile %1=ms variant)
doslfn.txt    Author's documentation (English; the upstream multi-language docs include DE/ES/FR/TR translations not shipped here)
doslfn.lsm    FreeDOS LSM metadata declaring "Freeware w/sources"
```

All four are **tracked in git** (the `.gitignore` has explicit exceptions). Same precedent as `vendor/cwsdpmi/` and `vendor/lfndos/`.

## Which binary to use

- **`doslfnms.com`**: prefer this on real MS-DOS 6.22. Built specifically for the MS-DOS environment per the upstream's `tasm doslfn.asm,doslfn%1.obj,doslfn%1.lst` build with `%1=ms`. Smaller (20 KB) and avoids any FreeDOS-specific code paths that MS-DOS doesn't expose.
- **`doslfn.com`**: the FreeDOS-default build. Use under DOSBox-X / FreeDOS / DR-DOS / DOSEMU. Broader environment detection.

The probe runner (`tests/run-dpmi-lfn-smoke.sh --variant tsr-doslfn`) defaults to `doslfn.com` for DOSBox-X testing; the real-hardware operator step in `tests/dpmi-lfn-smoke/README.md` recommends `doslfnms.com`.

## How to obtain

```bash
mkdir -p vendor/doslfn
curl -sLo vendor/doslfn/doslfn.com   "https://gitlab.com/FreeDOS/drivers/doslfn/-/raw/master/BIN/doslfn.com"
curl -sLo vendor/doslfn/doslfnms.com "https://gitlab.com/FreeDOS/drivers/doslfn/-/raw/master/BIN/doslfnms.com"
curl -sLo vendor/doslfn/doslfn.txt   "https://gitlab.com/FreeDOS/drivers/doslfn/-/raw/master/DOC/DOSLFN/doslfn.txt"
curl -sLo vendor/doslfn/doslfn.lsm   "https://gitlab.com/FreeDOS/drivers/doslfn/-/raw/master/APPINFO/DOSLFN.LSM"
```

Verify:
- `doslfn.com` is ~21 KB; `file` reports `DOS executable (COM)`.
- `doslfnms.com` is ~20 KB; same `file` output.
- `doslfn.txt` opens with `* Für den deutschen Text...` (mixed-language doc).
- `doslfn.lsm` `Copying-policy:` line reads `Freeware w/sources`.

## License

DOSLFN is **"Freeware w/sources"** per its FreeDOS LSM, with explicit reference to `https://en.wikipedia.org/wiki/License-free_software`. The upstream `github.com/adoxa/doslfn` repo has no `LICENSE` / `COPYING` file (verified 2026-04-25 via the GitHub contents API), so the FreeDOS LSM is the canonical declaration we lean on. FreeDOS's package vetting concluded redistribution is safe; that's what we cite.

This is meaningfully weaker evidence than LFNDOS's explicit GPLv2 — which is why LFNDOS is the primary recommendation. We vendor DOSLFN here as the empirically-tested fallback for environments where LFNDOS doesn't load (notably DOSBox-X). If real-hardware testing confirms LFNDOS works there, DOSLFN can be dropped from the dist zip later.

The doskutsu repo's `LICENSE` (MIT) is unchanged by vendoring DOSLFN — it's a standalone TSR, not linked into `DOSKUTSU.EXE`. Ships in `dist/doskutsu-cf.zip` (when shipped) under "mere aggregation" semantics, alongside (not combined with) the GPLv3 binary.

DOSLFN is the fallback LFN-TSR candidate; LFNDOS at `vendor/lfndos/` is primary based on its stronger license declaration.

## Why two binary variants

DOSLFN's upstream build accepts a `%1` argument naming the target environment. Two were shipped:

```bash
tasm doslfn.asm,doslfn.obj,doslfn.lst        # → doslfn.com  (default, multi-environment)
tasm -DMSDOS doslfn.asm,doslfnms.obj,...     # → doslfnms.com (MS-DOS-specific)
```

The MS-DOS-specific build hardcodes some assumptions valid only on real MS-DOS (3.30+) kernels and shaves ~1 KB. We ship both because the right binary for DOSBox-X's emulated DOS is empirically different from the right binary for real MS-DOS 6.22 — and the cost of vendoring a second small `.com` is negligible.

## Why this is not in the build graph

Same as `vendor/cwsdpmi/` and `vendor/lfndos/`: pre-built binary, redistributed unmodified. Rebuilding from source would require Borland TASM 4.1 + TLINK 7.1.30.1 (per `doslfn.txt`) — an old commercial toolchain we don't have.
