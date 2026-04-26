# Building DOSKUTSU

Step-by-step guide for building `DOSKUTSU.EXE` from source on a Linux dev host, testing it in DOSBox-X, and deploying to a Pentium-era DOS machine.

---

## Prerequisites

Required on the dev host (Linux or WSL):

- `cmake` >= 3.16
- `git`, `make`, standard POSIX build tools (`bash`, `awk`, `patch`)
- `dosbox-x` — `sudo apt install dosbox-x` on Debian/Ubuntu
- `scrot`, `xdotool` — for visible DOSBox-X automation (optional, only needed for playtest + screenshots)
- `zip` — for the `dist` target

DJGPP is installed via the shared `~/emulators/` hub — see below.

## One-time setup

### 1. Link to the shared `~/emulators/` toolchain hub

The DJGPP cross-compiler lives under `~/emulators/tools/djgpp/` alongside the sibling projects (`vellm`, `geomys`, `flynn`). A convenience symlink at `tools/djgpp` lets this repo's Makefile path-reference it without assuming `$HOME`.

```bash
./scripts/setup-symlinks.sh
```

This creates `tools/djgpp -> ~/emulators/tools/djgpp`. The Makefile adds `tools/djgpp/bin` and `tools/djgpp/i586-pc-msdosdjgpp/bin` (for `stubedit`) to `PATH` automatically.

### 2. Install DJGPP (if not already present)

```bash
~/emulators/scripts/update-djgpp.sh
```

