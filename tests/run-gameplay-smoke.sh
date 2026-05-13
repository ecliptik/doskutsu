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
#   tests/run-gameplay-smoke.sh --skip-gate       # capture logs but skip the banner-emit gate
#                                                 # (intended for wave-39 ablation builds where
#                                                 # reverted patches make required banners
#                                                 # unreachable by design — gate would fail
#                                                 # spuriously; flush-instr decomp verifies the
#                                                 # expected absences from the captured logs)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCHER="$REPO_ROOT/tools/dosbox-launch.sh"
LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE --fast)
OUT_DIR="/tmp/gameplay-smoke"
KEEP_RUNNING=0
SKIP_GATE=0
DISPLAY="${DOSBOX_DISPLAY:-:0}"

while (($#)); do
  case "$1" in
    --parity)         LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE) ;;
    --out)            shift; OUT_DIR="$1" ;;
    --keep-running)   KEEP_RUNNING=1 ;;
    --skip-gate)      SKIP_GATE=1 ;;
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

# Capture engine-side logs before killing DOSBox-X. DJGPP fopen writes uppercase
# 8.3 names; the staged Linux tree is case-sensitive, so the engine's DEBUG.LOG
# lands at build/stage/DEBUG.LOG and SDL/0024's SDLDBG.LOG lands inside the
# DOSKUTSU subdir per its hard-coded "/DOSKUTSU/sdldbg.log" path. Patch 0036
# (nxengine) + SDL/0024 fsync-per-line make these readable without waiting for
# DOSBox-X to exit, but we still kill the process first so the gate runs on a
# fully-flushed log without a race window.
if [[ "$KEEP_RUNNING" == "0" ]]; then
  log "killing DOSBox-X..."
  pkill -x dosbox-x || true
  sleep 2
  cp "$REPO_ROOT/build/stage/DEBUG.LOG" "$OUT_DIR/debug.log" 2>/dev/null || log "no debug.log captured (looked at build/stage/DEBUG.LOG)"
  cp "$REPO_ROOT/build/stage/DOSKUTSU/SDLDBG.LOG" "$OUT_DIR/sdldbg.log" 2>/dev/null || log "no sdldbg.log captured (looked at build/stage/DOSKUTSU/SDLDBG.LOG)"
fi

# Quick error-count summary so a human reviewer can spot regressions fast.
DEBUG_LOG="$OUT_DIR/debug.log"
SDLDBG_LOG="$OUT_DIR/sdldbg.log"
if [[ -f "$DEBUG_LOG" ]]; then
  # grep -c outputs "0" + exit 1 on zero matches; `|| true` suppresses the
  # non-zero exit without appending a second "0" the way `|| echo 0` would.
  ERR_COUNT=$(grep -c '\[error\]' "$DEBUG_LOG" 2>/dev/null || true)
  CRIT_COUNT=$(grep -c '\[critical\]' "$DEBUG_LOG" 2>/dev/null || true)
  DRAWSURF_COUNT=$(grep -c "drawSurface.*invalid" "$DEBUG_LOG" 2>/dev/null || true)
  log "debug.log: ${ERR_COUNT:-0} errors, ${CRIT_COUNT:-0} criticals, ${DRAWSURF_COUNT:-0} drawSurface-invalid"
fi
if [[ -f "$SDLDBG_LOG" ]]; then
  log "sdldbg.log: $(wc -l <"$SDLDBG_LOG") lines"
fi

# Banner-emit gate. `strings | grep` of the binary proves the literal lives in
# .rodata but NOT that the surrounding LOG_* call site is reachable from
# runtime — dead code paths keep their string literals. This gate captures the
# logs after the smoke run and requires each regex below to match ≥1 line in
# DEBUG.LOG ∪ SDLDBG.LOG. Regex alternations cover both default-ON and
# default-OFF variants so the gate stays correct across killswitch flips and
# only fails when the call site itself is dead. Add a new entry whenever a
# lever ships a boot/init banner. See CLAUDE.md § Critical Rules § Build
# verification.
#
# Parallel arrays (indexed by position). REGEX uses `|` for alternations so
# fields are kept separate rather than packed-and-split.

