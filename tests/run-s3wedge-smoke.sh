#!/usr/bin/env bash
# run-s3wedge-smoke.sh -- DOSBox-X correctness-only smoke for S3WEDGE.EXE
# (S3-2 task #16 Stage-1 concurrent-contention wedge probe, build-qa lane;
# recipe per probe-engineer). Sibling of run-s3blt-smoke.sh.
#
# Runs S3WEDGE.EXE SMOKE (128-frame fast mode, ~2 sample windows) under DOSBox-X.
# The full real-HW run (NO arg) does 4000 frames/cell; SMOKE caps to 128 so the
# emulator correctness check is fast. Verifies S3WEDGE.LOG has:
#   - "S3WEDGE-BEGIN" / "version: v"          -- started + provenance
#   - "[s3wedge SUITE_BEGIN "                  -- setup reached the cells
#   - "[s3wedge PROGRESS "                     -- the RDTSC-vs-0040:006C detector ran
#   - "[s3wedge CELL_DONE SCATTERED " + "...SUSTAINED " -- both cells ran
#   - "[s3wedge GATE "                         -- a verdict emitted
#   - "PROBE-DONE" / "S3WEDGE-DONE"           -- clean bounded exit
#
# CORRECTNESS-ONLY. DOSBox-X's LFB is host RAM (no real bus contention) + the
# parity conf has no SB16, so audio_concurrent=NO and NEITHER cell wedges ->
# verdict CLEAN_SALVAGEABLE, the EXPECTED emulator outcome ([[dosbox_not_proxy]]).
# This gate proves the binary maps the LFB, runs both write-pattern cells + the
# WORD page-flip + the wedge-detector, exits clean, and writes a parseable log.
# The REAL wedge verdict (does the scattered VRAM-write pattern flatline the BIOS
# tick = IRQ-delivery-dead) is a g2k-with-ViRGE + SB16-active measurement ONLY.
#
# Usage:  tests/run-s3wedge-smoke.sh [--keep-stage]
# Exit:   0 passed   1 assertion failed   2 invocation error

set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="$REPO_ROOT/build/probes/s3wedge.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/s3wedge-smoke"

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
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make build/probes/s3wedge.exe\`" >&2
        exit 2
    fi
done
command -v dosbox-x >/dev/null 2>&1 || { echo "$(basename "$0"): dosbox-x not in PATH" >&2; exit 2; }

mkdir -p "$LOG_DIR"
stage="$(mktemp -d -t s3wedge-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT
cp "$EXE" "$stage/S3WEDGE.EXE"; cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
# SMOKE arg = 128-frame fast mode (the real-HW BAT runs S3WEDGE.EXE with NO arg).
printf '@ECHO OFF\r\nS3WEDGE.EXE SMOKE\r\n' > "$stage/RUN.BAT"

echo "[s3wedge-smoke] stage: $stage"
echo "[s3wedge-smoke] running S3WEDGE.EXE SMOKE under DOSBox-X; allowing 240s."
if ! timeout 240 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -silent -exit -nogui -nomenu >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[s3wedge-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi
if [[ ! -f "$stage/S3WEDGE.LOG" ]]; then
    echo "[s3wedge-smoke] FAIL: S3WEDGE.LOG not produced -- probe may have crashed" >&2
    exit 1
fi
cp "$stage/S3WEDGE.LOG" "$LOG_DIR/S3WEDGE.LOG"
echo "[s3wedge-smoke] captured $LOG_DIR/S3WEDGE.LOG ($(wc -l < "$LOG_DIR/S3WEDGE.LOG") lines)"

rc=0
OUT="$LOG_DIR/S3WEDGE.LOG"
check() { if grep -qE "$1" "$OUT"; then echo "  PASS  $2"; else echo "  FAIL  $2 (absent)" >&2; rc=1; fi; }
check '^S3WEDGE-BEGIN'                       "S3WEDGE-BEGIN present"
check '^version: v'                          "version provenance line present"
check '^\[s3wedge SUITE_BEGIN '              "SUITE_BEGIN (setup reached the cells)"
check '^\[s3wedge PROGRESS '                 "PROGRESS (RDTSC-vs-0040:006C wedge detector ran)"
check '^\[s3wedge CELL_DONE SCATTERED '      "SCATTERED cell ran to a verdict"
check '^\[s3wedge CELL_DONE SUSTAINED '      "SUSTAINED cell ran to a verdict"
check '^\[s3wedge GATE '                     "GATE verdict line emitted"
check '^PROBE-DONE'                          "PROBE-DONE marker present"
check '^S3WEDGE-DONE'                        "S3WEDGE-DONE present (clean exit)"

if [[ "$rc" -eq 0 ]]; then
    echo "[s3wedge-smoke] PASS: BEGIN+version+SUITE+PROGRESS+both cells+GATE+PROBE-DONE+DONE."
    echo "[s3wedge-smoke] note: real wedge verdict (BIOS-tick flatline = IRQ-dead) is a"
    echo "               g2k+ViRGE+SB16 measurement; DOSBox = host RAM, no contention."
else
    echo "[s3wedge-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
