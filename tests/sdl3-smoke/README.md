# tests/sdl3-smoke — Phase 2d SDL3 DOS-backend smoke test

Phase 2 gate (#14): exercise the SDL3 DOS backend (PR #15377) under DOSBox-X
to confirm `libSDL3.a` links, the DPMI runtime survives `SDL_Init`, the VESA
video driver returns at least one display, and the SoundBlaster audio driver
returns at least one playback device.

## What's here

| File | Role |
|---|---|
| `sdltest.c` | Probe (MIT, doskutsu-authored). Same coverage as upstream's `testaudioinfo` + `testdisplayinfo` combined, but writes via `printf()` to stdout instead of `SDL_Log()` to stderr. |

The build target `sdl3-smoke` produces `build/sdl3-smoke/sdltest.exe` and
runs it via `tests/run-sdl3-smoke.sh`. Captured stdout lands at
`build/sdl3-smoke/sdltest.log`, and an annotated copy is written to
`tests/fixtures/sdl3-modes-dosbox.txt` as the Phase 8 real-hardware
comparison baseline.

## Why a doskutsu-authored probe instead of upstream's tests

The original plan was to compile `vendor/SDL/test/testaudioinfo.c` and
`vendor/SDL/test/testdisplayinfo.c` directly against `libSDL3.a`. Both are
flagged `SDL_NONINTERACTIVE 1` in upstream's CMake, so they exit on their
own without keyboard input. They link cleanly under DJGPP. **The blocker is
output capture.**

Upstream's tests log via `SDL_Log()`, which writes to **stderr** unconditionally
(`vendor/SDL/src/SDL_log.c:808`: `fprintf(stderr, ...)`). Our headless
DOSBox-X harness redirects stdout to a file via `> STDOUT.TXT` in a generated
`RUN.BAT`. Merging stderr into stdout would require `2>&1` syntax, which
**neither MS-DOS COMMAND.COM nor DOSBox-X's built-in shell support**:
DOSBox-X parses the `2>` token as a separate stderr-redirect to a file
literally named `&1` (verified empirically — the `&1` file appears in the
stage with zero bytes). Result: capture is empty, gate cannot pass.

Two paths to fix were considered and rejected:

1. **Patch SDL test source to call `SDL_SetLogOutputFunction(stdout_writer, ...)`.**
   Conflicts with `vendor/SDL/CLAUDE.md` ("AI must not be used to generate code
   for contributions to this project"). Even maintaining the patch in
   `patches/SDL/` long-term is awkward — the policy reads as a request not to
   produce SDL-derivative code via LLMs even when it's never submitted upstream.

2. **Wait for DOSBox-X / DJGPP to support `2>&1`.** DJGPP's argv-level
   redirection is real but only kicks in for tokens left after COMMAND.COM
   parsing — and DOSBox's shell consumes `2>` first. Even if we worked around
   it, MS-DOS 6.22 on real hardware won't behave the same, so the gate would
   break on real hardware anyway. Shell-agnostic capture is the only future-
   proof answer.

So we author this small probe in our own tree (MIT, doskutsu-only), call the
same public SDL3 APIs the upstream tests do, and write to stdout via printf.
Equivalent coverage, shell-agnostic.

## What we explicitly skip and why

- **Renderer test (`testdraw.c`).** Keyboard-driven exit only — no
  `--frames N` flag in this SDL SHA. Auto-exit would require patching SDL
  test source (rejected above). The audio + video init gate alone is
  sufficient to confirm both DOS-backend subsystems initialize. Renderer
  smoke deferred to Phase 5 (NXEngine itself is the renderer harness).
- **Audio playback test (`testaudio`).** Produces a tone — not verifiable
  from captured stdout.
- **`testkeyboard`, `testevents`.** Interactive.
- **Anything pulling in SDL_image or SDL_mixer.** Those layers don't exist
  yet (Phases 3–4).

## Output format

Every line starts with a fixed prefix so the runner can grep for known-stable
substrings instead of byte-matching the whole capture:

```
SDLTEST-BEGIN: doskutsu Phase 2d smoke (SDL3 X.Y.Z)
AUDIO-DRIVERS: count=N
AUDIO-DRIVER: 0 dosaudio
AUDIO-CURRENT: dosaudio
AUDIO-DEVICES-playback: count=N
DEVICE-playback: 0 SoundBlaster
AUDIO-DEVICES-recording: count=0
VIDEO-DRIVER: dosvesa
VIDEO-DISPLAYS: count=1
DISPLAY: 1 name=... bounds=WxH@0,0
MODE-CURRENT: 1 WxH HHz fmt=...
MODES: 1 count=N
MODE: 1 0 320x200 ... fmt=...
MODE: 1 1 640x480 ... fmt=...
...
SDLTEST-END: rc=0 audio=0 video=0
```

`SDLTEST-BEGIN:` and `SDLTEST-END: rc=0` together prove the exe started,
both subsystems returned, and the process exited cleanly. The runner's
required-substring list (`tests/run-sdl3-smoke.sh`) is the canonical gate.

## Running

```bash
make sdl3-smoke              # build + run (single exe, single test target)
```

Then inspect:

```bash
cat build/sdl3-smoke/sdltest.log               # full capture
cat tests/fixtures/sdl3-modes-dosbox.txt       # Phase 8 baseline (committed)
```

## Real-hardware baseline (`tests/fixtures/sdl3-modes-dosbox.txt`)

Committed snapshot of the probe's output under `tools/dosbox-x.conf`
(parity config). For real-hardware testing, run the same `sdltest.exe`
on the target machine and diff against this fixture. Differences in the
VESA mode list, SB device naming, or display bounds are the early-
warning signal that PR #15377's DOS backend behaves differently on real
hardware than under DOSBox-X's emulation.
