# Gameplay smoke test — what it covers, what it doesn't, how to read the output

`tests/run-gameplay-smoke.sh` drives `DOSKUTSU.EXE` through a scripted keyboard sequence in a visible DOSBox-X session, capturing screenshots at named milestones and the engine's logs after the run. It is the floor of "does the binary run a playable game", not a substitute for the Phase 7 stability gate (a 30-min human-driven session) or for real-hardware (g2k) validation.

## What the smoke verifies

| Layer | How it's checked |
|---|---|
| Binary boots | DOSBox-X launches without error; `dosbox-x` PID exists for the run |
| Title screen renders | Screenshot `02-post-title.png` (after a 5s settle and one Z press) shows the Cave Story title with menu |
| Engine accepts keyboard input | A second Z press advances past the title to the opening dialogue |
| First playable scene loads | Screenshots `07-jumped.png` / `08-final.png` show the opening room (tile-rendered floor, Quote sprite, broken teleporter, dialogue box) |
| No render-pipeline regression | `debug.log` contains zero `drawSurface ... 'texture' is invalid` lines (gated by patches 0030 + 0032) |
| No new criticals | `debug.log` `[critical]` count is 0 |
| Init logs sane | `sdldbg.log` shows DOSVESA framebuffer state populated correctly (`banked_multibank=1`, non-NULL `pixels`) and source pixel data being written each flush |

## What the smoke does NOT verify (still requires a human)

- **Visual correctness.** Screenshots prove pixels appear, not that they're the *right* pixels — sprite alignment, palette fidelity, text legibility, scrolling smoothness all need eyes.
- **Audio.** No audio-capture path on the test bot. Organya music + Pixtone SFX could be silent, distorted, or fine; the smoke can't tell.
- **Long-running stability.** Heap fragmentation, palette drift, and memory leaks only appear over the 30-min Phase 7 stability gate.
- **Save/load round-trip integrity.** The smoke runs once forward; round-trip needs a separate sequence.
- **Real-hardware behavior.** DOSBox-X's emulated VESA + SB16 + DPMI are not byte-identical to a Pentium with Mach64 + Vibra16S + CWSDPMI. Per `CLAUDE.md`, "trust g2k when DOSBox-X and real diverge."

## Expected output sequence for a healthy run

`tests/run-gameplay-smoke.sh --out /tmp/gameplay-smoke` writes (default --fast config):

| File | Expected content |
|---|---|
| `01-title.png` | Engine init / fade-in (the screenshot fires at t=5s; under `--fast` the title may not have fully painted yet — small ~2KB image with a dark blue-black rectangle is normal) |
| `02-post-title.png` | Cave Story title screen with menu (cursor on "New game", ~10KB) |
| `03-mid-scene.png` | Same title screen (cursor still on menu, ~10KB) |
| `04-after-z2.png` | Opening dialogue: "From somewhere, a transmission..." in a dialogue box, dark-blue background (~3KB) |
| `05-moved-right.png` | Same dialogue (input is locked during dialogue — not a regression) |
| `06-moved-left.png` | Same dialogue |
| `07-jumped.png` | First room: tile-rendered metallic floor, Quote sprite, broken teleporter, door (~10KB) |
| `08-final.png` | First room with dialogue box: "Connecting to network... / Logged on." (~10KB) |
| `debug.log` | NXEngine logs — see "expected noise" below |
| `sdldbg.log` | SDL3-DOS framebuffer init dump + per-100-call source-pixel spot-checks (~21 lines) |
| `launcher.log` | DOSBox-X stderr/stdout |
| `results.txt` | Per-step log written by the smoke script |

The screenshot byte-size pattern alternates between **~10KB (rendered scene)** and **~3KB (dialogue-overlay or blank)** — both are healthy. Pure all-black (~1.5KB) at any milestone after `02-post-title.png` would indicate a render regression.

## Expected noise (not a regression)

These error-log lines are expected and harmless; do NOT count them against the smoke:

| Line | Source | Why it's expected |
|---|---|---|
| `Couldn't open file settings.dat.` | NXEngine first-run | The pref-path settings file doesn't exist yet on a fresh stage; engine creates it (or fails silently) on first save. Cosmetic. |
| `Surface::LoadImage: load failed of 'data/endpic/pixel.bmp' ... ENOENT` | NXEngine | Known data-extract gap (`tasks #17`). The sprite-sheet manifest references `pixel.bmp`; the freeware extract this repo currently uses doesn't include it. Patch 0030 silences the downstream NULL-render flood. |
| `Renderer::drawSurface: NULL texture from Surface@... ; first occurrence` | NXEngine, patch 0030 | Single line per unique Surface; throttled. Documents the pixel.bmp consequence. Periodic summary line every 500 NULL renders is also expected. |

