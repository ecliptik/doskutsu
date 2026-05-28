#!/usr/bin/env bash
# io-audit-gate.sh -- permanent sprite-sheet RE-DECODE regression gate.
#
# Guards the SFXPAUSE-012 shipped fix (patch 0178, SKIP_SHEET_FLUSH default-ON):
# sprite sheets stay resident across caves, so each sheet is decoded AT MOST
# ONCE per session (lazily, on first on-screen appearance). If the per-cave
# flush ever comes back (the Bug 1 regression -> fire-frame pause on every cave
# entry), a sheet gets decoded a SECOND time and this gate FAILs.
#
# Mechanism: runs DOSKUTSU.EXE under SDL_HINT_DOSKUTSU_IO_AUDIT=1 (patch 0180)
# in DOSBox-X, drives several cave transitions via xdotool, and counts
# [io-audit] phase=gameplay op=loadImage occurrences per file:
#   sprite file seen >= 2 times  -> FAIL (re-decode == flush regression)
#   sprite file seen == 1 time   -> OK (first-visit cold load, inherent)
#   op matched by allowlist      -> EXEMPT (any count; music + bk* backdrops)
#   other phase=gameplay op type -> FAIL (a new lazy-IO landmine)
# Rationale + match syntax: see tests/io-audit-allowlist.txt.
#
# DOSBox-X is a faithful oracle here -- the check is filesystem-IO ordering +
# repetition (which phase + how many times a read happens), not HW-IO timing
# ([[dosbox_not_proxy]]). It CANNOT confirm the g2k stall magnitude, only that
# the read does not RECUR at gameplay phase. That is the right invariant.
#
# CAVEAT: the xdotool drive is not perfectly deterministic (which caves the
# blind drive reaches varies), so the gate keys on per-file repetition, which
# is robust: a re-decode regression repeats the SAME file across caves
# regardless of exact route. Counts may vary run-to-run; the >=2 threshold does
# not.
#
# Usage: tests/io-audit-gate.sh [--regress]
#   default      shipping config (SKIP_SHEET_FLUSH default-ON) -- expect PASS
#   --regress    set SKIP_SHEET_FLUSH=0 (restore per-cave flush) -- self-test,
#                expect FAIL (sprites re-decode every cave). Proves the gate's
#                teeth; not for CI.
#
# Exit: 0 = no re-decode / no new landmine; 1 = regression found;
#       2 = usage error; 4 = dosbox-x failed to start; 5 = no DEBUG.LOG.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
CONF="$REPO/tools/dosbox-x-fast.conf"
STAGE="$REPO/build/stage"
ALLOWLIST="$SCRIPT_DIR/io-audit-allowlist.txt"
export DISPLAY="${DOSBOX_DISPLAY:-:0}"

REGRESS=0
case "${1:-}" in
  --regress) REGRESS=1 ;;
  "")        : ;;
  *)         echo "[io-gate] usage error: unknown arg '$1' (see header)"; exit 2 ;;
esac

[[ -f "$ALLOWLIST" ]] || { echo "[io-gate] FAIL: allowlist not found: $ALLOWLIST"; exit 2; }
[[ -f "$STAGE/DOSKUTSU.EXE" ]] || { echo "[io-gate] FAIL: stage not built -- run 'make stage' first"; exit 2; }

# Keep env block SMALL (DOSBox-X COMMAND.COM env ~720 bytes; 3+ long
# SDL_HINT_DOSKUTSU_* SETs overflow it and silently break autorun).
# Shipping config has SKIP_SHEET_FLUSH default-ON, so the gate sets nothing
# extra in default mode; --regress sets the =0 killswitch to restore flush.
ENV_ARGS=( -c 'SET SDL_HINT_DOSKUTSU_IO_AUDIT=1' )
(( REGRESS )) && ENV_ARGS+=( -c 'SET SDL_HINT_DOSKUTSU_SKIP_SHEET_FLUSH=0' )

pkill -x dosbox-x 2>/dev/null; sleep 2
rm -f "$STAGE/LOGS/DEBUG.LOG" "$STAGE/DEBUG.LOG" "$STAGE/debug.log" 2>/dev/null

dosbox-x -conf "$CONF" -nopromptfolder \
  -c "MOUNT C $STAGE" -c "MOUNT D $REPO/vendor/cwsdpmi" \
  -c 'SET PATH=Z:\;C:\;D:\' \
  -c 'SET BLASTER=A220 I5 D1 H5 T6' \
  -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1' \
  -c 'SET SDL_INVALID_PARAM_CHECKS=0' \
  -c 'SET DOSKUTSU_LOG_VERBOSE=1' \
  "${ENV_ARGS[@]}" \
  -c "C:" -c "DOSKUTSU.EXE" >/tmp/io-audit-gate-launcher.log 2>&1 &