Under the hood this wraps [`andrewwutw/build-djgpp`](https://github.com/andrewwutw/build-djgpp) and builds GCC 12.2.0 + binutils + DJGPP libc into `~/emulators/tools/djgpp/`. The build takes 30-60 minutes. You can skip it if the toolchain is already installed — the script detects existing installs.

Verify:

```bash
make djgpp-check
```

Expected output: `i586-pc-msdosdjgpp-gcc (GCC) 12.2.0` or similar.

### 3. Place CWSDPMI

`CWSDPMI.EXE` is the DPMI host that DJGPP-built programs need on DOS. It is vendored per-project.

```bash
# Option A: copy from the sibling vellm project
cp ~/git/vellm/vendor/cwsdpmi/cwsdpmi.exe vendor/cwsdpmi/
cp ~/git/vellm/vendor/cwsdpmi/cwsdpmi.doc vendor/cwsdpmi/

# Option B: download from https://sandmann.dotster.com/cwsdpmi/
# (see vendor/cwsdpmi/README.md for the exact URL and verification steps)
```

The Makefile refuses to build the `dist` target if either file is missing.

### 4. Fetch the upstream sources

```bash
./scripts/fetch-sources.sh
```

This clones the upstreams listed in `vendor/sources.manifest` at the pinned SHAs:

- `vendor/SDL/` — libsdl-org/SDL (post-PR-#15377 main)
- `vendor/SDL_mixer/` — libsdl-org/SDL_mixer release-3.2.x (SDL3-track)
- `vendor/SDL_image/` — libsdl-org/SDL_image release-3.2.x (SDL3-track)
- `vendor/nxengine-evo/` — nxengine/nxengine-evo master
- `vendor/sdl2-compat/` — libsdl-org/sdl2-compat main (cloned but **not built**; retained for reference)

All five directories are gitignored; only `vendor/sources.manifest` and `vendor/cwsdpmi/` are tracked.

### 5. Apply DOS-port patches

```bash
./scripts/apply-patches.sh
```

Applies every `patches/<name>/*.patch` to `vendor/<name>/` in lexical order. Re-running is safe — the script first `git reset --hard` to the pinned SHA, then reapplies.

---

## Build

The top-level `Makefile` orchestrates four stages:

```bash
make sdl3          # SDL3 static library (with DOS backend), installs into build/sysroot/
make sdl3-mixer    # SDL3_mixer (WAV + OGG via stb_vorbis), installs into build/sysroot/
make sdl3-image    # SDL3_image (PNG via stb_image),         installs into build/sysroot/
make nxengine      # NXEngine-evo → build/doskutsu.exe (stubedit'd to 2048K min stack)
```

Each stage depends on the previous one's installed output via `CMAKE_PREFIX_PATH=build/sysroot`. No root required.

Or in one go:

```bash
make                # equivalent to: make all
```

The full end-to-end build from a clean tree takes ~10-15 minutes depending on host CPU.

### Incremental rebuilds

Each stage has a per-stage build directory under `build/`:

```
build/
├── sysroot/                        # where each stage installs (libs + headers)
├── sdl3/                           # SDL3's cmake build tree
├── sdl3-mixer/                     # SDL3_mixer's cmake build tree
├── sdl3-image/                     # SDL3_image's cmake build tree
├── nxengine/                       # NXEngine-evo's cmake build tree
└── doskutsu.exe                    # final artifact
```

Re-running `make nxengine` after an edit in NXEngine-evo source only rebuilds NXEngine, not the SDL stack. `make clean` wipes everything under `build/`; `make distclean` also drops the cloned upstream trees under `vendor/` (keeping only the manifest).

---

## Test

### Phase 0 / 1 smoke test (automated)

The repository ships a minimal `hello.c` under `tests/smoketest/` that links against DJGPP's libc (no SDL, no CWSDPMI if it can help it) and prints a known string. Two variants exist to exercise both DOSBox-X configs:

```bash
make smoke-fast    # tests/run-smoke.sh with tools/dosbox-x-fast.conf (cycles=max)
make smoke         # tests/run-smoke.sh with tools/dosbox-x.conf (cycles=fixed 40000)
```

`make smoke-fast` is the default during iteration — it completes in seconds. `make smoke` is the pre-merge gate — it checks the binary still runs under real-HW-equivalent cycles.

### DOSKUTSU.EXE in DOSBox-X (visible)

Once `make` has produced `build/doskutsu.exe` and `data/` contains extracted Cave Story assets:

```bash
tools/dosbox-launch.sh --exe build/doskutsu.exe              # parity config
tools/dosbox-launch.sh --fast --exe build/doskutsu.exe       # fast config
tools/dosbox-launch.sh --kill-first --exe build/doskutsu.exe # restart cleanly
tools/dosbox-launch.sh --stage --exe build/doskutsu.exe      # mount build/stage/ as C:
```

**`--stage` / `-s` for real game runs.** NXEngine-evo's `ResourceManager` resolves assets via `SDL_GetBasePath() + "data/"` — i.e. it expects `data/` to live next to the binary. The default launcher mounts the repo root as C:, which works for one-off SDL probes that don't touch `data/`, but a real game run needs the runtime layout: `DOSKUTSU.EXE` + `CWSDPMI.EXE` + `data/` co-located. `--stage` produces that layout under `build/stage/` (binary, DPMI host, and a symlink to `data/`) and mounts it as C: instead. This matches the `C:\DOSKUTSU\` install target on real CF cards. The flag invokes `make stage` automatically — no need to run it manually.

Use `--stage` whenever you're actually launching the game (title screen, playtest, smoke runs that load assets). Plain `tools/dosbox-launch.sh` is fine for SDL-driver probes (`tests/sdl3-smoke/sdltest.exe`) that don't read `data/`.

The DOSBox-X window opens on `DISPLAY=:0`. From the same shell, you can drive it:

```bash
DISPLAY=:0 scrot -u /tmp/dosbox.png                           # capture focused window
DISPLAY=:0 xdotool search --name DOSBox windowactivate --sync
DISPLAY=:0 xdotool type --delay 40 'DOSKUTSU'
DISPLAY=:0 xdotool key Return

pkill -x dosbox-x                                             # stop (or Ctrl+F9 in window)
```

**Rules of engagement** (same as the sibling projects — Snow, Basilisk II, vellm):

- `scrot -u` for screenshots. **Never** use ImageMagick `import` (it grabs the X pointer and breaks emulator mouse input).
- Always target `DISPLAY=:0` explicitly — SSH-forwarded shells may inherit a different `$DISPLAY`.
- Only one DOSBox-X instance at a time. The launcher refuses a second; use `--kill-first` to restart.
- `pkill -x dosbox-x` (exact match), not `-f` — the `-f` form false-matches any bash subshell whose cmdline mentions `dosbox-x`.

### DOSKUTSU.EXE in DOSBox-X (headless, for CI-ish use)

```bash
tools/dosbox-run.sh --exe build/doskutsu.exe --stdout /tmp/doskutsu.out
```

This runs the binary under `dosbox-x -silent -exit`, captures its stdout to `STDOUT.TXT` inside the ephemeral DOSBox-X C: mount, and copies it out. Interactive games don't produce meaningful stdout, so this mode is primarily for `hello.exe`-class smoke tests; the playtest gate is the visible launcher.

### Two DOSBox-X configs — when to use which

| Config | `cycles` | Purpose |
|---|---|---|
| `tools/dosbox-x.conf` | `fixed 40000` | **Parity** with Pentium-class hardware. Use for playtest gate, audio-dropout investigations, anything where real-HW-equivalent timing matters. |
| `tools/dosbox-x-fast.conf` | `max` | **Fast iteration.** Use when you're debugging logic / UI / crash bugs and just want to get to the repro state quickly. Do not use for performance judgments — 4-8x faster than real HW. |

Both configs are otherwise identical: 48 MB RAM, SB16 on IRQ 5 / DMA 1/5 / base 220, VESA SVGA (`svga_s3` machine), `quit warning = false`.

---

## Deploy

### Build a CF deploy bundle

```bash
make dist
```

Produces `dist/doskutsu-cf.zip` containing:

```
DOSKUTSU.EXE          the binary (DJGPP-built, stubedit'd)
CWSDPMI.EXE           DPMI host (from vendor/cwsdpmi/)
CWSDPMI.DOC           CWSDPMI redistribution terms
LICENSE.TXT           DOSKUTSU port MIT license
GPLV3.TXT             NXEngine-evo GPLv3 (the dominant license of the binary)
THIRD-PARTY.TXT       attribution matrix (CRLF)
README.TXT            DOS-readable quick-start + how to obtain Cave Story data
```

`dist/doskutsu-cf.zip` does **not** include Cave Story game data. Users must extract it from the 2004 freeware `Doukutsu.exe` themselves — see [docs/ASSETS.md](./docs/ASSETS.md).

### Direct install to a mounted CF card

```bash
make install CF=/mnt/cf
```

Copies the same payload to `$CF/DOSKUTSU/`. If Cave Story data is present at `data/base/` at install time, the Makefile also copies it to `$CF/DOSKUTSU/DATA/BASE/` — for convenience only, not legal redistribution (the copy is happening on your own CF card, not being uploaded anywhere).

---

## Common errors

### `i586-pc-msdosdjgpp-gcc: command not found`

DJGPP isn't on `PATH`. Ensure `tools/djgpp` is a symlink (`./scripts/setup-symlinks.sh`) and the Makefile is the entry point (not a raw `cmake` call). If you're invoking CMake directly, export PATH manually:

```bash
export PATH=$PWD/tools/djgpp/bin:$PWD/tools/djgpp/i586-pc-msdosdjgpp/bin:$PATH
```

### `DPMI host not found` on DOS startup

`CWSDPMI.EXE` is not in the current directory or on `PATH`. Ensure both `DOSKUTSU.EXE` and `CWSDPMI.EXE` are in the same directory, or that `CWSDPMI.EXE` is somewhere on `PATH` (e.g. `C:\DOS\`).

### SDL3_mixer or SDL3_image build fails with "undefined reference to dlopen"

A codec backend's dynamic-loader path leaked through. The Makefile passes `-DSDLMIXER_DEPS_SHARED=OFF` / `-DSDLIMAGE_DEPS_SHARED=OFF` to disable the `SDL_LoadObject` codec-loader path on DJGPP (which has no real `dlopen`). If you see this error, verify those flags survived your CMake invocation — see the `make sdl3-mixer` / `make sdl3-image` recipes in `Makefile`.

### `fopen("file", "r")` reads short / corrupted bytes

DJGPP defaults to text mode. Always `fopen(path, "rb")` for binary files (sprites, maps, `.org` music, save games). CRLF translation silently corrupts everything.

### VESA mode fails on real hardware but works in DOSBox-X

Real VESA BIOSes vary. If the on-board BIOS doesn't expose VBE 1.2+, load a vendor VBE driver (e.g. `M64VBE.COM` for ATI Mach64, `S3VBE` for S3 cards) before `DOSKUTSU.EXE`. UNIVBE is a generic fallback but often slower. The SDL3 DOS backend probes for VBE 1.2+ linear framebuffer; older VBE 1.0 cards are unsupported.

### Audio plays but is garbled / wrong speed

`BLASTER` environment variable mismatch. SB16-compatible cards typically expect `A220 I5 D1 H5 T6` or similar. Check `SET` output on the target. The SDL3 DOS backend reads `BLASTER` at init.

### Framerate drops / audio stutters in Mimiga Village

Organya synth CPU cost at 22050 stereo is the likely culprit. Fallback is `Mix_OpenAudio(11025, AUDIO_S16SYS, 1, 2048)` — matches Cave Story's original 2004 spec.

### `make distclean` removed my Cave Story data

`distclean` only wipes `build/` and cloned `vendor/` subdirectories. `data/base/` is yours — it is gitignored but never touched by the Makefile. If it's gone, re-extract per `docs/ASSETS.md`.

---

## Troubleshooting the build system itself

If the Makefile misbehaves:

- `make -n <target>` — dry run, shows what would execute
- `make VERBOSE=1 <target>` — dump the full command lines CMake runs
- `cmake --build build/<stage> --verbose` — bypass the Makefile, rebuild one stage directly

If a patch fails to apply:

- Run `./scripts/apply-patches.sh` manually and read the `patch` output
- Individual patches are `git format-patch`-style; you can `git am` them in the vendor tree directly for triage
- If upstream has drifted, re-pin the SHA in `vendor/sources.manifest` to an earlier commit, or refresh the patch against the new upstream

If CMake can't find SDL3 / SDL3_mixer / SDL3_image when building a later stage:

- Verify `build/sysroot/lib/pkgconfig/*.pc` or `build/sysroot/lib/cmake/*/` exist
- The Makefile passes `CMAKE_PREFIX_PATH=build/sysroot` — check the upstream's CMakeLists accepts that
