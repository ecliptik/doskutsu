# DOSKUTSU Configuration

DOSKUTSU reads its options from DOS environment variables at startup. Set them with `SET` - either in `AUTOEXEC.BAT`, or at the DOS prompt before running `DOSKUTSU.EXE`:

```
SET SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=1
DOSKUTSU
```

A few rules apply to all of them:

- **On/off flags use a strict `=1` match.** `=0`, an empty value, or leaving the variable unset all mean "off". (Two variables carry a value instead of a flag - `PERF_MODE` takes a level, `AUDIO_DEVICE_SAMPLE_FRAMES` takes a frame count.)
- **Every option is read once at startup** and cached, so toggling one has no per-frame cost.
- **The default DOS environment block is small** (256 bytes). If `SET` reports `Out of environment space`, enlarge it in `CONFIG.SYS` and reboot once:
  ```
  SHELL=C:\DOS\COMMAND.COM /E:1024 /P
  ```

The defaults are tuned for the reference PC. You only need this file if you want to change the music, fix the game speed, or work around a hardware quirk.

---

## Game speed and fidelity

| Variable | Values | Default | Effect |
|---|---|---|---|
| `SDL_HINT_DOSKUTSU_FIXED_TIMESTEP` | `0`, `1` | `0` | `1` runs game logic at a fixed 50 Hz, decoupled from the frame rate, so the game plays at its authored speed instead of slowing down with the frame rate. See [50 Hz without 50 fps](../README.md#50-hz-without-50-fps). |
| `SDL_HINT_DOSKUTSU_PERF_MODE` | `0`, `1`, `2` | `0` | Performance Mode - trades render detail for frame rate. `1` drops decorative foreground detail (collision and slope tiles are always kept); `2` is currently the same as `1`. |

```
SET SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=1   REM play at the correct 50 Hz game speed
SET SDL_HINT_DOSKUTSU_PERF_MODE=1        REM drop decorative foreground detail
```

**Performance:** `FIXED_TIMESTEP` does not change the frame rate - it changes game *speed*, so movement and timers run correctly at ~30 fps instead of in slow motion. `PERF_MODE=1`'s cuts measured flat on the reference PC; the visible change is a flatter depth look, with little measurable fps gain there. Both default-OFF; both leave the faithful render untouched when off.

---

## Audio

| Variable | Values | Default | Effect |
|---|---|---|---|
| `SDL_HINT_DOSKUTSU_AUDIO_BACKEND` | `opl3`, `organya`, `wb` | `opl3` | Music synthesizer. `opl3`: the Sound Blaster OPL3 FM chip. `organya`: Cave Story's original software synth. `wb`: a WaveBlaster daughterboard. |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` | `wiimidi`, `orgmid` | `wiimidi` | Which MIDI set the `opl3` / `wb` backends play. `wiimidi`: the WiiWare arrangement. `orgmid`: the Hart legacy `.mid` set. |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_GM_VARIANT` | `v1`, `v2` | unset | Picks an `org2mid`-converted General MIDI variant. |
| `SDL_HINT_DOSKUTSU_AUDIO_TIER2` | `0` (to disable) | on | On (default): 11025 Hz mono audio. `=0`: 22050 Hz stereo, the original 2004 audio quality. |
| `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` | frame count | `1024` | Audio chunk size. Larger values cost less CPU but add SFX latency; smaller values do the reverse. |

```
SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya   REM the original software synth (slower)
SET SDL_HINT_DOSKUTSU_AUDIO_TIER2=0           REM full 22050 Hz stereo audio
SET SDL_AUDIO_DEVICE_SAMPLE_FRAMES=2048       REM larger ring; saves CPU on slow hardware
```

**Performance:** the `opl3` and `wb` backends play music on dedicated sound hardware, off the CPU. `organya` mixes every audio tick on the CPU and runs roughly 9 fps slower on a Pentium - it is the exact 2004 sound, at a cost. `AUDIO_TIER2=0` restores full-quality audio but costs about 11 fps in music-heavy scenes. Raising `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` (try `2048`) helps if you hear audio stutter on slow hardware.

---

## Input

| Variable | Values | Default | Effect |
|---|---|---|---|
| `DOSKUTSU_USE_JOYSTICK` | `0`, `1` | `0` | `1` opens the joystick / gamepad subsystem. |

```
SET DOSKUTSU_USE_JOYSTICK=1   REM only if a real joystick is plugged into the gameport
```

**Performance:** leave this off unless you have a physical joystick. On Sound Blaster cards the gameport is detected even with nothing plugged in, and polling it through the BIOS costs about 80 ms per frame on the reference PC - a severe frame-rate hit. Keyboard-only play is fully supported and is the default.

---

## Compatibility fallbacks

These restore older code paths. The default behavior is correct on the tested hardware; set one of these to `0` only if you see a visual artifact or hear audio trouble on your own hardware.

| Variable | Set to | Effect when set |
|---|---|---|
| `SDL_HINT_DOSKUTSU_DIRTY_RECTS` | `0` | Disables dirty-rectangle rendering. |
| `SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8` | `0` | Disables indexed (8bpp) rendering; falls back to RGB565. |
| `SDL_HINT_DOSKUTSU_DIRECT_VESA` | `0` | Disables the direct-to-VRAM present path. |
| `SDL_HINT_DOSKUTSU_FORCE_PUMP_YIELD` | `1` | Restores the original per-pump thread yield; try this if you hear audio stutter. |

```
SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=0   REM only if colors look wrong on your card
```

**Performance:** each of these turns *off* an optimization that is on by default, so setting one costs frame rate. They exist for hardware that the optimization does not suit, not for tuning - on the reference PC the defaults are the fast path.

---

## Developer and diagnostic flags

DOSKUTSU has many more environment variables for instrumentation, profiling, TAS record/replay, and per-wave diagnostics. They are for contributors working on the port, not for playing the game, and are not covered here. Contributors: see `docs/internal/BOOT.md` for the full set.
