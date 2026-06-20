#!/usr/bin/env bash
# org-cache.sh -- pre-render every Organya song to the build-sha-keyed PCM cache
# by running the built DOSKUTSU.EXE headless under DOSBox-X (max cycles) with
# DOSKUTSU_ORG_PRECACHE_ALL=1. Produces CACHE/<rate>_<channels>/*.PCM keyed to
# the rendering binary's DOSKUTSU_BUILD_SHA12 (in each PCM header), so the cache
# only HITs on the exact binary that produced it.
#
# Invoked by `make org-cache`. Standalone use:
#   scripts/org-cache.sh                                  # render build/doskutsu.exe, Tier-2
#   ORGCACHE_EXE=/path/DOSKUTSU.EXE scripts/org-cache.sh  # render a specific binary
#   TIER=2 scripts/org-cache.sh                           # 11025 mono (default)
#
# LICENSING: the produced CACHE/ is Cave-Story-DERIVED (rendered from the user's
# extracted .org files). It is a LOCAL / OPERATOR-DEPLOY artifact ONLY and must
# NEVER be included in `make dist` (the public zip) -- see docs/BUILDING.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONF_FAST="$REPO_ROOT/tools/dosbox-x-fast.conf"
CWSDPMI="${CWSDPMI:-$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe}"
# shellcheck source=../tools/dosbox-teardown.sh
source "$REPO_ROOT/tools/dosbox-teardown.sh"   # dbx_kill_conf -- conf-scoped teardown

# --- inputs (overridable) --------------------------------------------------
ORGCACHE_EXE="${ORGCACHE_EXE:-$REPO_ROOT/build/doskutsu.exe}"
TIER="${TIER:-2}"                          # 2 = Tier-2 11025 mono (default); 1 = Tier-1 22050 stereo
WORK="${ORGCACHE_WORK:-$REPO_ROOT/build/orgcache}"
TIMEOUT_S="${ORGCACHE_TIMEOUT:-600}"       # hard wall-clock cap on the DOSBox run

# --- resolve tier -> rate/channels/subdir ----------------------------------
if [ "$TIER" = "2" ]; then   RATE=11025; CH=1; TIER2=1
elif [ "$TIER" = "1" ]; then RATE=22050; CH=2; TIER2=0
else echo "[org-cache] error: TIER must be 1 (22050 stereo) or 2 (11025 mono); got '$TIER'" >&2; exit 2; fi
SUBDIR="${RATE}_${CH}"

# --- guards ----------------------------------------------------------------
[ -f "$ORGCACHE_EXE" ] || { echo "[org-cache] error: DOSKUTSU.EXE not found: $ORGCACHE_EXE" >&2
  echo "           build it first ('make') or set ORGCACHE_EXE=/path/to/DOSKUTSU.EXE" >&2; exit 1; }
[ -f "$CWSDPMI" ] || { echo "[org-cache] error: CWSDPMI.EXE missing: $CWSDPMI -- run ./scripts/fetch-vendor-binaries.sh" >&2; exit 1; }
ORGN="$(ls "$REPO_ROOT"/data/org/*.org 2>/dev/null | wc -l | tr -d ' ')"
[ "$ORGN" -ge 1 ] || { echo "[org-cache] error: no .org files at $REPO_ROOT/data/org -- Cave Story data required (see docs/ASSETS.md)" >&2; exit 1; }
command -v dosbox-x >/dev/null || { echo "[org-cache] error: dosbox-x not on PATH" >&2; exit 1; }
if pgrep -x dosbox-x >/dev/null; then echo "[org-cache] error: dosbox-x already running -- kill it first (no concurrent instances)" >&2; exit 1; fi
if [ "$TIER" = "1" ]; then
  echo "[org-cache] note: Tier-1 (22050 stereo) HQ set -- true interleaved-stereo render" >&2
  echo "           (patch 0231, v1.0.9). Larger cache + real fps cost on 486-class HW;" >&2
  echo "           TIER=2 is the lighter LQ (11025 mono) default. Both sets coexist." >&2
fi

echo "[org-cache] binary : $ORGCACHE_EXE"
echo "[org-cache] tier   : Tier-$TIER -> ${RATE} Hz / ${CH} ch -> CACHE/$SUBDIR/"
echo "[org-cache] org src: $ORGN .org files at data/org/"

