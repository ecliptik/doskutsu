# DOSKUTSU Configuration

DOSKUTSU reads its options from DOS environment variables at startup. Set them with `SET` - either in `AUTOEXEC.BAT`, or at the DOS prompt before running `DOSKUTSU.EXE`:

```
SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya
DOSKUTSU
```

A few rules apply to all of them:

- **On/off flags use a strict `=1` match.** `=0`, an empty value, or leaving the variable unset all mean "off". (A few variables carry a value instead of a flag - `PERF_MODE` takes a level, `AUDIO_DEVICE_SAMPLE_FRAMES` takes a frame count, `AUDIO_WB_VOICE_RESET` takes a reset type.)
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
| `SDL_HINT_DOSKUTSU_AUDIO_BACKEND` | `auto`, `opl3`, `organya`, `wb` | `auto` | Music synthesizer. `auto` (default): probe for a WaveBlaster daughterboard first, fall back to OPL3 FM if none is found. `opl3`: force the Sound Blaster OPL3 FM chip. `organya`: Cave Story's original software synth. `wb`: force a WaveBlaster daughterboard. | `organya` is ~9 fps slower than MIDI; `auto` / `opl3` / `wb` run music off the CPU |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE` | `wiimidi`, `orgmid`, `<dir>` | `wiimidi` | Which MIDI set the `opl3` / `wb` backends play. `wiimidi`: the WiiWare arrangement (`data/midi/`). `orgmid`: the Hart legacy `.mid` set (`data/orgmid/`). Any other value is treated as a custom drop-in directory name: if `data/<dir>/` holds your own `.mid` tracks, the engine plays from there (see "Bring your own MIDI set" in `docs/ASSETS.md`). SETUP exposes this as the **MIDI music set** row (writing the `MIDI_SET` DOSKUTSU.CFG key) -- known sets show as WiiWare / OrgMIDI, drop-ins as `Custom (<dir>)`. | None |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_GM_VARIANT` | `v1`, `v2` | unset | Picks an `org2mid`-converted General MIDI variant. | None |
| `SDL_HINT_DOSKUTSU_AUDIO_MIDI_CUSTOM_DIRS` | `0` (to disable) | on | Whether `MIDI_SET` / `..._MIDI_SOURCE` may name a custom `data/<dir>/` drop-in set (see the row above). On (default): an unrecognized value naming a real `data/<dir>/` with at least one `.mid` plays from that dir. `=0`: only the built-in `wiimidi` / `orgmid` sets are accepted; any other value falls back to `wiimidi`. The dir name must be a single safe path segment (letters/digits/`_`/`-`/`.`, no `..`, no path separators); on real DOS hardware that means 8 characters or fewer. | None |
| `SDL_HINT_DOSKUTSU_AUDIO_TIER2` | `0` (to disable) | on | On (default): 11025 Hz mono audio. `=0`: 22050 Hz stereo, the original 2004 audio quality. | `=0` costs ~11 fps in music-heavy scenes |
| `SDL_HINT_DOSKUTSU_AUDIO_WB_COLD_INIT` | `0` (to disable) | on | WaveBlaster (`wb` backend) only. On (default): brings the MPU-401 up (reset + UART entry + ACK drain) BEFORE the Sound Blaster opens, on a quiet bus -- this is what lets WaveBlaster music start cleanly on 486-class boards that otherwise stall at load. `=0` restores the old late init (only if the legacy behavior is needed). | None |
| `SDL_HINT_DOSKUTSU_AUDIO_WB_VOICE_RESET` | `gs`, `xg`, `gm`, `none` | `none` | WaveBlaster (`wb` backend) only. Sends a synthesizer reset at the start of each song so the daughterboard uses the right instrument map. Try `gs` first if WaveBlaster music plays the correct tune but with wrong instruments (e.g. a cowbell where a piano belongs). `none` (default) sends nothing. | None |
| `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` | frame count | `1024` | Audio chunk size. Larger values cost less CPU but add SFX latency; smaller values do the reverse. | Minor - larger values cost slightly less CPU |
| `SDL_HINT_DOSKUTSU_AUDIO_SB_FORCE_8BIT` | `1` (to enable) | off | Forces the pre-SB16 8-bit / low-DMA (`D`-channel) playback path regardless of the card's reported DSP version. This is for an 8-bit Sound Blaster-class card - notably a **PicoGUS in SB mode** - that reports DSP 4.x but has no real 16-bit (high) DMA. If such a card's `BLASTER` variable simply has no `H` token, the engine now detects that and falls back to the 8-bit path automatically (no flag needed); set this flag for the case where an `H` IS present in `BLASTER` but the card still cannot do 16-bit DMA, or any time you want to pin the 8-bit path. Without the correct path such a card commits to a 16-bit transfer it cannot perform and all audio (SFX plus every music backend) goes silent together. A real SB16 (e.g. Vibra16) does not need it and is byte-identical with the default off. | None |

```
SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya   REM the original software synth (slower)
SET SDL_HINT_DOSKUTSU_AUDIO_TIER2=0           REM full 22050 Hz stereo audio
SET SDL_AUDIO_DEVICE_SAMPLE_FRAMES=2048       REM larger ring; saves CPU on slow hardware
SET SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE=mymidi   REM play your own data/mymidi/*.mid (see docs/ASSETS.md)
```

The `opl3` and `wb` backends play music on dedicated sound hardware, off the CPU. `organya` mixes every audio tick on the CPU - it is the exact 2004 sound, at a cost. `AUDIO_TIER2=0` restores full-quality audio but costs frame rate in music-heavy scenes. Raising `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` (try `2048`) helps with audio stutter on slow hardware.

