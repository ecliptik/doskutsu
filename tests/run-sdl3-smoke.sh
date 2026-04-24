#!/usr/bin/env bash
# run-sdl3-smoke.sh — Phase 2d SDL3 DOS-backend smoke runner.
#
# Runs build/sdl3-smoke/sdltest.exe under DOSBox-X headless, captures stdout
# (the probe writes via printf so plain `> STDOUT.TXT` redirection suffices),
# verifies several known-stable substrings appear, and writes:
#
#   build/sdl3-smoke/sdltest.log              full capture, kept after run
#   tests/fixtures/sdl3-modes-dosbox.txt      Phase 8 baseline
#                                              (full capture annotated with
#                                              host/config provenance for
#                                              real-HW diff)
#
# Exit codes:
#   0  — all required substrings present
#   1  — one or more required substrings missing
#   2  — invocation error (exe missing, dosbox-run.sh failed, etc.)

set -uo pipefail
# NOTE: deliberately not using `set -e` because we want to keep running
# through partial failures so the operator sees every check at once.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/sdl3-smoke/sdltest.exe"
LOG="$REPO_ROOT/build/sdl3-smoke/sdltest.log"
FIXTURE="$REPO_ROOT/tests/fixtures/sdl3-modes-dosbox.txt"
RUN_SMOKE="$SCRIPT_DIR/run-smoke.sh"

if [[ ! -f "$EXE" ]]; then
    echo "run-sdl3-smoke.sh: $EXE missing — build with \`make sdl3-smoke\`" >&2
    exit 2
fi

# Single capture; assertions run against it locally without relaunching
# DOSBox-X each time.
echo "[sdl3-smoke] running $EXE under parity DOSBox-X config"
if ! "$REPO_ROOT/tools/dosbox-run.sh" --exe "$EXE" --stdout "$LOG"; then
    echo "[sdl3-smoke] FAIL: dosbox-run.sh failed" >&2
    exit 2
fi
if [[ ! -s "$LOG" ]]; then
    echo "[sdl3-smoke] FAIL: capture is empty (exe may have crashed under DPMI)" >&2
    exit 2
fi

# Required substrings — each represents a code path that MUST survive in
# the DOS backend for the probe to be considered to have run at all.
# Audio device enumeration is intentionally NOT in this list because PR
# #15377's SoundBlaster detection has a known DOSBox-X interaction issue
# (DSP reset 0xAA byte mismatch); we still verify the audio driver is
# bootstrapped via AUDIO-DRIVER:.
REQUIRED=(
    "SDLTEST-BEGIN:"           # exe started, printf works under DPMI
    "AUDIO-DRIVERS: count="    # bootstrap audio driver list returned
    "AUDIO-DRIVER: 0 "         # at least one audio driver compiled in
    "VIDEO-DRIVER:"            # current video driver returned
    "VIDEO-DISPLAYS: count="   # SDL_GetDisplays returned
    "SDLTEST-END:"             # process reached the bottom of main()
)

# Informational substrings — present when the corresponding subsystem fully
# initialized. Their absence is reported but does not fail the gate; the
# operator should investigate via the build/sdl3-smoke/sdltest.log capture.
INFO=(
    "AUDIO-INIT: OK"           # SDL_Init(SDL_INIT_AUDIO) succeeded
    "DISPLAY: "                # at least one display enumerated
    "MODE: "                   # at least one fullscreen mode enumerated
)

failures=0
echo "[sdl3-smoke] verifying capture (build/sdl3-smoke/sdltest.log)..."
for needle in "${REQUIRED[@]}"; do
    if tr -d '\r' < "$LOG" | grep -F -q -- "$needle"; then
        echo "  PASS  contains:  $needle"
    else
        echo "  FAIL  missing:   $needle" >&2
        failures=$((failures+1))
    fi
done
for needle in "${INFO[@]}"; do
    if tr -d '\r' < "$LOG" | grep -F -q -- "$needle"; then
        echo "  INFO  present:   $needle"
    else
        echo "  WARN  absent:    $needle  (subsystem partial — see log)" >&2
    fi
done

# Phase 8 baseline — annotated capture written into tests/fixtures/ regardless
# of test outcome (a failed run is still useful provenance for debugging).
mkdir -p "$(dirname "$FIXTURE")"
{
    printf '# Phase 2d / Phase 8 baseline — SDL3 DOS-backend display+audio info\r\n'
    printf '# Source: build/sdl3-smoke/sdltest.exe (tests/sdl3-smoke/sdltest.c)\r\n'
    printf '# Host:   DOSBox-X under tools/dosbox-x.conf (cycles=fixed 40000,\r\n'
    printf '#         memsize=48, VESA modelist=compatible, SB16 IRQ 5 / DMA 1,5).\r\n'
    printf '#         NOT real ATI Mach64 / M64VBE. Phase 8 engineer compares\r\n'
    printf '#         this against the same probe run on g2k to flag VESA / SB\r\n'
    printf '#         backend drift between PR #15377 in DOSBox-X and real HW.\r\n'
    printf '# Regenerate: make sdl3-smoke\r\n'
    printf '\r\n'
    cat "$LOG"
} > "$FIXTURE"
echo "[sdl3-smoke] baseline written: $FIXTURE"

if [[ "$failures" -gt 0 ]]; then
    echo "[sdl3-smoke] FAIL: $failures required substring(s) missing" >&2
    echo "[sdl3-smoke] full capture:" >&2
    sed 's/^/    /' "$LOG" >&2
    exit 1
fi

echo "[sdl3-smoke] PASS: all required substrings present"
