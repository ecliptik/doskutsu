# DOSKUTSU Configuration

DOSKUTSU reads its options from DOS environment variables at startup. Set them with `SET` - either in `AUTOEXEC.BAT`, or at the DOS prompt before running `DOSKUTSU.EXE`:

```
SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya
DOSKUTSU
```

A few rules apply to all of them:

- **On/off flags use a strict `=1` match.** `=0`, an empty value, or leaving the variable unset all mean "off". (Two variables carry a value instead of a flag - `PERF_MODE` takes a level, `AUDIO_DEVICE_SAMPLE_FRAMES` takes a frame count.)
- **Every option is read once at startup** and cached, so toggling one has no per-frame cost.
- **The default DOS environment block is small** (256 bytes). If `SET` reports `Out of environment space`, enlarge it in `CONFIG.SYS` and reboot once:
  ```
  SHELL=C:\DOS\COMMAND.COM /E:1024 /P
  ```

The defaults are tuned for the reference PC. This file matters only for changing the music, fixing the game speed, or working around a hardware quirk.

---

## Game speed and fidelity

| Variable | Values | Default | Effect | FPS impact |
|---|---|---|---|---|
| `SDL_HINT_DOSKUTSU_FIXED_TIMESTEP` | `0`, `1` | `1` | On by default: game logic runs at a fixed 50 Hz, decoupled from the frame rate, so the game plays at its authored speed. `0` reverts to the legacy 1:1 logic/render loop. See [Fixed-Timestep mode](../README.md#fixed-timestep-mode). | None - changes game speed, not frame rate |
| `SDL_HINT_DOSKUTSU_PERF_MODE` | `0`, `1`, `2` | `0` | Performance Mode - trades render detail for frame rate. `1` drops decorative foreground detail (collision and slope tiles are always kept); `2` is currently the same as `1`. | Minimal - the level-1 cuts measured flat on the reference PC |

```
SET SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=0   REM legacy frame-coupled loop (slow at ~30 fps)
SET SDL_HINT_DOSKUTSU_PERF_MODE=1        REM drop decorative foreground detail
```

`FIXED_TIMESTEP` does not change the frame rate - it changes game *speed*, so movement and timers run correctly at ~30 fps instead of in slow motion. It is on by default; `=0` restores the legacy frame-coupled loop. `PERF_MODE` is default-OFF; its level-1 cuts measured flat on the reference PC, the visible change being a flatter depth look.

---

## Audio

| Variable | Values | Default | Effect | FPS impact |
|---|---|---|---|---|
| `SDL_HINT_DOSKUTSU_AUDIO_BACKEND` | `opl3`, `organya`, `wb` | `opl3` | Music synthesizer. `opl3`: the Sound Blaster OPL3 FM chip. `organya`: Cave Story's original software synth. `wb`: a WaveBlaster daughterboard. | `organya` is ~9 fps slower than MIDI; `opl3` / `wb` run music off the CPU |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` | `wiimidi`, `orgmid` | `wiimidi` | Which MIDI set the `opl3` / `wb` backends play. `wiimidi`: the WiiWare arrangement. `orgmid`: the Hart legacy `.mid` set. | None |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_GM_VARIANT` | `v1`, `v2` | unset | Picks an `org2mid`-converted General MIDI variant. | None |
| `SDL_HINT_DOSKUTSU_AUDIO_TIER2` | `0` (to disable) | on | On (default): 11025 Hz mono audio. `=0`: 22050 Hz stereo, the original 2004 audio quality. | `=0` costs ~11 fps in music-heavy scenes |
| `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` | frame count | `1024` | Audio chunk size. Larger values cost less CPU but add SFX latency; smaller values do the reverse. | Minor - larger values cost slightly less CPU |

```
SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya   REM the original software synth (slower)
SET SDL_HINT_DOSKUTSU_AUDIO_TIER2=0           REM full 22050 Hz stereo audio
SET SDL_AUDIO_DEVICE_SAMPLE_FRAMES=2048       REM larger ring; saves CPU on slow hardware
```

The `opl3` and `wb` backends play music on dedicated sound hardware, off the CPU. `organya` mixes every audio tick on the CPU - it is the exact 2004 sound, at a cost. `AUDIO_TIER2=0` restores full-quality audio but costs frame rate in music-heavy scenes. Raising `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` (try `2048`) helps with audio stutter on slow hardware.

---

## Input

| Variable | Values | Default | Effect | FPS impact |
|---|---|---|---|---|
| `DOSKUTSU_USE_JOYSTICK` | `0`, `1` | `0` | `1` opens the joystick / gamepad subsystem. | `=1` costs ~80 ms per frame on Sound Blaster gameports - severe |

```
SET DOSKUTSU_USE_JOYSTICK=1   REM only when a real joystick is on the gameport
```

Leave this off unless a physical joystick is connected. On Sound Blaster cards the gameport is detected even with nothing plugged in, and polling it through the BIOS costs about 80 ms per frame on the reference PC - a severe frame-rate hit. Keyboard-only play is fully supported and is the default.

---

## Compatibility fallbacks

These restore older code paths. The default behavior is correct on the tested hardware; set one of these only to work around a visual artifact or audio trouble on a specific machine.

| Variable | Set to | Effect when set | FPS impact |
|---|---|---|---|
| `SDL_HINT_DOSKUTSU_DIRTY_RECTS` | `0` | Disables dirty-rectangle rendering. | Costs frame rate - disables a default optimization |
| `SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8` | `0` | Disables indexed (8bpp) rendering; falls back to RGB565. | Costs frame rate - disables a default optimization |
| `SDL_HINT_DOSKUTSU_DIRECT_VESA` | `0` | Disables the direct-to-VRAM present path. | About -0.27 fps |
| `SDL_HINT_DOSKUTSU_FORCE_PUMP_YIELD` | `1` | Restores the original per-pump thread yield; use it if audio stutters. | Costs frame rate - restores a per-frame thread yield |

```
SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=0   REM only if colors render wrong on a specific card
```

Each of these turns *off* an optimization that is on by default, so setting one costs frame rate. They exist for hardware that the optimization does not suit, not for tuning - on the reference PC the defaults are the fast path.

---

## Developer and diagnostic flags

DOSKUTSU has many more environment variables for instrumentation, profiling, TAS record/replay, and per-wave diagnostics. They are for contributors working on the port, not for playing the game, and are not covered here. Contributors: see `docs/internal/BOOT.md` for the full set.