BANNER_REGEX=(
  "Renderer::initVideo: opaque-tile fastpath (ENABLED|DISABLED)"
  "Renderer::initVideo: Cirrus BLT solid-fill consumer (ENABLED|DISABLED)"
  "sdl: SDL/0059 Cirrus BLT solid-fill (ACTIVE|DISABLED|N/A)"
  "cirrus-blt-async: (enabled|disabled)"
  "gameloop: (legacy combined-tick path|Mechanism A.2 tick split ACTIVE)"
  "\[0142 abl_cache_test n="
  "sdl: SDL/0060 Cirrus BLT pattern-copy (ACTIVE|DISABLED|N/A)"
)
BANNER_SEVERITY=(
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
)
BANNER_LABEL=(
  "lever-1 opaque-tile fastpath (patch 0137)"
  "lever-2b nx-engine consumer (patch 0138)"
  "lever-2a SDL primitive (patch SDL/0059)"
  "lever-3 BULK_COPY async (patch 0139)"
  "A.2 gameloop tick (patch 0141)"
  "abl-cache disambiguation bench (patch 0142)"
  "BLTPAT primitive (patch SDL/0060)"
)

if [[ "$SKIP_GATE" == "1" ]]; then
  log ""
  log "=== Banner-emit gate SKIPPED (--skip-gate) ==="
  log "Logs still captured at $OUT_DIR/{debug,sdldbg}.log for flush-instr decomp."
  log "Caller must verify expected banner absences match the ablation contract."
  log "done. Artifacts in: $OUT_DIR"
  exit 0
fi

GATE_FAIL=0
log ""
log "=== Banner-emit gate (proves runtime invocation, not just embed) ==="

for i in "${!BANNER_REGEX[@]}"; do
  regex="${BANNER_REGEX[$i]}"
  severity="${BANNER_SEVERITY[$i]}"
  label="${BANNER_LABEL[$i]}"

  hit_total=0
  hit_source=""
  for src in "$DEBUG_LOG" "$SDLDBG_LOG"; do
    [[ -f "$src" ]] || continue
    c=$(grep -cE "$regex" "$src" 2>/dev/null || true)
    c="${c:-0}"
    if [[ "$c" -gt 0 ]]; then
      hit_total=$((hit_total + c))
      hit_source="${hit_source:+$hit_source,}$(basename "$src")=$c"
    fi
  done

  case "$severity" in
    required)
      if [[ "$hit_total" -gt 0 ]]; then
        log "  PASS [$label] emits=$hit_total ($hit_source)"
      else
        log "  FAIL [$label] emits=0 — REQUIRED banner absent; runtime code path is dead"
        log "        regex: $regex"
        GATE_FAIL=1
      fi
      ;;
    forbidden)
      if [[ "$hit_total" -gt 0 ]]; then
        log "  FAIL [$label] emits=$hit_total ($hit_source) — FORBIDDEN banner present; incomplete revert (banner literal in .rodata + runtime emit means the patch's code is still live)"
        log "        regex: $regex"
        GATE_FAIL=1
      else
        log "  PASS [$label] emits=0 (correctly absent)"
      fi
      ;;
    optional)
      log "  INFO [$label] emits=$hit_total ($hit_source) — informational; no gate effect"
      ;;
  esac
done

if [[ "$GATE_FAIL" -gt 0 ]]; then
  log ""
  log "GATE FAIL: one or more REQUIRED banners absent (or FORBIDDEN banners present)."
  log "This is the wave-38 failure mode: code that strings|grep finds in the binary"
  log "is not actually reached at runtime. Investigate the failing patch(es) before"
  log "shipping. See CLAUDE.md § Critical Rules — Build verification."
  log "done. Artifacts in: $OUT_DIR"
  exit 5
fi

log ""
log "GATE PASS: all required banners emit at runtime."
log "done. Artifacts in: $OUT_DIR"
log "Review screenshots 01..08, debug.log, sdldbg.log."
