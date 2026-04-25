#!/usr/bin/env bash
# run-sdl3-image-smoke.sh — Path B / #28 functional smoke runner.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/sdl3-image-smoke/imgsmk.exe"
LOG="$REPO_ROOT/build/sdl3-image-smoke/imgsmk.log"

if [[ ! -f "$EXE" ]]; then
    echo "run-sdl3-image-smoke.sh: $EXE missing — build with \`make sdl3-image-smoke\`" >&2
    exit 2
fi

echo "[sdl3-image-smoke] running $EXE under parity DOSBox-X config"
if ! "$REPO_ROOT/tools/dosbox-run.sh" --exe "$EXE" --stdout "$LOG"; then
    echo "[sdl3-image-smoke] FAIL: dosbox-run.sh failed" >&2
    exit 2
fi
if [[ ! -s "$LOG" ]]; then
    echo "[sdl3-image-smoke] FAIL: capture is empty (exe may have crashed under DPMI)" >&2
    exit 2
fi

REQUIRED=(
    "IMGTEST-BEGIN:"        # exe started, printf works under DPMI
    "SDL-INIT: OK"          # SDL3 video init OK (proves DPMI + SDL3 healthy)
    "IO-FROM-MEM: OK"       # SDL_IOFromMem works
    "IMG-LOAD: OK"          # IMG_Load_IO returned non-NULL
    "IMG-DIMS: OK 1x1"      # PNG decoded with correct 1x1 geometry
    "IMGTEST-END: rc=0"     # process completed cleanly with all checks green
)

failures=0
echo "[sdl3-image-smoke] verifying capture (build/sdl3-image-smoke/imgsmk.log)..."
for needle in "${REQUIRED[@]}"; do
    if tr -d '\r' < "$LOG" | grep -F -q -- "$needle"; then
        echo "  PASS  contains:  $needle"
    else
        echo "  FAIL  missing:   $needle" >&2
        failures=$((failures+1))
    fi
done

if [[ "$failures" -gt 0 ]]; then
    echo "[sdl3-image-smoke] FAIL: $failures required substring(s) missing" >&2
    echo "[sdl3-image-smoke] full capture:" >&2
    sed 's/^/    /' "$LOG" >&2
    exit 1
fi

echo "[sdl3-image-smoke] PASS: PNG decode via stb_image works on DJGPP+SDL3"