---

## Input

| Variable | Values | Default | Effect | FPS impact |
|---|---|---|---|---|
| `DOSKUTSU_USE_JOYSTICK` | `0`, `1` | `0` | `1` opens the joystick / gamepad subsystem. | small (bounded direct-port read; see `JOY_DIRECTREAD`) |
| `DOSKUTSU_BIND_<ACTION>` | `k:<keycode>[,b:<button>]` | unset | Rebinds one player action to an SDL keycode and optional gameport button. `<ACTION>` is one of `LEFT`, `RIGHT`, `UP`, `DOWN`, `JUMP`, `FIRE`, `STRAFE`, `PREVWPN`, `NEXTWPN`, `INVENTORY`, `MAP`. Unset = the action keeps its saved (settings.dat) or built-in binding. | None |
| `SDL_HINT_DOSKUTSU_JOY_CAL` | `xmin,xcenter,xmax,ymin,ycenter,ymax` | unset | Stored gameport calibration (per-axis minimum, resting centre, maximum), so the stick need not be re-centred each run. Unset = the SDL backend auto-calibrates. Values are in the direct-port read's units (see `JOY_DIRECTREAD`); written by SETUP and not meant for hand-editing. | None |
| `SDL_HINT_DOSKUTSU_JOY_DIRECTREAD` | `0`, `1` | `1` | Killswitch for the bounded direct gameport (port 0x201) axis read. `1` (default) reads only the two connected joystick-1 axes with a hard iteration cap. `0` reverts to the legacy BIOS INT 15h read (which also waits on the open joystick-2 axes -- the historical ~80 ms/frame cost) AND ignores the stored `JOY_CAL` (its units differ from the BIOS read), auto-calibrating instead. | `=0` reintroduces the ~80 ms/frame BIOS cost - severe |
| `DOSKUTSU_JOY_INVERT_Y` | `0`, `1` | `0` | `1` inverts the gameport stick's vertical (Y) axis, swapping up and down. Useful for a flightstick whose pitch axis reads opposite the platformer convention (push-forward = climb vs dive). Affects looking / aiming up vs down and the up-to-enter-doors interaction; the horizontal (X) axis is unaffected. Toggle it in `SETUP.EXE` -> Input -> Configure joystick. | None |
| `SDL_HINT_DOSKUTSU_JOY_CAP` | iteration count | `3000` | Direct-port (`JOY_DIRECTREAD=1`) axis discharge-count cap. Raise it only if a high-resistance stick's full deflection reads as "not connected" and that direction stops responding near the extreme; most sticks never need it. | Minor - a larger cap slightly lengthens the worst-case axis read |

```
SET DOSKUTSU_USE_JOYSTICK=1   REM only when a real joystick is on the gameport
```

Leave `DOSKUTSU_USE_JOYSTICK` off unless a physical joystick is connected. On Sound Blaster cards the gameport is detected even with nothing plugged in. The default read path (`SDL_HINT_DOSKUTSU_JOY_DIRECTREAD=1`) reads only the two connected axes directly off port 0x201 with a bounded timing loop, so the per-frame cost is small; the legacy BIOS read (`=0`) also waits on the open joystick-2 axes and costs about 80 ms per frame on the reference PC - a severe frame-rate hit. Keyboard-only play is fully supported and is the default.

The `DOSKUTSU_BIND_*` and `SDL_HINT_DOSKUTSU_JOY_CAL` variables are normally written for you by `SETUP.EXE`'s Input screens (Configure keyboard / Configure joystick / Calibrate joystick), which store them in `DOSKUTSU.CFG`. You can also set them by hand. A binding value is `k:<keycode>` for keyboard, optionally `,b:<button>` to also map a gameport button (0-3); for example `SET DOSKUTSU_BIND_JUMP=k:122,b:0` binds Jump to the `Z` key and gameport button 0. A binding written here wins over the saved `settings.dat` mapping for that action; all other actions are left untouched. With no `DOSKUTSU_BIND_*` set, controls are exactly the default Cave Story layout.

The `k:` value is a numeric SDL keycode. For a printable key it is simply the ASCII code of the lowercase character (letters `a`-`z` = 97-122, digits `0`-`9` = 48-57). Special keys use `1073741824 + scancode`. Common remappable keys:

| Key | `k:` value | | Key | `k:` value |
|---|---|---|---|---|
| Left arrow | `1073741904` | | Space | `32` |
| Right arrow | `1073741903` | | Enter / Return | `13` |
| Up arrow | `1073741906` | | Tab | `9` |
| Down arrow | `1073741905` | | Backspace | `8` |
| `A` (97) `B` (98) `C` (99) | `97`-`99` | | Left Shift | `1073742049` |
| `Q` (113) `S` (115) | `113` / `115` | | Left Ctrl | `1073742048` |
| `W` (119) `X` (120) `Z` (122) | `119` / `120` / `122` | | Left Alt | `1073742050` |

The Cave Story defaults are Left/Right/Up/Down = the arrow keys, Jump = `Z` (122), Fire = `X` (120), Strafe = `C` (99), Prev/Next Weapon = `A`/`S` (97/115), Inventory = `Q` (113), Map = `W` (119). Any other letter is its ASCII code (e.g. `D` = 100, `F` = 102).

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

DOSKUTSU has many more environment variables for instrumentation, profiling, TAS record/replay, and per-wave diagnostics. They are for contributors working on the port, not for playing the game, and are not covered here. (Their full reference lives in the maintainers' internal notes, which are not part of the public repository.)
