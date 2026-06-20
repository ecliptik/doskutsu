#!/usr/bin/env bash
#
# tests/run-input-coexist-smoke.sh -- keyboard + gameport-axis coexistence smoke
# (#40 Phase 3 / DOSBox-prep-batch Item 3). DOSBox-X only; runs the SHIPPING
# binary (build/stage/DOSKUTSU.EXE) -- NO engine rebuild.
#
# WHAT THIS VALIDATES (only what DOSBox-X can reliably show):
#   1. [input-bind] applied=N invert_y=M dos_axes=1 -- the coexistence-wiring
#      banner emits every boot (input_apply_cfg_bindings). dos_axes=1 means the
#      DOS axis->action maps were re-asserted after settings_load (patch 0230).
#   2. DOSKUTSU_USE_JOYSTICK=1 is HONORED -- the default "Joystick: skipped"
#      line is ABSENT, and the engine boots + runs cleanly with the joystick
#      subsystem enabled (reaches the configured auto-exit tick; no hang/crash;
#      the ~82 ms BIOS-poll stall the env var guards against does not occur).
#   3. KEYBOARD path is live WITH the joystick enabled -- when keys are injected
#      (visible-X step), the SDL-side kbd_irq counter goes 0 -> >0. This is the
#      DOSBox-witnessable half of the 0230 coexistence: keyboard input is
#      delivered while USE_JOYSTICK=1. (Auto-skipped when no X display.)
#
# EMPIRICAL FINDING (the research output -- recorded + asserted):
#   The SDL3-DOS joystick driver enumerates ZERO gameport devices under
#   DOSBox-X: no "Opened Joystick 0" / "Number of Axes" lines, and the in-game
#   [eng-axis] axis-poll diag NEVER emits (SDL_GetNumJoystickAxes(NULL) == 0).
#   So the gameport direct-port 0x201 axis read (SDL 0102-0105) has nothing to
#   read in DOSBox. AXIS-driven movement and the TRUE CONCURRENT key+axis
#   coexistence are hardware-I/O paths and are g2k-ONLY (per [[dosbox_not_proxy]]).
#   This smoke does NOT fake axis input. See the g2k manual cell:
#   tests/INPUT-COEXIST-G2K.md.
#
# Usage:
#   tests/run-input-coexist-smoke.sh                 # auto: visible on :0 if reachable, else headless
#   tests/run-input-coexist-smoke.sh --headless      # force headless (skip the keyboard-inject witness)
#   tests/run-input-coexist-smoke.sh --display :0    # pick the X display for the visible step
#   tests/run-input-coexist-smoke.sh --exit-tick 250 # auto-exit tick (default 250)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF="$REPO_ROOT/tools/dosbox-x-fast.conf"
STAGE="$REPO_ROOT/build/stage"
EXE="$STAGE/DOSKUTSU.EXE"
CWSDPMI_DIR="$REPO_ROOT/vendor/cwsdpmi"
WORK="/tmp/input-coexist-smoke"
EXIT_TICK=250
FORCE_HEADLESS=0
XDISPLAY="${DOSBOX_DISPLAY:-:0}"

while (($#)); do
  case "$1" in
    --headless)   FORCE_HEADLESS=1 ;;
    --display)    shift; XDISPLAY="$1" ;;
    --exit-tick)  shift; EXIT_TICK="$1" ;;
    --work)       shift; WORK="$1" ;;
    -h|--help)    sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //'; exit 0 ;;
    *)            echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

fail() { echo "[input-coexist] FAIL: $*" >&2; exit 1; }
log()  { echo "[input-coexist] $*"; }

[ -f "$EXE" ] || fail "$EXE not found -- run 'make stage' first (do NOT rebuild the shipping binary for this test)"
[ -f "$CONF" ] || fail "$CONF not found"
[ -f "$CWSDPMI_DIR/cwsdpmi.exe" ] || fail "CWSDPMI missing at $CWSDPMI_DIR -- run ./scripts/fetch-vendor-binaries.sh"
command -v dosbox-x >/dev/null || fail "dosbox-x not on PATH"
if pgrep -x dosbox-x >/dev/null; then fail "dosbox-x already running -- kill it first (no concurrent instances)"; fi

# --- decide visible vs headless --------------------------------------------
VISIBLE=0
if [ "$FORCE_HEADLESS" = "0" ] && DISPLAY="$XDISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
  command -v xdotool >/dev/null && command -v scrot >/dev/null && VISIBLE=1