# --- stage a clean work dir ------------------------------------------------
rm -rf "$WORK"
mkdir -p "$WORK/LOGS" "$WORK/DOSKUTSU/LOGS"
install -m 0644 "$ORGCACHE_EXE" "$WORK/DOSKUTSU.EXE"
install -m 0644 "$CWSDPMI"      "$WORK/CWSDPMI.EXE"
ln -s "$REPO_ROOT/data" "$WORK/data"
rm -rf "$WORK/CACHE"

# precache launcher: COMMAND /E:2048 self-relaunch so the long SDL_HINT_* SETs
# cannot overflow the default DOS environment block (silent SET failure).
{
  printf '@echo off\r\n'
  printf 'if "%%1"=="GO" goto run\r\n'
  printf 'COMMAND /E:2048 /C %%0 GO\r\n'
  printf 'goto end\r\n'
  printf ':run\r\n'
  printf 'SET SDL_HINT_DOSKUTSU_AUDIO_TIER2=%s\r\n' "$TIER2"
  printf 'SET DOSKUTSU_ORG_PRECACHE_ALL=1\r\n'
  printf 'SET DOSKUTSU_LOG_TAG=PCACH\r\n'
  printf 'DOSKUTSU.EXE\r\n'
  printf ':end\r\n'
} > "$WORK/PCACHE.BAT"

LOG="$WORK/LOGS/PCACH.LOG"

# --- run headless, poll for completion, never leave an orphan ---------------
cleanup() { dbx_kill_conf "$CONF_FAST" 2>/dev/null || true; }
trap cleanup EXIT

echo "[org-cache] rendering under DOSBox-X (max cycles, headless)..."
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" dosbox-x -conf "$CONF_FAST" -nopromptfolder \
  -c "MOUNT C $WORK" \
  -c "MOUNT D $REPO_ROOT/vendor/cwsdpmi" \
  -c 'SET PATH=Z:\;C:\;D:\' \
  -c 'SET BLASTER=A220 I5 D1 H5 T6' \
  -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1' \
  -c 'SET SDL_INVALID_PARAM_CHECKS=0' \
  -c 'SET DOSKUTSU_LOG_VERBOSE=1' \
  -c "C:" -c "PCACHE.BAT" >"$WORK/dosbox.out" 2>&1 &
DBX=$!

elapsed=0
while kill -0 "$DBX" 2>/dev/null; do
  if grep -q '\[org-precache\] done' "$LOG" 2>/dev/null; then break; fi
  if [ "$elapsed" -ge "$TIMEOUT_S" ]; then
    echo "[org-cache] error: timed out after ${TIMEOUT_S}s waiting for precache to finish" >&2
    break
  fi
  sleep 3; elapsed=$((elapsed + 3))
done
dbx_kill_conf "$CONF_FAST" 2>/dev/null || true
wait "$DBX" 2>/dev/null || true
trap - EXIT

# --- two-witness verification ----------------------------------------------
TOTAL_LINE="$(grep -E '\[org-precache\] TOTAL ' "$LOG" 2>/dev/null | tail -1 | tr -d '\r' || true)"
[ -n "$TOTAL_LINE" ] || { echo "[org-cache] FAIL: no '[org-precache] TOTAL' line in $LOG (precache did not complete)" >&2; exit 1; }
echo "$TOTAL_LINE" | grep -q 'failed=0' || { echo "[org-cache] FAIL: precache reported failures: $TOTAL_LINE" >&2; exit 1; }

RENDERED="$(echo "$TOTAL_LINE" | sed -n 's/.*rendered=\([0-9]\+\).*/\1/p')"
SKIPPED="$(echo "$TOTAL_LINE"  | sed -n 's/.*skipped=\([0-9]\+\).*/\1/p')"
EXPECT=$(( ${RENDERED:-0} + ${SKIPPED:-0} ))
PCMN="$(ls "$WORK/CACHE/$SUBDIR"/*.PCM 2>/dev/null | wc -l | tr -d ' ')"
[ "$PCMN" -ge 1 ] || { echo "[org-cache] FAIL: no *.PCM in $WORK/CACHE/$SUBDIR" >&2; exit 1; }
[ "$PCMN" = "$EXPECT" ] || { echo "[org-cache] FAIL: PCM count $PCMN != rendered+skipped $EXPECT (log vs disk mismatch)" >&2; exit 1; }
SIZE="$(du -sh "$WORK/CACHE/$SUBDIR" 2>/dev/null | cut -f1)"

echo "[org-cache] $TOTAL_LINE"
echo "[org-cache] OK: $PCMN PCMs ($SIZE) -> $WORK/CACHE/$SUBDIR/"
echo "[org-cache] NOTE: Cave-Story-derived cache -- local/operator-deploy ONLY, never 'make dist'."
