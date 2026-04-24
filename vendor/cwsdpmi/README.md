# vendor/cwsdpmi/

**CWSDPMI** is the DPMI host required at runtime by DJGPP-compiled programs on DOS. It provides 32-bit protected mode services and is a separate executable — **not** statically linked into `DOSKUTSU.EXE`. It ships alongside the binary in every CF deploy and in `dist/doskutsu-cf.zip`.

## Expected files

```
cwsdpmi.exe    the DPMI host binary
cwsdpmi.doc    CWSDPMI license + documentation (must be redistributed with the binary)
```

Both are **tracked in git** (the `.gitignore` has an explicit exception) because the build's `make dist` target depends on them being present at known paths.

## How to obtain

### Option 1: copy from the sibling `vellm` project

If you have the `vellm` repository checked out alongside this one (both projects share the `~/emulators/` hub), just copy:

```bash
cp ~/git/vellm/vendor/cwsdpmi/cwsdpmi.exe  vendor/cwsdpmi/
cp ~/git/vellm/vendor/cwsdpmi/cwsdpmi.doc  vendor/cwsdpmi/
```

This is the preferred path on a machine that already has `vellm` set up. The binaries are identical — CWSDPMI r7 is r7 regardless of which project vendored it.

### Option 2: download fresh from Charles W. Sandmann's site

CWSDPMI is maintained by its original author. The canonical distribution is at:

- https://sandmann.dotster.com/cwsdpmi/

Download `csdpmi7b.zip` (binary distribution). Extract and copy:

```bash
unzip csdpmi7b.zip
cp bin/CWSDPMI.EXE vendor/cwsdpmi/cwsdpmi.exe
cp bin/CWSDPMI.DOC vendor/cwsdpmi/cwsdpmi.doc
```

Verify the binary is ~20 KB and `cwsdpmi.doc` is a readable text file.

## License

CWSDPMI is freeware with redistribution permitted, provided:
- `CWSDPMI.DOC` is included with the binary
- No fee is charged beyond reasonable media / transfer costs
- The binary is not modified

See `cwsdpmi.doc` for the full terms. DOSKUTSU's `make dist` target includes both `CWSDPMI.EXE` and `CWSDPMI.DOC` in the output zip to satisfy these terms.

## Why this is not in the build graph

`CWSDPMI.EXE` is not compiled as part of `make` — it's a pre-built binary we redistribute unmodified. Rebuilding CWSDPMI from source would require a real-mode 16-bit DOS toolchain (DJGPP's assembler won't produce a DPMI host). Redistributing the upstream binary is the practical approach, and the license permits it.