fi
if [ "$VISIBLE" = "1" ]; then
  log "mode: VISIBLE (display $XDISPLAY) -- includes the keyboard-inject witness"
else
  log "mode: HEADLESS (SDL_VIDEODRIVER=dummy) -- keyboard-inject witness SKIPPED (no usable X)"
fi

# --- stage a minimal WORK dir (no CACHE: OPL3 backend, no organya pre-render) -
rm -rf "$WORK"; mkdir -p "$WORK/LOGS"
cp "$EXE" "$WORK/DOSKUTSU.EXE"
[ -f "$STAGE/DOSKUTSU.CFG" ] && cp "$STAGE/DOSKUTSU.CFG" "$WORK/"
ln -s "$STAGE/data" "$WORK/data"
[ -d "$STAGE/DOSKUTSU" ] && ln -s "$STAGE/DOSKUTSU" "$WORK/DOSKUTSU" || mkdir -p "$WORK/DOSKUTSU"
LOG="$WORK/LOGS/COEX.LOG"

# COEX.BAT: enable joystick + axis diag, OPL3 (skip organya cold-render),
# headless-safe auto-exit. COMMAND /E:2048 self-relaunch so the SETs cannot
# overflow the default ~256 B DOS environment (the qhb5g lesson).
{
  printf '@echo off\r\n'
  printf 'if "%%1"=="GO" goto run\r\n'
  printf 'COMMAND /E:2048 /C %%0 GO\r\n'
  printf 'goto end\r\n'
  printf ':run\r\n'
  printf 'SET DOSKUTSU_USE_JOYSTICK=1\r\n'
  printf 'SET SDL_HINT_DOSKUTSU_JOY_DIAG=1\r\n'
  printf 'SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=opl3\r\n'
  printf 'SET DOSKUTSU_TAS_AUTO_EXIT_TICK=%s\r\n' "$EXIT_TICK"
  printf 'SET DOSKUTSU_LOG_TAG=COEX\r\n'
  printf 'DOSKUTSU.EXE\r\n'
  printf ':end\r\n'
} > "$WORK/COEX.BAT"

dbx_args=(-conf "$CONF" -nopromptfolder
  -c "MOUNT C $WORK" -c "MOUNT D $CWSDPMI_DIR"
  -c 'SET PATH=Z:\;C:\;D:\'
  -c 'SET BLASTER=A220 I5 D1 H5 T6'
  -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1'
  -c 'SET SDL_INVALID_PARAM_CHECKS=0'
  -c 'SET DOSKUTSU_LOG_VERBOSE=1'
  -c "C:" -c "COEX.BAT")

cleanup() { pkill -x dosbox-x 2>/dev/null || true; }
trap cleanup EXIT

KBD_BEFORE=0
KBD_AFTER=0

if [ "$VISIBLE" = "1" ]; then
  DISPLAY="$XDISPLAY" dosbox-x "${dbx_args[@]}" >"$WORK/dosbox.out" 2>&1 &
  DBX=$!
  log "waiting for title (the [input-bind] banner)..."
  for i in $(seq 1 45); do grep -qa '\[input-bind\]' "$LOG" 2>/dev/null && break; kill -0 "$DBX" 2>/dev/null || break; sleep 1; done
  sleep 8   # settle at title/intro
  KBD_BEFORE=$(grep -ao 'kbd_irq=[0-9]*' "$LOG" 2>/dev/null | sort -t= -k2 -n | tail -1 | cut -d= -f2); KBD_BEFORE=${KBD_BEFORE:-0}
  WIN=$(DISPLAY="$XDISPLAY" xdotool search --name -- "DOSBox-X" 2>/dev/null | tail -1 || true)
  if [ -n "${WIN:-}" ]; then
    DISPLAY="$XDISPLAY" xdotool windowactivate --sync "$WIN" 2>/dev/null || true
    log "injecting menu keys (Down/Up/Z/Left/Right) with USE_JOYSTICK=1..."
    for k in Down Up Down z z Left Right Left Right Up; do
      DISPLAY="$XDISPLAY" xdotool key --window "$WIN" --delay 120 "$k" 2>/dev/null || true
    done
    sleep 4
    DISPLAY="$XDISPLAY" scrot -u "$WORK/coexist-title.png" 2>/dev/null || true
  else
    log "WARN: could not find the DOSBox-X window -- keyboard-inject witness incomplete"
  fi
  KBD_AFTER=$(grep -ao 'kbd_irq=[0-9]*' "$LOG" 2>/dev/null | sort -t= -k2 -n | tail -1 | cut -d= -f2); KBD_AFTER=${KBD_AFTER:-0}
  # let it auto-exit (or kill on timeout)
  for i in $(seq 1 60); do grep -qa 'tas: auto-exit at tick' "$LOG" 2>/dev/null && break; kill -0 "$DBX" 2>/dev/null || break; sleep 1; done
  pkill -x dosbox-x 2>/dev/null || true
