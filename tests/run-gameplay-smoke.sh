#!/usr/bin/env bash
#
# tests/run-gameplay-smoke.sh — visible DOSBox-X gameplay smoke test
#
# Drives DOSKUTSU.EXE through a scripted sequence of keystrokes via xdotool,
# capturing screenshots at named milestones. Designed for repeatable smoke
# verification without a human at the keyboard. Distinct from the headless
# library smokes (run-smoke.sh, run-sdl3-smoke.sh, etc.) — those exercise
# isolated SDL/SDL_mixer/SDL_image; this one exercises the full game binary
# end-to-end through the visible-DOSBox-X path used for screenshots.
#
# What this CAN verify:
#   - Binary boots without DPMI/CWSDPMI failure.
#   - Title screen renders (paint pipeline works).
#   - Engine accepts keyboard input (Z key advances "New game").
#   - Post-title content renders (intro scene, first cave, etc.).
#   - debug.log + sdldbg.log are captured for offline inspection.
#
# What this CANNOT verify (still requires human eyes):
#   - Whether the rendered content is *correct* (sprite alignment, palette
#     fidelity, text legibility, scrolling smoothness).
#   - Whether audio plays (no audio-capture path on the headless test bot).
#   - Long-running stability (heap fragmentation, memory leaks) — that's
#     the Phase 7 gate's 30-min run and it remains human-in-the-loop.
#   - Save/load round-trip integrity.
#
# So: this script is the floor, not the ceiling. Pass = the binary
# *can* be driven; humans review screenshots to judge whether it
# *should* be shipped.
#
# Usage:
#   tests/run-gameplay-smoke.sh                   # default: --fast, ./build/stage, /tmp/gameplay-smoke
#   tests/run-gameplay-smoke.sh --parity          # parity DOSBox-X config (cycles=fixed 40000)
#   tests/run-gameplay-smoke.sh --out /tmp/foo    # custom artifact dir
#   tests/run-gameplay-smoke.sh --keep-running    # don't kill DOSBox-X at the end

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCHER="$REPO_ROOT/tools/dosbox-launch.sh"
LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE --fast)
OUT_DIR="/tmp/gameplay-smoke"
KEEP_RUNNING=0
DISPLAY="${DOSBOX_DISPLAY:-:0}"

while (($#)); do
  case "$1" in
    --parity)         LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE) ;;
    --out)            shift; OUT_DIR="$1" ;;
    --keep-running)   KEEP_RUNNING=1 ;;
    -h|--help)        sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //' ; exit 0 ;;
    *)                echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$OUT_DIR"
RESULTS="$OUT_DIR/results.txt"
: > "$RESULTS"

log() {
  echo "[gameplay-smoke] $*" | tee -a "$RESULTS"
}

shoot() {
  # shoot <name> — capture a screenshot, store under OUT_DIR/<name>.png
  local name="$1"
  DISPLAY=$DISPLAY scrot -u "$OUT_DIR/$name.png" 2>/dev/null || true
  local size
  size=$(stat -c%s "$OUT_DIR/$name.png" 2>/dev/null || echo 0)
  log "screenshot $name.png ($size bytes)"
}

key() {
  # key <name> — send a key via xdotool; we do NOT focus the window each
  # time (focus once at the start) because re-focusing introduces a
  # window-manager round-trip that desynchronizes the keystroke timing.
  local name="$1"
  DISPLAY=$DISPLAY xdotool key --delay 60 "$name"
  log "sent key: $name"
}

# Refuse to run if DOSBox-X is already up — don't fight the existing lock.
if pgrep -x dosbox-x >/dev/null; then
  echo "[gameplay-smoke] error: dosbox-x already running. Kill it first or use --kill-first via the launcher." >&2
  exit 3
fi

# Refresh stage so we test the current build.
log "make stage..."
make -C "$REPO_ROOT" stage >>"$RESULTS" 2>&1

# Clear prior logs so debug.log/sdldbg.log only contain this run's output.
rm -f "$REPO_ROOT/build/stage/debug.log" "$REPO_ROOT/build/stage/sdldbg.log"

