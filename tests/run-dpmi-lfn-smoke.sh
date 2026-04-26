#!/usr/bin/env bash
# run-dpmi-lfn-smoke.sh — Phase 8 DPMI LFN propagation probe runner.
#
# Runs build/dpmi-lfn-smoke/probe.exe under DOSBox-X (parity config, lfn=true)
# with paired test fixtures (WAVETABL.DAT + wavetable.dat) staged into C:\,
# captures stdout, asserts the three PROBE: lines passed.
#
# This is the DEV-HOST baseline only — it confirms the probe code is correct.
# The actual question (does CWSDPMI propagate LFN INT 21h calls under real
# MS-DOS 6.22?) is answered only by running probe.exe on g2k. See
# tests/dpmi-lfn-smoke/README.md § "How to run on g2k real hardware".
#
# Exit codes:
#   0  — all three PROBE: lines report PASS
#   1  — one or more PROBE: lines missing or report FAIL
#   2  — invocation error (exe missing, dosbox-run.sh failed, etc.)

set -uo pipefail
# NOTE: deliberately not using `set -e` because we want to keep running through
# partial failures so the operator sees every check at once.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/dpmi-lfn-smoke/probe.exe"
LOG="$REPO_ROOT/build/dpmi-lfn-smoke/probe.log"
FIXTURE_DIR="$REPO_ROOT/tests/dpmi-lfn-smoke"

if [[ ! -f "$EXE" ]]; then
    echo "run-dpmi-lfn-smoke.sh: $EXE missing — build with \`make dpmi-lfn-smoke\`" >&2
    exit 2
fi
for fx in wavetabl.dat wavetable.dat; do
    if [[ ! -f "$FIXTURE_DIR/$fx" ]]; then
        echo "run-dpmi-lfn-smoke.sh: fixture $FIXTURE_DIR/$fx missing" >&2
        exit 2
    fi
done

echo "[dpmi-lfn-smoke] running $EXE under parity DOSBox-X config (lfn=true baseline)"
if ! "$REPO_ROOT/tools/dosbox-run.sh" \
        --exe "$EXE" \
        --include "$FIXTURE_DIR/wavetabl.dat" \
        --include "$FIXTURE_DIR/wavetable.dat" \
        --stdout "$LOG"; then
    echo "[dpmi-lfn-smoke] FAIL: dosbox-run.sh failed" >&2
    exit 2
fi

if [[ ! -s "$LOG" ]]; then
    echo "[dpmi-lfn-smoke] FAIL: empty probe.log — exe likely crashed" >&2
    exit 2
fi

echo "[dpmi-lfn-smoke] capture follows ↓↓↓"
sed 's/^/    /' "$LOG"
echo "[dpmi-lfn-smoke] capture above ↑↑↑"

# Required PROBE lines and what we expect to see for each one.
fail=0
check() {
    local label="$1"
    local pattern="$2"
    if grep -qE "$pattern" "$LOG"; then
        echo "[dpmi-lfn-smoke] OK   $label"
    else
        echo "[dpmi-lfn-smoke] MISS $label  (no line matching: $pattern)"
        fail=1
    fi
}

check "version banner"          '^PROBE: dpmi-lfn-smoke v1'
check "short_baseline PASS"     '^PROBE: short_baseline PASS '
check "long_libc_lfn PASS"      '^PROBE: long_libc_lfn PASS '
check "long_int21_716c PASS"    '^PROBE: long_int21_716c PASS '
check "done line"               '^PROBE: done fails=0[[:space:]]*$'

if [[ "$fail" == "0" ]]; then
    echo "[dpmi-lfn-smoke] PASS — DOSBox-X baseline green; probe code is correct."
    echo "[dpmi-lfn-smoke] Next: run probe.exe on g2k real HW per tests/dpmi-lfn-smoke/README.md."
    exit 0
fi

echo "[dpmi-lfn-smoke] FAIL — one or more PROBE: assertions missed." >&2
exit 1