else
  SDL_VIDEODRIVER=dummy dosbox-x "${dbx_args[@]}" >"$WORK/dosbox.out" 2>&1 &
  DBX=$!
  log "headless run; waiting for clean auto-exit at tick $EXIT_TICK..."
  for i in $(seq 1 180); do grep -qa 'tas: auto-exit at tick' "$LOG" 2>/dev/null && break; kill -0 "$DBX" 2>/dev/null || break; sleep 1; done
  pkill -x dosbox-x 2>/dev/null || true
fi
sleep 1

[ -f "$LOG" ] || fail "no engine log at $LOG -- the binary never booted (see $WORK/dosbox.out)"

# ============================ ASSERTIONS ====================================
PASS=1
check() { if eval "$2"; then echo "  [OK]  $1"; else echo "  [X]   $1"; PASS=0; fi; }

echo "[input-coexist] --- assertions ---"

# 1. coexistence-wiring banner
BIND_LINE=$(grep -a '\[input-bind\] applied=' "$LOG" | head -1 || true)
check "coexistence-wiring banner emits ([input-bind] applied=N invert_y=M dos_axes=1)" \
      "grep -qa '\[input-bind\] applied=[0-9]* invert_y=[0-9]* dos_axes=1' '$LOG'"
[ -n "$BIND_LINE" ] && echo "        -> $BIND_LINE"

# 2. USE_JOYSTICK=1 honored (the default skip line must be ABSENT)
check "DOSKUTSU_USE_JOYSTICK=1 honored (no 'Joystick: skipped' line)" \
      "! grep -qa 'Joystick: skipped' '$LOG'"

# 3. clean run with the joystick subsystem enabled (reached auto-exit; no hang/crash)
check "engine boots + runs cleanly with joystick enabled (auto-exit at tick $EXIT_TICK)" \
      "grep -qa 'tas: auto-exit at tick' '$LOG'"
check "no [critical] engine errors" \
      "! grep -qa '\[critical\]' '$LOG'"

# EMPIRICAL: DOSBox-X enumerates zero gameport devices -> axis path g2k-only
ENGAXIS=$(grep -ac '\[eng-axis\]' "$LOG" || true)
JOYOPEN=$(grep -ac 'Opened Joystick\|Number of Axes' "$LOG" || true)
echo "[input-coexist] --- empirical (gameport in DOSBox-X) ---"
echo "        joystick-device-open log lines : $JOYOPEN  (expect 0 in DOSBox-X)"
echo "        [eng-axis] axis-poll diag lines: $ENGAXIS  (expect 0 in DOSBox-X)"
if [ "$ENGAXIS" -eq 0 ] && [ "$JOYOPEN" -eq 0 ]; then
  echo "        => CONFIRMED: DOSBox-X presents no gameport device; axis-driven"
  echo "           movement + concurrent key+axis are g2k-ONLY (see tests/INPUT-COEXIST-G2K.md)."
else
  echo "        => NOTE: DOSBox-X enumerated a gameport this run ($JOYOPEN open / $ENGAXIS poll);"
  echo "           inspect getaxis values in $LOG -- they are NOT a g2k proxy ([[dosbox_not_proxy]])."
fi

# 4. keyboard-live witness (visible only)
if [ "$VISIBLE" = "1" ]; then
  echo "[input-coexist] --- keyboard-with-joystick witness ---"
  echo "        kbd_irq before inject = $KBD_BEFORE, after = $KBD_AFTER"
  check "keyboard path live with USE_JOYSTICK=1 (kbd_irq rose after key injection)" \
        "[ '$KBD_AFTER' -gt '$KBD_BEFORE' ]"
else
  echo "[input-coexist] keyboard-with-joystick witness SKIPPED (headless) -- covered by the g2k manual cell."
fi

echo
if [ "$PASS" = "1" ]; then
  log "PASS -- coexistence wiring + USE_JOYSTICK honored + clean run verified in DOSBox-X."
  log "       axis movement + concurrent key+axis are g2k-only (tests/INPUT-COEXIST-G2K.md)."
  exit 0
else
  fail "one or more assertions failed (see above + $LOG)"
fi
