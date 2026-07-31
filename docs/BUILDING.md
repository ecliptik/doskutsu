# Building DOSKUTSU

Step-by-step guide for building `DOSKUTSU.EXE` from source on a Linux dev host, testing it in DOSBox-X, and deploying to a Pentium-era DOS machine.

---

## TL;DR -- one command

```bash
./scripts/bootstrap.sh
```

The bootstrap script orchestrates the whole pipeline:

1. Verifies host prerequisites (`cmake`, `git`, `make`, `gcc`, `python3`, `unzip`)
2. Checks for the DJGPP cross-toolchain (prompts with install instructions if missing)
3. Fetches the four vendored upstreams (SDL3, SDL3_mixer, SDL3_image, NXEngine-evo) at pinned SHAs
4. Applies the DOS-port patch series
5. Builds the four-stage chain -> `build/doskutsu.exe`
6. Optionally extracts Cave Story assets when pointed at the freeware `Doukutsu.exe`
7. Stages the runtime layout in `build/stage/`

### With Cave Story assets in one shot

With the 2004 EN freeware `Doukutsu.exe` already on disk, point the script at it:

```bash
./scripts/bootstrap.sh --cave-story-exe /path/to/Doukutsu.exe
```

The script extracts `wavetbl.dat`, `stage.dat`, and `endpic/pixel.bmp` from the EXE into `./data/`, runs the 8.3 rename helper, and stages the runtime layout. The remaining game content (sprites, maps, music, TSC scripts) still needs extracting via `doukutsu-rs` / `NXExtract` / `cavestory.one` and dropping into `./data/` per [ASSETS.md](./ASSETS.md) -- then re-run `./scripts/rename-user-data-83.sh data && make stage` to finish.

### Override locations

```bash
DJGPP_PREFIX=/opt/djgpp ./scripts/bootstrap.sh   # use a system DJGPP install
EMULATORS_ROOT=/elsewhere ./scripts/bootstrap.sh # non-default ~/emulators/ hub path
./scripts/bootstrap.sh --skip-djgpp-check         # bypass the toolchain probe entirely
```

---

## Prerequisites

Required on the dev host (Linux or WSL):

- `cmake` >= 3.16
- `git`, `make`, standard POSIX build tools (`bash`, `awk`, `patch`)
- `python3` -- the asset extractor
- `unzip` -- the bootstrap may unpack a downloaded `Doukutsu.exe` zip
- `dosbox-x` -- `sudo apt install dosbox-x` on Debian/Ubuntu. **Required** for the automated correctness gates: `make smoke` / `make smoke-fast` run the built `DOSKUTSU.EXE` under DOSBox-X to verify it boots, reaches the title, and passes the gameplay-smoke banner gate (Phases 1 + 5). There is no byte-identical oracle for an interactive game, so DOSBox-X execution **is** the build-verification gate. It is also used to generate the Organya pre-render PCM cache (`DOSKUTSU_ORG_PRECACHE_ALL=1` runs the shipped `DOSKUTSU.EXE` headless under DOSBox-X to render the cache on a fast machine before deploying to the CF). Not needed at runtime.
- `scrot`, `xdotool` -- for visible DOSBox-X automation (optional, only needed for playtest + screenshots)
- `zip` -- for `make dist`

Install them on common distros:

```bash
# Debian / Ubuntu
sudo apt install cmake git make gcc python3 unzip zip dosbox-x scrot xdotool

# Fedora
sudo dnf install cmake git make gcc python3 unzip zip dosbox-x scrot xdotool

# Arch (dosbox-x is in the AUR)
sudo pacman -S cmake git make gcc python3 unzip zip scrot xdotool
```

The bootstrap script verifies all of the above and aborts with a clear message if anything is missing.

## DJGPP

The DJGPP cross-compiler is the one prerequisite the bootstrap can't auto-install (the build takes 30-60 minutes; running it without explicit consent isn't friendly). Three options:

**Option 1 -- install via [`andrewwutw/build-djgpp`](https://github.com/andrewwutw/build-djgpp):**

```bash
git clone https://github.com/andrewwutw/build-djgpp.git
cd build-djgpp && ./build-djgpp.sh 12.2.0   # ~30 min
DJGPP_PREFIX=$HOME/djgpp ./scripts/bootstrap.sh
```

**Option 2 -- use an existing DJGPP install:**

```bash
DJGPP_PREFIX=/path/to/djgpp ./scripts/bootstrap.sh
```

**Option 3 -- the shared `~/emulators/` hub (maintainer convenience; new users can ignore this).** This is the DOSKUTSU author's local multi-project layout, not something a new user needs to set up. If `~/emulators/tools/djgpp/` already exists (shared with sibling projects on the same dev host), the bootstrap auto-symlinks `tools/djgpp` to it and no `DJGPP_PREFIX` is needed. Without that hub -- the normal case for a fresh clone -- use Option 1 or 2.

Verify the toolchain is reachable:

```bash
make djgpp-check
```

Expected: `i586-pc-msdosdjgpp-gcc (GCC) 12.2.0` or similar.

## CWSDPMI

`CWSDPMI.EXE` and the other vendored DOS binaries (LFNDOS, DOSLFN) are fetched on demand from URLs in `vendor/binaries.manifest`:

```bash
./scripts/fetch-vendor-binaries.sh           # fetch all four binaries
./scripts/fetch-vendor-binaries.sh --check   # verify sha256 only, no fetch
```

`make stage`, `make dist`, `make install`, and `make dpmi-lfn-smoke` invoke the fetch step automatically as an order-only prerequisite -- runs once, idempotent thereafter. The accompanying license / `.doc` / `.lsm` / `COPYING` files stay tracked because the redistribution licenses require them to ship with their binaries.

## Manual steps (without the bootstrap)

To run each step individually -- e.g., to debug a specific stage -- the bootstrap is just a wrapper around these:

```bash
./scripts/setup-symlinks.sh    # if using the ~/emulators/ hub
./scripts/fetch-sources.sh     # clone vendored upstreams at pinned SHAs
./scripts/apply-patches.sh     # apply the DOS-port patch series
make all                       # build the four-stage chain
make stage                     # produce build/stage/
```

---

## Build

The top-level `Makefile` orchestrates four stages:

```bash
make sdl3          # SDL3 static library (with DOS backend), installs into build/sysroot/
make sdl3-mixer    # SDL3_mixer (WAV + OGG via stb_vorbis), installs into build/sysroot/
make sdl3-image    # SDL3_image (PNG via stb_image),         installs into build/sysroot/
make nxengine      # NXEngine-evo -> build/doskutsu.exe (stubedit'd to 2048K min stack)
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
+-- sysroot/                        # where each stage installs (libs + headers)
+-- sdl3/                           # SDL3's cmake build tree
+-- sdl3-mixer/                     # SDL3_mixer's cmake build tree
+-- sdl3-image/                     # SDL3_image's cmake build tree
+-- nxengine/                       # NXEngine-evo's cmake build tree
`-- doskutsu.exe                    # final artifact
```

Re-running `make nxengine` after an edit in NXEngine-evo source only rebuilds NXEngine, not the SDL stack. `make clean` wipes everything under `build/`; `make distclean` also drops the cloned upstream trees under `vendor/` (keeping only the manifest).

### SETUP.EXE (the configurator)

`SETUP.EXE` is a separate DOS program that writes `DOSKUTSU.CFG` (see [SETUP.md](./SETUP.md)). It has its own `setup/Makefile`, which the top-level `setup` target delegates to:

```bash
make setup                 # build build/setup/setup.exe (scaffold, no SDL link)
make -C setup AUDIOTEST=1   # build the live-audio SETUP.EXE (links the built SDL3 stack)
make setup-test            # host-side unit tests (config loader + recommend matrix)
```

`make setup` builds the no-SDL scaffold (enough to drive the TUI and the `DOSKUTSU.CFG` round-trip); the `AUDIOTEST=1` flavor links the already-built `build/sysroot` SDL3 + SDL3_mixer libraries so SETUP can play the real audio test, and is the flavor `make dist` and `make stage` ship. `make setup-test` runs with the host compiler -- no DOS toolchain or emulator needed.

---

## Test

### Phase 0 / 1 smoke test (automated)

The repository ships a minimal `hello.c` under `tests/smoketest/` that links against DJGPP's libc (no SDL, no CWSDPMI if it can help it) and prints a known string. Two variants exist to exercise both DOSBox-X configs:

```bash
make smoke-fast    # tests/run-smoke.sh with tools/dosbox-x-fast.conf (cycles=max)
make smoke         # tests/run-smoke.sh with tools/dosbox-x.conf (cycles=fixed 40000)
```

`make smoke-fast` is the default during iteration -- it completes in seconds. `make smoke` is the pre-merge gate -- it checks the binary still runs under real-HW-equivalent cycles.

### DOSKUTSU.EXE in DOSBox-X (visible)

Once `make` has produced `build/doskutsu.exe` and `data/` contains extracted Cave Story assets:

```bash
tools/dosbox-launch.sh --exe build/doskutsu.exe              # parity config
tools/dosbox-launch.sh --fast --exe build/doskutsu.exe       # fast config
tools/dosbox-launch.sh --kill-first --exe build/doskutsu.exe # restart cleanly
tools/dosbox-launch.sh --stage --exe build/doskutsu.exe      # mount build/stage/ as C:
```

**`--stage` / `-s` for real game runs.** NXEngine-evo's `ResourceManager` resolves assets via `SDL_GetBasePath() + "data/"` -- i.e. it expects `data/` to live next to the binary. The default launcher mounts the repo root as C:, which works for one-off SDL probes that don't touch `data/`, but a real game run needs the runtime layout: `DOSKUTSU.EXE` + `CWSDPMI.EXE` + `data/` co-located. `--stage` produces that layout under `build/stage/` (binary, DPMI host, and a symlink to `data/`) and mounts it as C: instead. This matches the `C:\DOSKUTSU\` install target on real CF cards. The flag invokes `make stage` automatically -- no need to run it manually.

Use `--stage` whenever actually launching the game (title screen, playtest, smoke runs that load assets). Plain `tools/dosbox-launch.sh` is fine for SDL-driver probes (`tests/sdl3-smoke/sdltest.exe`) that don't read `data/`.

The DOSBox-X window opens on `DISPLAY=:0`. From the same shell, drive it with:

```bash
DISPLAY=:0 scrot -u /tmp/dosbox.png                           # capture focused window
DISPLAY=:0 xdotool search --name DOSBox windowactivate --sync
DISPLAY=:0 xdotool type --delay 40 'DOSKUTSU'
DISPLAY=:0 xdotool key Return

pkill -x dosbox-x                                             # stop (or Ctrl+F9 in window)
```

**Rules of engagement** (same as the sibling projects -- Snow, Basilisk II, vellm):

- `scrot -u` for screenshots. **Never** use ImageMagick `import` (it grabs the X pointer and breaks emulator mouse input).
- Always target `DISPLAY=:0` explicitly -- SSH-forwarded shells may inherit a different `$DISPLAY`.
- Only one DOSBox-X instance at a time. The launcher refuses a second; use `--kill-first` to restart.
- `pkill -x dosbox-x` (exact match), not `-f` -- the `-f` form false-matches any bash subshell whose cmdline mentions `dosbox-x`.

### DOSKUTSU.EXE in DOSBox-X (headless, for CI-ish use)

```bash
tools/dosbox-run.sh --exe build/doskutsu.exe --stdout /tmp/doskutsu.out
```

This runs the binary under `dosbox-x -silent -exit`, captures its stdout to `STDOUT.TXT` inside the ephemeral DOSBox-X C: mount, and copies it out. Interactive games don't produce meaningful stdout, so this mode is primarily for `hello.exe`-class smoke tests; the playtest gate is the visible launcher.

### Two DOSBox-X configs -- when to use which

| Config | `cycles` | Purpose |
|---|---|---|
| `tools/dosbox-x.conf` | `fixed 40000` | **Parity** with Pentium-class hardware. Use for playtest gate, audio-dropout investigations, anything where real-HW-equivalent timing matters. |
| `tools/dosbox-x-fast.conf` | `max` | **Fast iteration.** Use when debugging logic / UI / crash bugs to reach the repro state quickly. Do not use for performance judgments -- 4-8x faster than real HW. |

Both configs are otherwise identical: 48 MB RAM, SB16 on IRQ 5 / DMA 1/5 / base 220, VESA SVGA (`svga_s3` machine), `quit warning = false`.

### Smoke-gate banner-emit verification

`tests/run-gameplay-smoke.sh` runs DOSBox-X at parity cycles, captures the engine + SDL runtime logs, and asserts that each patch's boot banner string was actually emitted at runtime. A string embedded in the binary does not prove its code path ran; emission into the runtime log does. This is the canonical pre-ship gate for any cross-build.

**Why two gates instead of one.** `strings build/doskutsu.exe | grep <banner>` proves the literal compiled into the binary; it does not prove the code path that emits the banner runs. A build once shipped with a patch silently dropped during the cross-build (stale `.obj` linked against post-patch source); the strings-grep gate false-passed because the dropped patch happened to share an env-var name with another patch that DID apply. The runtime banner-emit gate is the direct detector for that failure mode: if the consumer code path is missing or never invoked, the banner doesn't fire, and the gate fails.

**Two-banner-target discipline.** Every patch must carry a patch-id-prefixed unique banner string (e.g. `sdl: SDL/0060 Cirrus BLT pattern-copy (ACTIVE|DISABLED|N/A)`); never share sentinels across patches. Sharing makes the strings-grep gate ambiguous about which patch contributed the literal -- the failure mode above.

**Where banners should fire.** Init-time banners (boot-banner / framebuffer-init / `Renderer::initVideo` head) survive missing-asset early-returns and consumer-wire-up gaps. SDL-side primitives that need ship-verification should hook lazy-init into a known-fire site like `populate_fb_state_for_direct_fb()` so the banner emits even when the consumer hasn't engaged yet. Banners that live deep inside `Renderer::initVideo` after asset loads are fragile against asset-stage gaps; the pattern is to keep banners ahead of any `return` points.

**BANNERS array -- parallel arrays, severity-typed.** The gate is driven by three parallel arrays in `tests/run-gameplay-smoke.sh`: `BANNER_REGEX[]` (regex per banner, alternation-friendly to cover `ENABLED|DISABLED|ACTIVE|N/A` variants under default-flips), `BANNER_SEVERITY[]` -- one of `required` (must emit >=1 line, gate FAILs on absence), `forbidden` (must emit 0 lines, a revert-completeness guard, gate FAILs on presence), or `optional` (informational; logged but no gate effect) -- and `BANNER_LABEL[]` (human-readable label for failure messages). The gate currently ships `forbidden` entries (guarding that a reverted lever's banner is truly gone) and `optional` entries (each shipped lever's boot banner -- the runtime-emit half of the two-witness pattern, read against the captured log). No banner is `required` at present: under the pkill-on-timeout DOSBox-X smoke a clean-exit banner can legitimately not fire, so a hard `required` entry would false-FAIL; `required` stays available for a future banner that provably fires on every run regardless of the kill path. Add a row for each new lever or instrumentation patch when shipping; the gate exits 5 on FAIL.

**Maintenance rule.** When authoring a new lever or instrumentation patch:

1. Add the patch's boot banner regex to `BANNER_REGEX[]` + severity + label, atomic with the patch (same author cycle; not as follow-up).
2. Confirm `strings build/doskutsu.exe | grep <new-patch-id>` after a forced rebuild -- the in-binary check.
3. Run `tests/run-gameplay-smoke.sh` -- the runtime-emit check.
4. Both must pass before the patch is considered shipped.

**After patch application, force a clean rebuild.** CMake's incremental build can silently link a stale `.obj` against the latest source. `make distclean && make` or an explicit `touch vendor/<name>/src/**/*.cpp` chain before `cmake --build` is required after every `make patches`. The smoke-gate catches this latent failure downstream; forced rebuild prevents it upstream.

### SETUP.EXE tests

```bash
make setup-test            # host unit tests: config loader (precedence,
                           # presence-checked skip, authoritative BLASTER) +
                           # the recommend matrix. No DOS toolchain / emulator.
tests/run-setup-e2e.sh     # headless end-to-end: SETUP.EXE -> DOSKUTSU.EXE
```

`tests/run-setup-e2e.sh` spawns its own Xvfb + DOSBox-X, drives the real `SETUP.EXE` CP437 TUI by keystroke through each scenario to write a `DOSKUTSU.CFG`, launches `DOSKUTSU.EXE`, and asserts the engine's startup banners reflect the configured values (config-load count, perf-mode, fixed-timestep, SB16 mixer balance, the authoritative `BLASTER` MPU-401 port, audio-device open). It needs `Xvfb` and `dosbox-x` on `PATH` and a staged tree (`make stage`, which installs `SETUP.EXE` + `SETUP.BAT` beside the game). See [SETUP.md](./SETUP.md) for the full configurator reference.

---

## Deploy

### Build a CF deploy bundle

```bash
make dist
```

Produces `dist/doskutsu-cf.zip` containing:

```
DOSKUTSU.EXE          the game binary (DJGPP-built, stubedit'd)
SETUP.EXE             the hardware / sound configurator (live-audio build)
SETUP.BAT             one-line launcher for SETUP.EXE
CWSDPMI.EXE           DPMI host (fetched from upstream at a pinned checksum)
CWSDPMI.DOC           CWSDPMI redistribution terms
LICENSE.TXT           DOSKUTSU port MIT license
GPLV3.TXT             NXEngine-evo GPLv3 (the dominant license of the binary)
3RDPARTY.TXT          attribution matrix (CRLF)
README.TXT            DOS-readable quick-start + how to obtain Cave Story data
DATA\                 NXEngine-evo's GPLv3 engine support data ONLY -- fonts, UI,
                      StgMeta, endpic. NO Cave Story content. (widescreen
                      bk*480fix.pbm are stripped; unused at 320x240)
```

The bundle ships NXEngine-evo's GPLv3 engine support data, but **not** any Cave Story game content (maps, sprites, music, SFX). Users extract that from the 2004 freeware `Doukutsu.exe` themselves -- see [ASSETS.md](./ASSETS.md). This is the same posture as a Doom source port shipping the engine while the user supplies their own WAD.

To publish this bundle as a tagged GitHub release, see [RELEASING.md](./RELEASING.md).

### Direct install to a mounted CF card

```bash
make install CF=/mnt/cf
```

Copies the same payload to `$CF/DOSKUTSU/`. If Cave Story data is present in the repo `data/` tree at install time, the Makefile also copies it directly to `$CF/DOSKUTSU/DATA/` -- for convenience only, not legal redistribution (the copy lands on a personally-owned CF card, not uploaded anywhere).

### Pre-render the Organya PCM cache (`make org-cache`)

On a slow 486-class target the organya backend cold-renders each song the first time it plays (the title song alone is ~57 s on a DX2-66 -- the historical "hang"). `make org-cache` does that rendering ONCE, here on the fast build host, so the target only ever cache-HITs:

```bash
make                 # build build/doskutsu.exe first
make org-cache       # render every song -> build/orgcache/CACHE/11025_1/*.PCM
```

It runs the built `DOSKUTSU.EXE` headless under DOSBox-X (max cycles) with `DOSKUTSU_ORG_PRECACHE_ALL=1` + `SDL_HINT_DOSKUTSU_AUDIO_TIER2=1` over `data/org/`, producing `CACHE/<rate>_<channels>/*.PCM` (default Tier-2 = 11025 mono -> `CACHE/11025_1/`, ~46 MB / 41 songs). Each PCM is keyed in its header to the rendering binary's `DOSKUTSU_BUILD_SHA12`, so the cache only HITs on that exact binary. To pre-render a specific shipped release exe instead of the current build:

```bash
make org-cache ORGCACHE_EXE=/path/to/DOSKUTSU.EXE
```

Then deploy the produced `CACHE/` tree into the target's game dir next to `DOSKUTSU.EXE` (e.g. `$CF/DOSKUTSU/CACHE/`). Requires `data/org/` (Cave Story data, see `ASSETS.md`); errors cleanly if absent. The DOSBox run is `timeout`-bounded and the process is killed on exit (no orphan). The gameplay-facing counterpart that does this automatically on the target's first launch is `SDL_HINT_DOSKUTSU_ORG_AUTOCACHE` (default ON). (Both `DOSKUTSU_ORG_PRECACHE_ALL` and `SDL_HINT_DOSKUTSU_ORG_AUTOCACHE` are contributor/diagnostic flags, not part of the player-facing `docs/CONFIG.md` reference.)

**Pixtone SFX render cache (`SDL_HINT_DOSKUTSU_PXT_AUTOCACHE`, default ON).** The 117 Cave Story sound effects are synthesized from their `.pxt` definitions at boot; on a Pentium-class box that costs ~51 s on **every** launch, identical across all audio backends. Like the organya cache above, the engine renders the SFX once on first launch, persists the S8/mono/22050 render output to a single tier-independent blob `CACHE\PXT\PXTSFX.BIN` keyed to the binary's `DOSKUTSU_BUILD_SHA12`, and loads that blob on subsequent boots to skip the synth entirely (first boot still pays the ~51 s to build the cache; later boots are fast). One blob serves every backend and every audio tier. Set `SDL_HINT_DOSKUTSU_PXT_AUTOCACHE=0` to disable (boot is byte-identical, just slow -- the synth runs and nothing is read/written). A rebuild invalidates the cache (the SHA changes); swapping the `.pxt` data without a rebuild does not (matching `ORG_AUTOCACHE`). Contributor/diagnostic killswitch, not part of the player-facing `docs/CONFIG.md` reference.

**LICENSING:** the produced `CACHE/` is Cave-Story-DERIVED (rendered from the user's extracted `.org` files). It is a LOCAL / OPERATOR-DEPLOY artifact ONLY and is DELIBERATELY excluded from `make dist` -- the public zip never ships game-derived data. Generate it locally; never upload or redistribute it.

---

## Common errors

### `i586-pc-msdosdjgpp-gcc: command not found`

DJGPP isn't on `PATH`. Ensure `tools/djgpp` is a symlink (`./scripts/setup-symlinks.sh`) and the Makefile is the entry point (not a raw `cmake` call). When invoking CMake directly, export PATH manually:

```bash
export PATH=$PWD/tools/djgpp/bin:$PWD/tools/djgpp/i586-pc-msdosdjgpp/bin:$PATH
```

### `DPMI host not found` on DOS startup

`CWSDPMI.EXE` is not in the current directory or on `PATH`. Ensure both `DOSKUTSU.EXE` and `CWSDPMI.EXE` are in the same directory, or that `CWSDPMI.EXE` is somewhere on `PATH` (e.g. `C:\DOS\`).

### SDL3_mixer or SDL3_image build fails with "undefined reference to dlopen"

A codec backend's dynamic-loader path leaked through. The Makefile passes `-DSDLMIXER_DEPS_SHARED=OFF` / `-DSDLIMAGE_DEPS_SHARED=OFF` to disable the `SDL_LoadObject` codec-loader path on DJGPP (which has no real `dlopen`). On this error, verify those flags survived the CMake invocation -- see the `make sdl3-mixer` / `make sdl3-image` recipes in `Makefile`.

### `fopen("file", "r")` reads short / corrupted bytes

DJGPP defaults to text mode. Always `fopen(path, "rb")` for binary files (sprites, maps, `.org` music, save games). CRLF translation silently corrupts everything.

### VESA mode fails on real hardware but works in DOSBox-X

Real VESA BIOSes vary. If the on-board BIOS doesn't expose VBE 1.2+, load a vendor VBE driver (e.g. `M64VBE.COM` for ATI Mach64, `S3VBE` for S3 cards) before `DOSKUTSU.EXE`. UNIVBE is a generic fallback but often slower. The SDL3 DOS backend probes for VBE 1.2+ linear framebuffer; older VBE 1.0 cards are unsupported.

### Audio plays but is garbled / wrong speed

`BLASTER` environment variable mismatch. SB16-compatible cards typically expect `A220 I5 D1 H5 T6` or similar. Check `SET` output on the target. The SDL3 DOS backend reads `BLASTER` at init.

### Framerate drops / audio stutters in Mimiga Village

Organya synth CPU cost at 22050 stereo is the likely culprit. Fallback is `Mix_OpenAudio(11025, AUDIO_S16SYS, 1, 2048)` -- matches Cave Story's original 2004 spec.

### `make distclean` removed my Cave Story data

`distclean` only wipes `build/` and cloned `vendor/` subdirectories. The repo `data/` tree is user data -- it is gitignored but never touched by the Makefile. If it's gone, re-extract per `ASSETS.md`.

---

## Troubleshooting the build system itself

If the Makefile misbehaves:

- `make -n <target>` -- dry run, shows what would execute
- `make VERBOSE=1 <target>` -- dump the full command lines CMake runs
- `cmake --build build/<stage> --verbose` -- bypass the Makefile, rebuild one stage directly

If a patch fails to apply:

- Run `./scripts/apply-patches.sh` manually and read the `patch` output
- Individual patches are `git format-patch`-style; `git am` applies them in the vendor tree directly for triage
- If upstream has drifted, re-pin the SHA in `vendor/sources.manifest` to an earlier commit, or refresh the patch against the new upstream

If CMake can't find SDL3 / SDL3_mixer / SDL3_image when building a later stage:

- Verify `build/sysroot/lib/pkgconfig/*.pc` or `build/sysroot/lib/cmake/*/` exist
- The Makefile passes `CMAKE_PREFIX_PATH=build/sysroot` -- check the upstream's CMakeLists accepts that
