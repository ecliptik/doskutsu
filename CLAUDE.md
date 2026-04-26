# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What This Is

`doskutsu` is a port of Cave Story (Doukutsu Monogatari, Pixel 2004) via NXEngine-evo to MS-DOS 6.22 on vintage Pentium-era hardware. The engine is C++11 NXEngine-evo (migrated SDL2 → SDL3 in source per the Path B amendment 2026-04-24 — see `PLAN.md § Plan Amendments § 2026-04-24`), linked statically against SDL3 (with DOS backend from [libsdl-org/SDL PR #15377](https://github.com/libsdl-org/SDL/pull/15377)) + SDL3_mixer + SDL3_image → DJGPP → CWSDPMI. Cross-compiled on Linux. The primary deliverable is `DOSKUTSU.EXE` plus CWSDPMI plus the extracted Cave Story data, fitting on a CF card and booting to the title screen on a Gateway 2000 Pentium OverDrive 83 MHz.

See `PLAN.md` for the phased implementation roadmap. See `DOSKUTSU.md` for the project overview (architecture, data flow, ports touched).

## Target Hardware

**Primary test configuration ("g2k" machine):**

- **CPU:** Intel Pentium OverDrive PODP5V83 (Socket 3, P54C core, 83 MHz, no MMX)
- **RAM:** 48 MB (1 MB conventional, 47 MB XMS)
- **Video:** ATI Mach64 PCI with VESA 1.2+ via `M64VBE.COM`
- **Sound:** Creative Vibra16S (SB16-class, CT2490) on IRQ 5, DMA 1/5, base 220
- **Storage:** CF-to-IDE, ~2 GB with MS-DOS 6.22
- **DPMI:** CWSDPMI r7

**Tiered hardware targets** (full detail in `docs/HARDWARE.md`):

- **Reference (Tier 1):** PODP5V83 / 48 MB / 22050 stereo — what Phase 7 / 8 gates run against.
- **Achievable Minimum (Tier 2):** 486DX2-66 / 16 MB / 11025 mono — expected playable once the Phase 9 audio fallback is wired.
- **Absolute Minimum (Tier 3) — stretch:** 486DX2-50 / 8 MB / 11025 mono + 8bpp indexed + direct-surface blits. Requires the full Phase 9 optimization pass. No real-hardware validation yet.

Hard floors: 486DX with FPU (no 486SX without a 487; DJGPP emits x87), VESA 1.2+ BIOS (UNIVBE acceptable), ≥8 MB RAM after HIMEM.

The g2k boot profile (`[VIBRA]`) is already defined in the g2k repo's CONFIG.SYS and is the canonical target. `doskutsu` does not ship a dedicated CONFIG.SYS profile — see `docs/BOOT.md` for the recommended environment.

## Toolchain

| Component | Location | Install |
|---|---|---|
| DJGPP cross-compiler | `~/emulators/tools/djgpp/` (override via `DJGPP_PREFIX`) | `~/emulators/scripts/update-djgpp.sh` |
| CWSDPMI | `vendor/cwsdpmi/cwsdpmi.exe` | vendored (copy from `vellm/vendor/cwsdpmi/` or download) |
| DOSBox-X (pre-HW test) | system | `sudo apt install dosbox-x` |
| CMake ≥ 3.16 | system | `sudo apt install cmake` |

Toolchain lives under `~/emulators/tools/` alongside the sibling projects (`vellm`, `geomys`, `flynn`). `tools/djgpp` in this repo is a symlink to that hub path; `./scripts/setup-symlinks.sh` creates it. The top-level `Makefile` exports DJGPP's bin dirs onto `PATH` so the four-stage cross-build (SDL3 → SDL3_mixer → SDL3_image → NXEngine-evo) Just Works once the symlink is in place.

See `~/emulators/CLAUDE.md` and `~/emulators/docs/DJGPP.md` for the hub convention.

## Build System

This repo orchestrates four upstream codebases (post-Path-B amendment 2026-04-24 — `vendor/sdl2-compat/` stays cloned but is no longer a build stage). Each is vendored as a snapshot (not a submodule) under `vendor/<name>/`, pinned by SHA in `vendor/sources.manifest`, with DOS-port patches applied from `patches/<name>/*.patch`.

```
vendor/
├── sources.manifest                # URL + ref + pinned SHA per upstream
├── SDL/                            # libsdl-org/SDL (post-PR-#15377)
├── SDL_mixer/                      # libsdl-org/SDL_mixer (SDL3-track release)
├── SDL_image/                      # libsdl-org/SDL_image (SDL3-track release)
├── nxengine-evo/                   # nxengine/nxengine-evo
├── sdl2-compat/                    # libsdl-org/sdl2-compat — cloned, NOT built (Path B)
└── cwsdpmi/                        # DPMI host binary (tracked, not cloned)

patches/
├── SDL/*.patch
├── SDL_mixer/*.patch
├── SDL_image/*.patch
├── nxengine-evo/*.patch            # see patches/nxengine-evo/README.md for layout
└── sdl2-compat/*.patch             # historical, retained but unapplied (Path B)
```

The top-level `Makefile` orchestrates the full chain. Each stage installs into `build/sysroot/` which the next stage consumes via `CMAKE_PREFIX_PATH`. No root needed.

```
make sources     →  scripts/fetch-sources.sh   (clones per manifest)
make patches     →  scripts/apply-patches.sh   (applies patches/)
make sdl3        →  build/sysroot/ gains libSDL3.a
make sdl3-mixer  →  build/sysroot/ gains libSDL3_mixer.a
make sdl3-image  →  build/sysroot/ gains libSDL3_image.a
make nxengine    →  build/doskutsu.exe         (the game binary)
make all         →  the whole chain end-to-end
```

## Development Workflow

```
edit on Linux → make <stage>              # incremental cross-build
              → make smoke-fast           # headless DOSBox-X, cycles=max (quick)
              → make smoke                # headless DOSBox-X, cycles=fixed 40000 (parity)
              → tools/dosbox-launch.sh    # visible DOSBox-X for playtest / screenshots
              → make install CF=/mnt/cf   # user mounts CF, we copy
              → boot real hardware
```

## Key Commands

```bash
# First-time setup
./scripts/setup-symlinks.sh            # links tools/djgpp to ~/emulators/tools/djgpp
~/emulators/scripts/update-djgpp.sh    # installs DJGPP if not present
./scripts/fetch-sources.sh             # clones vendored upstreams at pinned SHAs
./scripts/apply-patches.sh             # applies patches/<name>/*.patch

# Build (individual stages)
make sdl3
make sdl2-compat
make sdl2-mixer
make sdl2-image
make nxengine                          # produces build/doskutsu.exe

# Build (everything)
make                                   # equivalent to `make all`

# Test (DOSBox-X)
make smoke-fast                        # Phase 0 hello.exe, cycles=max
make smoke                             # Phase 0 hello.exe, parity cycles
tools/dosbox-launch.sh                 # visible DOSBox-X, repo mounted as C:
tools/dosbox-launch.sh --exe build/doskutsu.exe   # auto-run on launch
tools/dosbox-launch.sh --fast          # use dosbox-x-fast.conf
tools/dosbox-launch.sh --kill-first    # restart cleanly

# Deploy
make dist                              # produces dist/doskutsu-cf.zip
make install CF=/mnt/cf                # direct copy onto mounted CF card

# Cleanup
make clean                             # removes build/
make distclean                         # clean + removes cloned vendor/ trees (keeps manifests)
```

## DOSBox-X Interaction (parity with Snow / Basilisk workflow)

`tools/dosbox-launch.sh` brings DOSBox-X up on the local X session (`DISPLAY=:0`) so Claude Code — or a human — can screenshot it and drive it with xdotool, matching how the Mac pipeline runs Snow and Basilisk.

```bash
tools/dosbox-launch.sh --exe build/doskutsu.exe     # visible launch, repo as C:
tools/dosbox-launch.sh --fast                       # same, with cycles=max config

DISPLAY=:0 scrot -u /tmp/dosbox.png                 # capture focused window
DISPLAY=:0 xdotool search --name DOSBox windowactivate --sync
DISPLAY=:0 xdotool type --delay 40 'DOSKUTSU.EXE'
DISPLAY=:0 xdotool key Return

pkill -x dosbox-x                                   # stop it (or Ctrl+F9 in window)
```

**Rules:**

- Use `scrot -u` for screenshots. The Snow-era rule against ImageMagick `import` (it grabs the X pointer and breaks emulator mouse input) generalizes; don't use it here either.
- Always target `DISPLAY=:0` explicitly for scrot/xdotool. Shells invoked by Claude Code may inherit an SSH-forwarded `$DISPLAY` that isn't the user's visible desktop. `dosbox-launch.sh` forces `:0` itself; override with `DOSBOX_DISPLAY=...` if genuinely needed.
- Do not run multiple DOSBox-X instances simultaneously — they contend for the audio device and make `xdotool search --name DOSBox` ambiguous. The launcher refuses a second instance; use `--kill-first` to restart cleanly.
- `xdotool type` uses literal strings. Use `xdotool key Return` for Enter. Use `--delay 40` on `type` — DOSBox-X occasionally drops keys with zero delay.
- `tools/dosbox-run.sh` (headless) and `tools/dosbox-launch.sh` (visible) are separate tools with different jobs. Don't collapse them.
- Both configs (`dosbox-x.conf` parity, `dosbox-x-fast.conf` fast) set `quit warning = false` — DOSBox-X's default would block scripted shutdown on a modal dialog.

## Critical Rules

### DJGPP / DOS

- **Always `fopen(path, "rb")`.** DJGPP defaults to text mode; CRLF translation silently corrupts binaries (sprites, maps, music).
- **`size_t` is 32-bit on DJGPP.** Upstream code (especially NXEngine-evo, written against modern glibc) may assume 64-bit. Audit `ftell`, `off_t`, buffer-size math.
- **Stubedit stack.** Default DPMI stack is 256 KB. `DOSKUTSU.EXE` needs `stubedit doskutsu.exe minstack=2048k` as a post-link step (the Makefile does this for all `.exe` artifacts).
- **No MMX / SSE / SIMD.** P54C predates MMX. Target flags are `-march=i486 -mtune=pentium -O2` per plan — runs on any 486DX+ with FPU, scheduled for Pentium.
- **CWSDPMI must ship alongside `DOSKUTSU.EXE`.** Include in every CF deploy and dist zip. See `docs/BOOT.md`.
- **No shared libs.** SDL3 DOS backend is static-only (`SDL_LoadObject` is unsupported). Every `SDL_BUILD_SHARED=OFF`, every `BUILD_SHARED_LIBS=OFF`. Everything gets linked into `doskutsu.exe`.

### SDL3 DOS backend quirks

- **Renderer must be `SDL_RENDERER_SOFTWARE`.** The DOS backend has no accelerated renderer. NXEngine-evo currently calls `SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED)` at `vendor/nxengine-evo/src/graphics/Renderer.cpp:119` — one of our patches forces software.
- **Cooperative threading scheduler.** SDL3 DOS backend yields in its event pump and in `SDL_Delay`. NXEngine-evo doesn't spawn threads (verified), so this is fine — but we must never introduce `std::thread` or `SDL_CreateThread` in port glue.
- **Audio recording is unsupported.** Don't accidentally enable any Mix_* API that touches capture.
- **PR #15377 author explicitly states "no real hardware testing."** We will be the first real-HW users. Budget debugging time. When behavior diverges between DOSBox-X and g2k, trust g2k.
- **`SDL_INVALID_PARAM_CHECKS=0` cannot suppress NULL-pointer "invalid" errors.** SDL3's NULL guard at `vendor/SDL/src/SDL_utils_c.h:79` runs *before* the validation kill-switch — a literal NULL texture trips the "Parameter 'texture' is invalid" path regardless of hint state. Use the env var (or `SDL_SetHintWithPriority(...,"0",OVERRIDE)`) only for genuine hash-validation issues, not when chasing NULL pointers. (Surfaced by tasks #5 / #9 during the Phase 7 framebuffer-wall investigation.)
- **`SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER` defaults `"0"`; without flipping it, the normal-path framebuffer flush silently writes nowhere visible** despite `SDL_RenderPresent` reporting success at every layer. `patches/nxengine-evo/0032-sdl3-dos-fast-framebuffer-hint.patch` flips the hint to `"1"` programmatically before `SDL_Init`. The actual unblock mechanism is a side-effect during `fb_state` init, **not** fast-path engagement (per `patches/SDL/0002-debug-dosvesa-framebuffer-trace.patch`'s evidence: 900+ post-flip flushes still hit the normal path). The latent upstream bug is tracked as task #24 — **no-action by policy**.
- **`DEBUG.LOG` (and our `sdldbg.log`) only flush to disk on DOSBox-X exit.** `pkill -x dosbox-x` before `grep`'ing logs; mid-run reads return 0 bytes. The natural debugging instinct (launch → grep log → kill) is wrong on DOS — the grep step has to come *after* the kill. `tests/run-gameplay-smoke.sh` enforces this. (Surfaced by nxengine, 2026-04-25.)

### NXEngine-evo specifics

- **Lock window to 320x240 fullscreen at runtime.** Widescreen and Full HD code paths remain compiled in for possible future use — do not rip them out. The runtime lock lives in the DOS-port patch set.
- **Audio init target:** Path B's audio refactor (`patches/nxengine-evo/0013-0017`) keeps NXEngine-evo's upstream sample rate (`#define SAMPLE_RATE 44100`) so the migration's audio-correctness gate has no behavioral change bundled with the SDL2→SDL3 API change. **Phase 9 lever 1** is what reduces sample rate downstream: 22050 stereo for Tier 1 (PODP83 / 48 MB reference target) and 11025 mono for Tier 2/3 fallback (matches Cave Story's 2004 original spec). See `docs/HARDWARE.md § Hardware tiers` for per-tier audio targets and `PLAN.md § Phase 9 lever 1` for the rate-reduction work. The original prescription (start at 22050 stereo) reflected an incorrect assumption that 22050 was upstream's default — surfaced during #31 work.
- **`-fno-rtti` yes, `-fno-exceptions` no.** Resolved 2026-04-24 by software-architect ratification of nxengine's #27 audit:
  - **`-fno-rtti`** is a pure code-size win — NXEngine-evo has zero `dynamic_cast` / `typeid` hits, so the flag is safe to enable unconditionally. It lands in `patches/nxengine-evo/0002-dos-target-flags.patch`.
  - **Do NOT enable `-fno-exceptions`.** NXEngine-evo has 6 `nlohmann::json` parse sites that depend on exception propagation to convert *"log + skip the malformed asset, keep playing"* into *"abort the process"*. That's a bad trade for a port we want to be modder-friendly — a corrupt mod file should not crash the binary. The flag would shrink the binary further but at unacceptable runtime cost. See `PLAN.md § Plan Amendments § 2026-04-24` Phase 4'd row for the full reasoning.
- **Drop JPEG dep.** NXEngine-evo's `CMakeLists.txt` has `find_package(JPEG REQUIRED)` but Cave Story ships no `.jpg` assets — one of our patches removes this.

### Vendoring

- **Snapshots, not submodules.** `vendor/<name>/` is a working tree the `fetch-sources.sh` script populates by `git clone`-ing the SHA from `vendor/sources.manifest`. All cloned trees except `vendor/cwsdpmi/` are gitignored.
- **Patches live in `patches/<name>/*.patch`.** Numeric prefixes drive application order (`0001-*.patch`, `0002-*.patch`, ...). Use `git format-patch` to produce them. Reason each patch exists in its commit message — "why DOS needs this," not "what it does."
- **Patches stay local; never upstreamed** (policy decision 2026-04-25). Our `patches/<name>/*.patch` are workspace-local artifacts, not upstream contributions. We do not open PRs against `libsdl-org/SDL`, `libsdl-org/sdl2-compat`, `libsdl-org/SDL_mixer`, `libsdl-org/SDL_image`, or `nxengine/nxengine-evo` from this work. **This sidesteps `vendor/SDL/CLAUDE.md`'s no-AI-authoring restriction** entirely — that restriction scopes to PR-style contributions, which we commit to never doing. The trade is real: every upstream sync (rebasing the patch series against a new pinned SHA) is on us; freedom-to-patch beats upstream alignment for this project's purposes. If a fix is so generally useful that someone wants it upstream, that's a separate human-authored effort outside this repo.
- **Annotate every DOS-specific deviation in our port glue with `// DOS-PORT:`.** This is only for code we write ourselves (not imported patches). vellm's pattern.

### Licensing (read this before adding dependencies)

- The repo's `LICENSE` is **MIT** — it covers our original source (Makefile, scripts, port glue, docs).
- The **distributed binary `DOSKUTSU.EXE` is GPLv3** because it statically links NXEngine-evo (GPLv3). MIT on our source doesn't contradict this; the combined binary work takes the dominant license.
- **Any new dependency must be GPLv3-compatible** (MIT, BSD, zlib, Apache 2.0, public domain, LGPL via static linking with exception). No proprietary libs, no GPL-incompatible licenses (original 4-clause BSD with the advertising clause, etc.).
- **Patches against GPLv3 upstreams (NXEngine-evo)** are derivative works and therefore GPLv3, not MIT — regardless of this repo's `LICENSE`. This is how GPLv3 works for derivative code; it's not a conflict. Our MIT license still correctly describes our non-derivative original code.
- **The `dist` target must include** `LICENSE.TXT` (MIT), `GPLV3.TXT` (NXEngine-evo's license), `CWSDPMI.DOC` (CWSDPMI redistribution terms), and `THIRD-PARTY.TXT` (attribution matrix). All CRLF-normalized for DOS.
- **Cave Story game data never lands in this repo or the dist zip.** Users extract it themselves per `docs/ASSETS.md`. This keeps us clear of Pixel's freeware terms and prevents NXEngine-evo's GPLv3 from attempting to re-license game data via inclusion.
- See `PLAN.md § Licensing` for the full component matrix and redistribution checklist.

## Correctness Gate

Unlike vellm (cross-toolchain byte-identical output), doskutsu has no deterministic byte-level oracle — it's an interactive game. The correctness gates are:

1. **Phase 1 smoke test** (`make smoke-fast`): `hello.exe` links, runs under CWSDPMI, prints expected text, exits 0 in DOSBox-X.
2. **Phase 5 build gate:** `doskutsu.exe` links cleanly and reaches the title screen in DOSBox-X.
3. **Phase 7 play gate:** 30 min continuous DOSBox-X session covering: Mimiga Village dialogue, First Cave combat, Egg Corridor entry, save/load cycle. No crashes, no audio dropout, no visible corruption.
4. **Phase 8 real-HW gate:** same checklist on g2k, with `--fast` DOSBox config used only for iteration between real-HW sessions.

`tests/run-smoke.sh` is the automated gate for (1). (2)-(4) are human-in-the-loop.

## Do Not

- Do not replace the sdl2-compat bridge with direct SDL3 unless Phase 3 truly fails — it's the documented fallback in `PLAN.md`, not plan A.
- Do not enable SIMD (MMX/SSE/SSE2) flags. P54C predates all of them.
- Do not vendor upstream repos as git submodules — snapshots per `vendor/sources.manifest` are the convention. Freedom to patch > easy pulls.
- Do not modify toolchain files under `~/emulators/tools/djgpp/` directly. That directory is shared retro-build infrastructure; install-only.
- Do not commit the Cave Story `data/base/` assets. They are freeware, but we do not redistribute them from this repo. `docs/ASSETS.md` tells users how to obtain them.
- Do not ship `DOSKUTSU.EXE` without `CWSDPMI.EXE` alongside. Document this in the deploy docs.
- Do not push directly to remotes other than Forgejo. The primary is `ssh://git@forgejo.ecliptik.com/ecliptik/doskutsu.git`.
- Do not add a dependency without checking its license is GPLv3-compatible (MIT, BSD-3, zlib, Apache 2.0, public domain, LGPL). If unsure, ask before adding. `PLAN.md § Licensing` is the reference.

## Agent Teams

When creating a team to work the DOSKUTSU plan, include these roles (modeled on Geomys/Flynn's team structure with DOS-specific domain experts):

- **Team Lead** — owns phase sequencing, decides when a gate is passed, arbitrates between competing experts
- **Software Architect** — designs port glue, arbitrates SDL2-compat-vs-direct-SDL3 questions, reviews cross-phase decisions
- **DJGPP / DOS systems expert** — memory layout, DPMI constraints, `size_t` audits, stubedit, CWSDPMI behavior, real-mode-vs-protected-mode gotchas
- **SDL engine expert** — SDL3 DOS backend internals, SDL2 → SDL3 API deltas, sdl2-compat forwarding behavior, SDL_mixer / SDL_image configuration
- **NXEngine / Cave Story expert** — Organya synth, Pixtone, Renderer.cpp internals, TSC script format, `.pxm` / `.pxe` / `.pxa` file formats
- **Build Engineer** — owns the five-stage Makefile, CMake toolchain files, `fetch-sources.sh` / `apply-patches.sh`, CF packaging, deploy
- **QA / Playtest Engineer** — runs DOSBox-X playthroughs, owns the Phase 7 / 8 gates, files regressions, maintains the playtest checklist
- **Real-hardware validation engineer** — owns g2k testing, boot profile debugging, BIOS/VESA/BLASTER tuning, memory-budget verification on real hardware
- **Technical writer** — keeps README, CHANGELOG, PLAN, BUILDING, docs/ in sync as phases complete

**Never use git worktrees when working with agent teams** (inherited convention from Geomys/Flynn — avoids cross-worktree patch confusion).

**Only one agent launches DOSBox-X at a time.** The launcher script enforces this, but the team-level convention is: QA or playtest engineer owns the running DOSBox-X window; other engineers check before they screenshot.

## Repository Conventions

- Primary remote: `ssh://git@forgejo.ecliptik.com/ecliptik/doskutsu.git`
- Feature branches for phase work; squash-merge to `main` on phase gate pass
- Always include `Co-Authored-By: Claude Code` in commits (prepare-commit-msg hook may enforce — check if configured)
- Maintain: `README.md`, `CHANGELOG.md`, `TODO.md`, `PLAN.md`, `BUILDING.md`
- Do NOT commit: `build/`, `vendor/<cloned>/`, `data/base/` game assets, `dist/`, `*.exe` except `vendor/cwsdpmi/cwsdpmi.exe`

## Documentation

- `README.md` — user-facing: requirements, features, usage, quick build
- `PLAN.md` — phased implementation roadmap with decision log
- `BUILDING.md` — prerequisites, symlink setup, DJGPP install, five-stage build, DOSBox-X testing, deploy, common errors
- `DOSKUTSU.md` — project overview: what we're porting, what sits on top of what, cross-project links
- `THIRD-PARTY.md` — full attribution matrix for vendored + runtime-shipped components
- `TODO.md` — current phase's open tasks, future features, known bugs
- `CHANGELOG.md` — release notes (semantic versioning)
- `docs/ASSETS.md` — how to obtain and extract Cave Story 2004 EN freeware data
- `docs/HARDWARE.md` — g2k reference config, DOSBox-X calibration vs real HW
- `docs/BOOT.md` — recommended DOS boot profile (HIMEM, NOEMS, BLASTER, VESA, CTMOUSE)
- `vendor/sources.manifest` — pinned upstream SHAs
- `patches/README.md` — patch set convention and maintenance
