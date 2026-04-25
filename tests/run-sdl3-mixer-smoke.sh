#!/usr/bin/env bash
# run-sdl3-mixer-smoke.sh — Path B spike functional gate runner.
#
# Runs build/sdl3-mixer-smoke/mixertest.exe under DOSBox-X headless and
# verifies SDL3_mixer's three NXEngine-equivalent code paths actually work.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 8.3 DOS filename — matches Makefile's SDL3_MIXER_SMOKE_EXE.
EXE="$REPO_ROOT/build/sdl3-mixer-smoke/mixsmk.exe"
LOG="$REPO_ROOT/build/sdl3-mixer-smoke/mixsmk.log"

if [[ ! -f "$EXE" ]]; then
    echo "run-sdl3-mixer-smoke.sh: $EXE missing — build with \`make sdl3-mixer-smoke\`" >&2
    exit 2
fi

echo "[sdl3-mixer-smoke] running $EXE under parity DOSBox-X config"
if ! "$REPO_ROOT/tools/dosbox-run.sh" --exe "$EXE" --stdout "$LOG"; then
    echo "[sdl3-mixer-smoke] FAIL: dosbox-run.sh failed" >&2
    exit 2
fi
if [[ ! -s "$LOG" ]]; then
    echo "[sdl3-mixer-smoke] FAIL: capture is empty (exe may have crashed under DPMI)" >&2
    exit 2
fi

# Required substrings — each represents one of the three NXEngine audio
# code paths confirmed working end-to-end on DJGPP under DOSBox-X.
REQUIRED=(
    "MIXTEST-BEGIN:"           # exe started
    "MIX-INIT: OK"             # MIX_Init survived (audio + DPMI runtime OK)
    "MIX-DECODERS: count="     # decoder enumeration ran
    "MIX-CREATE-MIXER: OK"     # MIX_CreateMixer with dummy device OK
    "RAW-LOAD: OK"             # Organya path: MIX_LoadRawAudio worked
    "WAV-LOAD: OK"             # Cave Story SFX path: WAV decoder worked
    "VORBIS-DECODER: OK"       # Remix path: stb_vorbis decoder registered
    "MIXTEST-END: rc=0"        # process completed cleanly with all checks green
)

failures=0
echo "[sdl3-mixer-smoke] verifying capture (build/sdl3-mixer-smoke/mixsmk.log)..."
for needle in "${REQUIRED[@]}"; do
    if tr -d '\r' < "$LOG" | grep -F -q -- "$needle"; then
        echo "  PASS  contains:  $needle"
    else
        echo "  FAIL  missing:   $needle" >&2
        failures=$((failures+1))
    fi
done

if [[ "$failures" -gt 0 ]]; then
    echo "[sdl3-mixer-smoke] FAIL: $failures required substring(s) missing" >&2
    echo "[sdl3-mixer-smoke] full capture:" >&2
    sed 's/^/    /' "$LOG" >&2
    exit 1
fi

echo "[sdl3-mixer-smoke] PASS: all three NXEngine-equivalent audio paths working"