What WOULD count as a regression:

- More than ~5 `[error]` lines in `debug.log`
- Any `[critical]` line
- Any `drawSurface ... 'texture' is invalid` (patch 0030 + 0032 should keep this at zero)
- Any `SDL_RenderTexture failed` line that is NOT the throttled NULL-texture log from 0030
- `sdldbg.log` showing `direct_fb=0` AND `pixels=0x00000000` past the first call (would mean fb_state collapsed mid-run)
- Screenshots stuck at all-black after `02-post-title.png`

## 2026-04-25 baseline run

First successful gameplay smoke after the Phase 7 framebuffer wall closure (commit pending — patches 0027-0032 + SDL/0002):

```
[gameplay-smoke] dosbox-x started, PID 699201
[gameplay-smoke] DOSBox window focused
[gameplay-smoke] screenshot 01-title.png (2142 bytes)        # init/fade
[gameplay-smoke] sent key: z
[gameplay-smoke] screenshot 02-post-title.png (9943 bytes)   # title screen
[gameplay-smoke] screenshot 03-mid-scene.png (10070 bytes)   # title still
[gameplay-smoke] sent key: z
[gameplay-smoke] screenshot 04-after-z2.png (2950 bytes)     # "From somewhere, a transmission..."
[gameplay-smoke] screenshot 05-moved-right.png (2950 bytes)  # dialogue locks input
[gameplay-smoke] screenshot 06-moved-left.png (2980 bytes)   # dialogue locks input
[gameplay-smoke] sent key: z
[gameplay-smoke] screenshot 07-jumped.png (9786 bytes)       # first room visible
[gameplay-smoke] screenshot 08-final.png (10675 bytes)       # "Connecting to network..."
[gameplay-smoke] debug.log: 3 errors, 0 criticals, 0 drawSurface-invalid
[gameplay-smoke] sdldbg.log: 21 lines
```

The 3 errors were the known noise above (settings.dat / pixel.bmp / 0030-first-occurrence). Real signal: zero criticals, zero drawSurface-invalid, full opening sequence rendered through to the playable lab room.

## Caveats and tuning

- **Timing assumes `--fast`** (DOSBox-X `cycles=max`). Under `--parity` (`cycles=fixed 40000`, real-HW-approximate), the engine init takes longer and the milestones shift; the script has a `--parity` flag but the screenshot timings inside the script are tuned for `--fast`. If you want a parity-config smoke, expect to re-tune the `sleep` values.
- **Single-instance lock.** `tools/dosbox-launch.sh` refuses a second DOSBox-X. The smoke script will refuse to run if `dosbox-x` is already up — kill it first or use `--keep-running` on the prior run.
- **Display target.** The script forces `DISPLAY=:0` for `scrot` and `xdotool` (Snow / Basilisk parity convention). Override via `DOSBOX_DISPLAY=...` if the user's visible X session is elsewhere.
- **Window focus race.** `xdotool search --name DOSBox windowactivate --sync` is retried up to 5 seconds. If the X session has multiple DOSBox windows or the WM is slow to focus, the keystrokes can land on the wrong window — the smoke output's `screenshot` byte-sizes will look stuck at the title for milestones 04+.

## Future automation

What this script doesn't do that a future iteration could:

- **Baseline image diff.** Save `tests/fixtures/gameplay-smoke-baseline/` and compare new runs via `compare -metric AE` or perceptual hash. Need to factor out the DOSBox-X menu bar + window chrome (region-clip) since they vary by WM.
- **Audio capture.** Pipe DOSBox-X audio output to a WAV via PulseAudio loopback or DOSBox-X's `mixer set ... record`, then check audio energy in the file. Detects "engine plays no music" regression.
- **Save/load round-trip.** Send F1 → save → quit-to-title → "Load game" → confirm position. Adds a few minutes to runtime.
- **Multiple runs per build.** Detect flake by running 3x; if results differ, flag.
- **Headless variant.** A `tests/run-gameplay-smoke-headless.sh` that uses Xvfb so the test bot doesn't need a visible X session — would let the smoke run in CI.
