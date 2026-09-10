# DOSBox-X tooling mechanics

Covers `shared/tools/dosbox-launch.sh`, `dosbox-run.sh`,
`dosbox-teardown.sh`, and the three `.conf` variants. All three scripts
are already fully generic (usable as-is by any port) -- this is a guide
to using them well, not a spec of what they do (read the scripts' own
`--help`/header comments for the authoritative flag list, which can
change).

## Picking a mode

- **Manual/visual work** (play-testing, screenshots, driving via
  `xdotool`, watching a bug happen): `dosbox-launch.sh`. It mounts and
  stays running; you drive it or watch it. `--stage` mounts
  `build/stage/` (the real install layout: exe + `CWSDPMI.EXE` + `data/`
  co-located) rather than the repo root -- use `--stage` for anything
  where the engine resolves assets relative to its own base path, which
  is most gameplay-adjacent testing.
- **Scripted/CI-style smoke checks** (does it boot, does the expected
  banner line show up, does it exit 0): `dosbox-run.sh`. Headless, stages
  one exe (+ `--include` files), captures stdout to a file, exits.

## The stdout-only logging convention, and why it exists

`dosbox-run.sh`'s headless capture works by redirecting stdout to a file
in a generated `RUN.BAT` (`> STDOUT.TXT`). If a probe or test logs via
something that writes to **stderr** (e.g. SDL's own `SDL_Log()`), that
output is invisible to this capture path *and there is no fix available
inside DOSBox-X's own shell*: DOSBox-X's built-in command interpreter
does not support `2>&1` redirection the way a real DOS `COMMAND.COM`-plus-
DJGPP-runtime combination does when a DJGPP binary is invoked directly --
DOSBox-X parses `2>&1` as a redirect to a file literally named `&1`,
producing an empty capture, not a merged stream. (`dosbox-run.sh`'s
`--merge-stderr` flag works around this differently -- it only helps for
a program whose *own runtime* processes the `2>&1` in its invocation
line, which DJGPP's does; it does not make DOSBox-X's shell itself
understand the syntax.)

**Practical rule: write any new probe/smoke-test diagnostic output via
`printf`/stdout, not a stderr-writing logger**, if it needs to be
captured by a headless DOSBox-X run. This is why `shared/tests/sdl3-smoke/`'s
own probe reimplements upstream SDL's test coverage with `printf()`
instead of using `SDL_Log()` directly -- not a style preference, a
capture-mechanism requirement.

## Env-var hooks on `dosbox-launch.sh`

- **`DOSBOX_CONF=<path>`** -- override which `.conf` is used, resolved
  against the repo root if relative. Applied after `--fast`/parity flag
  parsing, so it wins over both.
- **`LAUNCH_LOG_VERBOSE_VAR=<NAME>`** -- forces `SET <NAME>=1` in the
  guest so a port's own verbose-logging env var is on for this launch,
  letting a smoke-gate banner-emit check witness INFO-level banners even
  when the port's untagged-run default is a quieter level.
- **`LAUNCH_EXTRA_SET="NAME=VALUE ..."`** -- injects arbitrary additional
  `SET` commands into the guest environment, applied after the launcher's
  own fixed `SET`s so a caller can deliberately override one of them.
  This is the same mechanism `docs/hardware-testing.md` and
  `dos-hardware-validation/references/runmanifest-log.md` reference for
  setting a port's environment-detection override.

## The three `.conf` variants

- **`dosbox-x.conf`** (parity) -- calibrated to approximate a specific
  real reference machine's CPU throughput (`cycles=fixed <N>`), for
  anything where real-HW-equivalent *timing* matters even locally (audio
  dropout investigation, anything timing-sensitive enough that
  `cycles=max` would hide or fabricate a problem). The shipped file is
  one port's own calibration, explicitly not a universal setting --
  re-benchmark and re-calibrate `cycles`/`oplmode`/`memsize`/video for
  your own port's reference machine rather than assuming the checked-in
  number transfers.
- **`dosbox-x-fast.conf`** -- identical except `cycles=max`, runs as fast
  as the host CPU allows (several times faster than a real reference
  machine). Use for fast iteration where wall-clock timing doesn't
  matter -- most compile/video/input/filesystem work.
- **`dosbox-x-oversized.conf`** -- a variant for a specific display/video
  scenario (see the conf's own header for current specifics, which can
  change as it's tuned).

## Emulator-only escape hatches baked into the shipped confs

The shipped `dosbox-x*.conf` and `dosbox-run.sh`'s generated `RUN.BAT`
set `SDL_DOS_AUDIO_SB_SKIP_DETECTION=1` unconditionally. This exists
because DOSBox-X's emulated SB16 returns a fixed failure value on the DSP
detection read regardless of timing tuning, so without this env var,
SDL3-DOS's real-hardware-validated SB16 detection logic never succeeds
under emulation at all -- audio simply doesn't init. **This is an
emulator-only escape hatch.** See
`references/emulator-vs-hardware-boundary.md` for the general rule this
is an instance of, and why setting it on real hardware would actively
mask a real regression rather than just being redundant.

## Driving and screenshotting DOSBox-X with `xdotool`/`scrot`

Sourced from doskutsu's own hardened harnesses (`tests/run-gameplay-smoke.sh`,
`tests/setup-review-walk.sh`) and confirmed the hard way a second time by
dossage hitting the same trap independently -- worth having here instead of
re-derived per port.

- **Focus once, then send keys with no `--window`.** `xdotool key --window
  <id>` / `type --window <id>` uses XSendEvent, which SDL2 (and therefore
  DOSBox-X) ignores -- it can silently no-op even against a valid, current
  window ID. This is not a staleness problem, it's the wrong event type for
  an SDL2 app. Use plain `xdotool key`/`type` (no `--window`) so events go
  via XTEST to whatever holds real keyboard focus, and focus the window
  once up front rather than per keystroke -- re-focusing before every key
  introduces a window-manager round-trip that desynchronizes keystroke
  timing (`run-gameplay-smoke.sh`'s `key()` does exactly this: focus once,
  then bare `xdotool key --delay 60 "$name"` for every subsequent send).
- **`xdotool search --name DOSBox windowactivate --sync` can race** if other
  clients also have "DOSBox" somewhere in their title -- loop/retry it
  rather than treating a single failure as fatal (`run-gameplay-smoke.sh`
  retries up to 10 times with a short sleep).
- **Cache the window ID once you have it; don't re-search by name after a
  video mode change.** The underlying X window survives a DOS text-mode ->
  VESA graphics-mode switch (it isn't destroyed/recreated), but its
  `WM_NAME` title goes blank on the switch, so a later `--name DOSBox`
  re-search stops matching anything. `setup-review-walk.sh`'s
  `focus_dosbox()` caches the id in `DBX_WIN` on first success and reuses
  it for every later `windowactivate`/`windowfocus` call. On a real
  (non-headless) window manager, `windowactivate` alone raises the window
  *without* granting it keyboard focus -- call `windowfocus` too, and
  re-assert both before each input batch since focus can drift on a live
  desktop with other windows.
- **`scrot -u` (capture the focused window) needs a window that's actually
  focused** -- if focus silently failed, this degrades straight into "failed
  to grab image" rather than a clear error; check the exit code rather than
  assuming a screenshot attempt succeeded. `scrot` also **appends
  `_000`/`_001` instead of overwriting** an existing target path -- `rm -f`
  the target before every shot, or a later diff/comparison silently runs
  against a stale frame from a prior run (this shipped a false "the patch
  didn't land" finding in doskutsu at least once). Fallback when `scrot`
  fails: `import -window "$(xdotool getactivewindow)" out.png`.
- **Prefer a private Xvfb for automated/headless gates over the operator's
  real `:0`.** Two independent reasons: an Xvfb doesn't touch whatever the
  operator is actually looking at on their real desktop, and a shared `:0`
  can end up in a state where `xdotool`/`xdpyinfo` block indefinitely --
  which reads exactly like a boot wedge in the port under test, not a
  display problem, and costs real debugging time until you think to check
  the display itself. doskutsu's automated gates default to
  `DOSBOX_DISPLAY=:88` with `--own-xvfb`; reserve the real `:0` for
  genuinely manual/interactive sessions where a human needs to see the
  window.

## Audio pre-buffering under `cycles=max` can make a working runtime change look unresponsive

Under `dosbox-x-fast.conf`, CPU emulation runs far faster than real time,
but the emulated DAC still drains its buffer at a fixed real-time rate --
so SDL's audio callback can pre-buffer far more audio than real-time
playback has actually consumed yet. Concretely: the callback can fire
many times immediately after stream-open, filling a large buffer, then go
quiet for tens of real seconds even though a keypress changed some
audio-affecting variable (e.g. a loudness/volume flag) moments later in
wall-clock time -- because everything already sitting in the buffer was
committed before the variable changed, and nothing is requesting more
audio yet to pick up the new value.

**Consequence:** testing "did a runtime audio-parameter change take
effect" by watching callback behavior under `cycles=max` can show a long,
misleading lag -- or apparent total non-response -- that has nothing to do
with the port's own logic being wrong. Either test audio-reactivity under
the parity config (`dosbox-x.conf`, not `-fast`), or budget a real
wall-clock wait proportional to however much audio is likely pre-buffered,
not to game-logic/CPU time, before concluding a change had no effect.

## The global-`pkill` hazard

`dosbox-teardown.sh` exists because a global `pkill -x dosbox-x` kills
*every* DOSBox-X process on the machine, not just the one a script
launched -- in a session with concurrent workstreams (a human reviewing a
build in one window, an automated smoke gate running in another), that
global kill has aborted a live, unrelated review more than once. Source
`dosbox-teardown.sh` and call `dbx_kill_conf "$CONF"` (conf-scoped,
comm-filtered so it can never self-kill the calling shell) instead of
reaching for `pkill` directly in any new script.
