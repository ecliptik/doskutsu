#!/usr/bin/env bash
#
# tests/run-tas-smoke.sh -- DOSBox-X round-trip for the TAS subsystem
# (patch nxengine-evo/0135).
#
# Runs DOSKUTSU.EXE twice under DOSBox-X:
#   1. RECORD mode -- DOSKUTSU_TAS_RECORD=PLAY.TAS, AUTO_EXIT_TICK=100;
#      captures ~2 seconds of title-screen idle input into PLAY.TAS,
#      then auto-exits cleanly via game.running = false.
#   2. REPLAY mode -- DOSKUTSU_TAS_REPLAY=PLAY.TAS, AUTO_EXIT_TICK=100;
#      re-runs against the same PLAY.TAS recording, validates that the
#      replay banner emits + the engine reaches the auto-exit tick.
#
# Post-run verification:
#   - PLAY.TAS exists with file size > 20 bytes (= header at minimum)
#   - First 7 bytes of PLAY.TAS are "DTASv1\n" (the magic)
#   - DEBUG.LOG from the RECORD run contains "tas: record opened"
#     and "tas: auto-exit at tick"
#   - DEBUG.LOG from the REPLAY run contains "tas: replay opened"
#     and "tas: auto-exit at tick"
#
# What this smoke does NOT verify (deferred to v2 TAS or real-HW iter):
#   - Byte-identical engine state between record + replay at a checkpoint
#     tick (would need engine-side state dump at AUTO_EXIT)
#   - Replay drift tolerance (no input pressed during smoke, so all
#     ticks are no-event; the lookahead loop in tas.cpp never iterates)
#
# Usage:
#   tests/run-tas-smoke.sh                           # default: build/stage layout
#   tests/run-tas-smoke.sh --out /tmp/foo            # custom artifact dir
#   tests/run-tas-smoke.sh --exit-tick 200           # override AUTO_EXIT_TICK

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE_DIR="$REPO_ROOT/build/stage"
OUT_DIR="/tmp/tas-smoke"
EXIT_TICK=100
DOSBOX_X_BIN="${DOSBOX_X:-dosbox-x}"

while (($#)); do
  case "$1" in
    --out)          shift; OUT_DIR="$1" ;;
    --exit-tick)    shift; EXIT_TICK="$1" ;;
    -h|--help)      sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //' ; exit 0 ;;
    *)              echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$OUT_DIR"
RESULTS="$OUT_DIR/results.txt"
: > "$RESULTS"

log() {
  echo "[tas-smoke] $*" | tee -a "$RESULTS"
}

if [[ ! -d "$STAGE_DIR" ]]; then
  log "error: $STAGE_DIR not present -- run \`make stage\` first" >&2
  exit 1
fi
if [[ ! -f "$STAGE_DIR/DOSKUTSU.EXE" ]]; then
  log "error: $STAGE_DIR/DOSKUTSU.EXE not present -- run \`make stage\` first" >&2
  exit 1
fi
if ! command -v "$DOSBOX_X_BIN" >/dev/null 2>&1; then
  log "error: $DOSBOX_X_BIN not on PATH" >&2
  exit 1
fi

# Layout note: on DOS, the engine's Logger writes to ResourceManager::
# getPrefPath(filename) which resolves to the engine's CWD (= stage root
# under DOSBox-X mount). SDL_Log (patch 0024) writes to /DOSKUTSU/<TAG>SDL.LOG.
# So:
#   stage_root/RECORD.LOG   <- engine debug log (we check this)
#   stage_root/REPLAY.LOG   <- engine debug log (we check this)
#   stage_root/PLAY.TAS     <- TAS record output (relative to CWD)
#   stage_root/DOSKUTSU/RECORDSD.LOG <- SDL log (not checked here)
#
# Defensive: ensure /DOSKUTSU/ subdir exists so SDL_Log doesn't silently
# fail to create its sink (DJGPP fopen returns NULL on missing-dir paths).
mkdir -p "$STAGE_DIR/DOSKUTSU"

# ---------------------------------------------------------------------
# Run 1: RECORD mode
# ---------------------------------------------------------------------

log "=== RECORD pass (TICKS=$EXIT_TICK) ==="

# Clear prior artifacts. The .TAS file lives at stage root (CWD when
# DOSKUTSU.EXE runs); DEBUG.LOG lives under DOSKUTSU/ per Logger::init.
rm -f "$STAGE_DIR/PLAY.TAS" \
      "$STAGE_DIR/debug.log" \
      "$STAGE_DIR/RECORD.LOG" \
      "$STAGE_DIR/REPLAY.LOG" \
      "$STAGE_DIR/DOSKUTSU/RECORDSD.LOG" \
      "$STAGE_DIR/DOSKUTSU/REPLAYSD.LOG"

# DOSKUTSU_LOG_TAG=RECORD makes the engine log to /DOSKUTSU/RECORD.LOG
# (per patch 0069), so we can keep RECORD-pass and REPLAY-pass logs
# disjoint.
RECORD_BAT="$STAGE_DIR/TASREC.BAT"
cat > "$RECORD_BAT" <<EOF
@echo off
SET DOSKUTSU_LOG_TAG=RECORD
SET DOSKUTSU_TAS_RECORD=PLAY.TAS
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=$EXIT_TICK
SET BLASTER=A220 I5 D1 H5 T6
SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1
DOSKUTSU.EXE
EXIT
EOF

