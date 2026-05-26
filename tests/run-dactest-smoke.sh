#!/usr/bin/env bash
# run-dactest-smoke.sh -- DOSBox-X correctness-only smoke for DACTEST.EXE
# (S3-VIRGE campaign task #2 probe, build-qa lane; recipe per probe-engineer).
#
# Stages DACTEST.EXE + CWSDPMI.EXE in a temp dir, runs the probe under
# DOSBox-X (parity config), then verifies DACTEST.LOG has the structural
# markers of a clean run:
#   - "DACTEST-BEGIN"
#   - "DACTEST-DONE"               (clean bounded exit, no hang)
#   - "[dactest] T1_BARE ..."      (the SDL DetectVGA replica ran)
#   - "[dactest] T1B_ARMED ..."    (the armed-precondition variant ran)
#   - "[dactest] T2_RESET ..."     (the 0x3C8-reset variant ran)
#   - "[dactest] T3_READS ..."     (the consecutive-read observation ran)
#   - "[dactest] VERDICT=..."      (a verdict emitted)
#
# CORRECTNESS-ONLY -- the gate checks emit-structure (BEGIN/DONE + every test's
# marker + a verdict line), NOT which verdict value. Observed: DOSBox-X DOES
# model the standard VGA DAC 4-consecutive-read pixel-mask overlay -- the cold
# bare T1 round-trips (PASS), but once T1B arms the overlay with four reads the
# 0xA5 write is rejected (readback!=0xA5, FAIL) -> VERDICT=CONFIRMED under
# emulation. That is a valid PASS for this smoke (binary runs, exercises the
# ports, writes a parseable log, exits cleanly). Per [[dosbox_not_proxy]] the
# AUTHORITATIVE PASS/FAIL is still a g2k-with-S3-ViRGE measurement -- the real
# SDAC may arm/behave differently; this smoke only proves correctness.
#
# Usage:
#   tests/run-dactest-smoke.sh                # default
#   tests/run-dactest-smoke.sh --keep-stage   # preserve temp dir for debug
#
# Exit codes:
#   0  smoke passed   1  assertion failed   2  invocation error

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/dactest.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/dactest-smoke"

KEEP_STAGE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --keep-stage) KEEP_STAGE=1; shift ;;
        -h|--help) sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //; s/^#$//'; exit 0 ;;
        *) echo "$(basename "$0"): unknown arg: $1" >&2; exit 2 ;;
    esac
done

for f in "$EXE" "$CWSDPMI" "$CONF"; do
    if [[ ! -f "$f" ]]; then
        echo "$(basename "$0"): required file missing: $f" >&2
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make dactest\`" >&2
        exit 2
    fi
done

if ! command -v dosbox-x >/dev/null 2>&1; then
    echo "$(basename "$0"): dosbox-x not in PATH" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"
stage="$(mktemp -d -t dactest-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT

cp "$EXE"     "$stage/DACTEST.EXE"
cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
printf '@ECHO OFF\r\nDACTEST.EXE\r\n' > "$stage/RUN.BAT"

echo "[dactest-smoke] stage: $stage"
echo "[dactest-smoke] running DACTEST.EXE under DOSBox-X (parity conf)..."

if ! timeout 120 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -silent -exit -nogui -nomenu \
        >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[dactest-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi

if [[ ! -f "$stage/DACTEST.LOG" ]]; then
    echo "[dactest-smoke] FAIL: DACTEST.LOG not produced -- probe may have crashed" >&2
    exit 1
fi

cp "$stage/DACTEST.LOG" "$LOG_DIR/DACTEST.LOG"
echo "[dactest-smoke] captured $LOG_DIR/DACTEST.LOG ($(wc -l < "$LOG_DIR/DACTEST.LOG") lines)"

rc=0
OUT="$LOG_DIR/DACTEST.LOG"
check() {  # $1=regex  $2=label
    if grep -qE "$1" "$OUT"; then echo "  PASS  $2"; else echo "  FAIL  $2 (absent)" >&2; rc=1; fi
}
check '^DACTEST-BEGIN'              "DACTEST-BEGIN present"
check '^DACTEST-DONE'               "DACTEST-DONE present (clean exit)"
check '^\[dactest\] T1_BARE '       "T1 bare SDL-replica ran"
check '^\[dactest\] T1B_ARMED '     "T1B armed-precondition variant ran"
check '^\[dactest\] T2_RESET '      "T2 reset-variant ran"
check '^\[dactest\] T3_READS '      "T3 consecutive-read observation ran"
check '^\[dactest\] VERDICT='       "verdict line emitted"

if [[ "$rc" -eq 0 ]]; then
    echo "[dactest-smoke] PASS: BEGIN + DONE + T1/T2/T3 + verdict (correctness-only)."
    echo "[dactest-smoke] note: PASS/FAIL VERDICT is real-HW-dependent (DOSBox-X DAC"
    echo "               has no S3 SDAC overlay); the S3 confirmation is a g2k iter."
else
    echo "[dactest-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