log "launching DOSBox-X (flags: ${LAUNCHER_FLAGS[*]})"
"$LAUNCHER" "${LAUNCHER_FLAGS[@]}" >"$OUT_DIR/launcher.log" 2>&1 &
LAUNCH_PID=$!

# Wait for the dosbox-x process to spawn (the launcher backgrounds it).
for _ in $(seq 1 20); do
  if pgrep -x dosbox-x >/dev/null; then break; fi
  sleep 0.5
done
if ! pgrep -x dosbox-x >/dev/null; then
  log "FAIL: dosbox-x did not start within 10s"
  exit 4
fi
log "dosbox-x started, PID $(pgrep -x dosbox-x)"

# Focus the DOSBox-X window once (xdotool's search is racy if many X clients
# claim "DOSBox" in the title — we wait + retry).
for _ in $(seq 1 10); do
  if DISPLAY=$DISPLAY xdotool search --name DOSBox windowactivate --sync 2>/dev/null; then
    log "DOSBox window focused"
    break
  fi
  sleep 0.5
done

# Milestone sequence. Timing comes from observation: under --fast the engine
# init-to-title takes ~3-4s; the title-screen menu accepts input immediately.
# Adjust if the parity config (--parity) is in use.

sleep 5
shoot "01-title"

# Press Z to confirm "New game" (Cave Story default action key).
key "z"
sleep 4
shoot "02-post-title"

# The post-title state for NXEngine-evo can be either an intro scene or
# a direct cut to first stage; either way, hold a few seconds and capture
# the steady-state render.
sleep 6
shoot "03-mid-scene"

# Try advancing dialogue / skipping intro with another Z press.
key "z"
sleep 3
shoot "04-after-z2"

# Probe player input: send a Right arrow press, see if the screen changes.
# Held key — release shortly after.
DISPLAY=$DISPLAY xdotool keydown Right
sleep 1.5
DISPLAY=$DISPLAY xdotool keyup Right
sleep 1
shoot "05-moved-right"

DISPLAY=$DISPLAY xdotool keydown Left
sleep 1.5
DISPLAY=$DISPLAY xdotool keyup Left
sleep 1
shoot "06-moved-left"

# Try jump.
key "z"
sleep 0.5
shoot "07-jumped"

sleep 2
shoot "08-final"

# Capture engine-side logs before killing DOSBox-X (they only flush on exit
# — see memory note `check_debug_log.md`).
if [[ "$KEEP_RUNNING" == "0" ]]; then
  log "killing DOSBox-X..."
  pkill -x dosbox-x || true
  sleep 2
  cp "$REPO_ROOT/build/stage/debug.log" "$OUT_DIR/debug.log" 2>/dev/null || log "no debug.log captured"
  cp "$REPO_ROOT/build/stage/sdldbg.log" "$OUT_DIR/sdldbg.log" 2>/dev/null || log "no sdldbg.log captured"
fi

# Quick error-count summary so a human reviewer can spot regressions fast.
DEBUG_LOG="$OUT_DIR/debug.log"
SDLDBG_LOG="$OUT_DIR/sdldbg.log"
if [[ -f "$DEBUG_LOG" ]]; then
  ERR_COUNT=$(grep -c '\[error\]' "$DEBUG_LOG" 2>/dev/null || echo 0)
  CRIT_COUNT=$(grep -c '\[critical\]' "$DEBUG_LOG" 2>/dev/null || echo 0)
  DRAWSURF_COUNT=$(grep -c "drawSurface.*invalid" "$DEBUG_LOG" 2>/dev/null || echo 0)
  log "debug.log: $ERR_COUNT errors, $CRIT_COUNT criticals, $DRAWSURF_COUNT drawSurface-invalid"
fi
if [[ -f "$SDLDBG_LOG" ]]; then
  log "sdldbg.log: $(wc -l <"$SDLDBG_LOG") lines"
fi

log "done. Artifacts in: $OUT_DIR"
log "Review screenshots 01..08, debug.log, sdldbg.log."