# Run DOSBox-X with the fast config (cycles=max so the 100 ticks complete
# in well under the 30s timeout). -silent prevents the GUI noise; -exit
# means DOSBox-X exits when the BAT finishes.
timeout 30 "$DOSBOX_X_BIN" \
  -conf "$REPO_ROOT/tools/dosbox-x-fast.conf" \
  -c "MOUNT C \"$STAGE_DIR\"" \
  -c "C:" \
  -c "TASREC.BAT" \
  -silent -exit >>"$RESULTS" 2>&1 || log "DOSBox-X record pass exited (timeout or normal)"

# Verify .TAS file produced
if [[ ! -f "$STAGE_DIR/PLAY.TAS" ]]; then
  log "FAIL: PLAY.TAS not produced after RECORD pass"
  exit 5
fi
SIZE=$(stat -c%s "$STAGE_DIR/PLAY.TAS")
if [[ "$SIZE" -lt 20 ]]; then
  log "FAIL: PLAY.TAS too small ($SIZE bytes; need >= 20 for header)"
  exit 5
fi
log "  PLAY.TAS size = $SIZE bytes"

# Verify magic (first 7 bytes = "DTASv1\n"). Use hex comparison since
# bash $(...) strips trailing newlines from command substitutions, which
# would consume the \n byte we want to validate. Expected hex:
#   D=44 T=54 A=41 S=53 v=76 1=31 \n=0a
MAGIC_HEX=$(head -c 7 "$STAGE_DIR/PLAY.TAS" | od -An -tx1 | tr -d ' \n')
EXPECTED_HEX="4454415376310a"
if [[ "$MAGIC_HEX" != "$EXPECTED_HEX" ]]; then
  log "FAIL: PLAY.TAS magic mismatch (got hex '$MAGIC_HEX'; expected '$EXPECTED_HEX')"
  exit 5
fi
log "  magic = DTASv1\\n (OK; hex $MAGIC_HEX)"

# Verify banners
REC_LOG="$STAGE_DIR/RECORD.LOG"
if [[ ! -f "$REC_LOG" ]]; then
  log "FAIL: RECORD.LOG not produced"
  exit 5
fi
if ! grep -q "tas: record opened" "$REC_LOG"; then
  log "FAIL: RECORD.LOG missing 'tas: record opened' banner"
  exit 5
fi
log "  banner 'tas: record opened' (OK)"
if ! grep -q "tas: auto-exit at tick" "$REC_LOG"; then
  log "FAIL: RECORD.LOG missing 'tas: auto-exit at tick' banner"
  exit 5
fi
log "  banner 'tas: auto-exit at tick' (OK)"

# Stash the artifacts
cp "$STAGE_DIR/PLAY.TAS" "$OUT_DIR/"
cp "$REC_LOG" "$OUT_DIR/RECORD.LOG"

# ---------------------------------------------------------------------
# Run 2: REPLAY mode
# ---------------------------------------------------------------------

log ""
log "=== REPLAY pass (TICKS=$EXIT_TICK) ==="

REPLAY_BAT="$STAGE_DIR/TASREP.BAT"
cat > "$REPLAY_BAT" <<EOF
@echo off
SET DOSKUTSU_LOG_TAG=REPLAY
SET DOSKUTSU_TAS_REPLAY=PLAY.TAS
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=$EXIT_TICK
SET BLASTER=A220 I5 D1 H5 T6
SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1
DOSKUTSU.EXE
EXIT
EOF

timeout 30 "$DOSBOX_X_BIN" \
  -conf "$REPO_ROOT/tools/dosbox-x-fast.conf" \
  -c "MOUNT C \"$STAGE_DIR\"" \
  -c "C:" \
  -c "TASREP.BAT" \
  -silent -exit >>"$RESULTS" 2>&1 || log "DOSBox-X replay pass exited (timeout or normal)"

REP_LOG="$STAGE_DIR/REPLAY.LOG"
if [[ ! -f "$REP_LOG" ]]; then
  log "FAIL: REPLAY.LOG not produced"
  exit 5
fi
if ! grep -q "tas: replay opened" "$REP_LOG"; then
  log "FAIL: REPLAY.LOG missing 'tas: replay opened' banner"
  exit 5
fi
log "  banner 'tas: replay opened' (OK)"
# Default replay path: EOF-auto-exit triggers BEFORE AUTO_EXIT_TICK in
# this smoke because the test recording is short (~1 event). Either
# the EOF-driven exit OR the AUTO_EXIT_TICK exit constitutes a clean
# termination; accept either banner as the termination witness.
if grep -q "tas: end-of-replay auto-exit at tick" "$REP_LOG"; then
  log "  banner 'tas: end-of-replay auto-exit' (OK; EOF-driven termination, the default behavior)"
elif grep -q "tas: auto-exit at tick" "$REP_LOG"; then
  log "  banner 'tas: auto-exit at tick' (OK; AUTO_EXIT_TICK-driven termination)"
else
  log "FAIL: REPLAY.LOG missing termination banner (neither 'end-of-replay auto-exit' nor 'auto-exit at tick')"
  exit 5
fi

cp "$REP_LOG" "$OUT_DIR/REPLAY.LOG"

log ""
log "PASS: TAS record/replay round-trip"
log "Artifacts in $OUT_DIR/"
exit 0