for _ in $(seq 1 20); do pgrep -x dosbox-x >/dev/null && break; sleep 0.5; done
pgrep -x dosbox-x >/dev/null || { echo "[io-gate] FAIL: dosbox-x did not start"; exit 4; }
for _ in $(seq 1 10); do xdotool search --name DOSBox windowactivate --sync 2>/dev/null && break; sleep 0.5; done

key()  { xdotool key --delay 80 "$1"; }
hold() { xdotool keydown "$1"; sleep "$2"; xdotool keyup "$1"; }

sleep 8                              # init -> title
key z; sleep 4                       # New game
key z; sleep 3; key z; sleep 3; key z; sleep 2   # push through intro -> playable
for r in 1 2 3 4 5; do               # drive across cave transitions
  hold Right 2.0; sleep 0.5; key Down; sleep 2.5; key z; sleep 0.8
  hold Left 1.2;  sleep 0.5; key Down; sleep 2.0
done
sleep 2

SRC=""
for cand in "$STAGE/LOGS/DEBUG.LOG" "$STAGE/DEBUG.LOG" "$STAGE/debug.log"; do
  [[ -f "$cand" ]] && { SRC="$cand"; break; }
done
[[ -n "$SRC" ]] || { echo "[io-gate] FAIL: no DEBUG.LOG captured"; pkill -x dosbox-x; exit 5; }
LOG="/tmp/io-audit-gate-debug.log"; cp "$SRC" "$LOG"
pkill -x dosbox-x; sleep 3; [[ -f "$SRC" ]] && cp "$SRC" "$LOG"; pkill -9 -x dosbox-x 2>/dev/null

# Load allowlist rules (strip comments/blanks).
mapfile -t RULES < <(grep -vE '^\s*(#|$)' "$ALLOWLIST")
matches_rule() {  # $1=op=TYPE  $2=BASENAME
  local op="$1" file="$2" rop rfile
  for rule in "${RULES[@]}"; do
    rop="${rule%% *}"; rfile="${rule#* }"; rfile="${rfile#file=}"
    [[ "$op" == "$rop" ]] || continue
    # shellcheck disable=SC2053
    [[ "$file" == $rfile ]] && return 0
  done
  return 1
}

# All phase=gameplay ops, normalized "op=TYPE file=BASENAME".
mapfile -t ALL_OPS < <(grep '\[io-audit\] phase=gameplay' "$LOG" \
  | sed -E 's/.*\[io-audit\] phase=gameplay (op=[a-zA-Z_]+) (file=[^ ]+).*/\1 \2/')

echo "[io-gate] config: IO_AUDIT=1 SKIP_SHEET_FLUSH=$([[ $REGRESS == 1 ]] && echo 0 || echo default-ON)"
echo "[io-gate] phase=gameplay ops total: ${#ALL_OPS[@]}"

# Tally op=loadImage counts per file; check non-loadImage ops against allowlist.
declare -A IMG_COUNT
VIOLATIONS=0
for entry in "${ALL_OPS[@]}"; do
  [[ -z "$entry" ]] && continue
  op="${entry%% *}"; filetok="${entry#* }"; base="${filetok#file=}"
  if [[ "$op" == "op=loadImage" ]]; then
    if matches_rule "$op" "$base"; then continue; fi   # bk* backdrops exempt
    IMG_COUNT["$base"]=$(( ${IMG_COUNT["$base"]:-0} + 1 ))
  else
    if matches_rule "$op" "$base"; then continue; fi   # music exempt
    echo "  [FAIL] $op file=$base  (UNLISTED non-sprite gameplay-IO landmine)"
    VIOLATIONS=$((VIOLATIONS+1))
  fi
done

# Re-decode rule: a sprite sheet decoded >= 2 times at gameplay phase = the
# per-cave flush regressed.
for f in "${!IMG_COUNT[@]}"; do
  n="${IMG_COUNT[$f]}"
  if (( n >= 2 )); then
    echo "  [FAIL] op=loadImage file=$f  decoded ${n}x at gameplay phase (RE-DECODE -> SKIP_SHEET_FLUSH regression)"
    VIOLATIONS=$((VIOLATIONS+1))
  else
    echo "  [OK]   op=loadImage file=$f  (first-visit cold load, 1x)"
  fi
done

echo ""
if (( VIOLATIONS > 0 )); then
  echo "[io-gate] GATE FAIL: $VIOLATIONS sprite re-decode(s) / new landmine(s) at gameplay phase."
  echo "          A sprite decoded >1x means the SFXPAUSE-012 per-cave flush came back"
  echo "          (Bug 1 fire-frame-pause regression). Restore SKIP_SHEET_FLUSH default-ON,"
  echo "          or for a genuinely new irreducible op add a justified allowlist line."
  exit 1
fi
echo "[io-gate] GATE PASS: no sprite re-decodes; all sheets decode at most once (first-visit)."
exit 0
