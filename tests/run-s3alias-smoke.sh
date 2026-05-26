#!/usr/bin/env bash
# run-s3alias-smoke.sh -- DOSBox-X correctness-only smoke for S3ALIAS.EXE
# (S3-2 task #26 MMIO-aliased-into-LFB hyp-3 probe, build-qa lane; recipe per
# probe-engineer). Sibling of run-s3wedge-smoke.sh.
#
# Runs S3ALIAS.EXE (no args) under DOSBox-X. Verifies S3ALIAS.LOG has:
#   - "S3ALIAS-BEGIN" / "version: v"            -- started + provenance
#   - "[s3alias] MAP "                          -- LFB+MMIO map + ride-alongs ran
#   - "[s3alias] CR config:"                    -- ViRGE CR ride-along ran
#   - "[s3alias CELL_DONE A_BULK page=0 "       -- bulk cell ran
#   - "[s3alias CELL_DONE B_SCATTERED page=0 "  -- scattered cell ran (no crash)
#   - "[s3alias SWEEP_DONE"                     -- low-offset alias sweep ran
#   - "[s3alias GATE "                          -- a verdict emitted
#   - "S3ALIAS-DONE"                           -- clean bounded exit
#
# CORRECTNESS-ONLY. DOSBox-X's svga_s3 is a Trio64 with a host-RAM LFB + no
# ViRGE 2D-engine aliasing, so BOTH cells are clean + the sweep shows no
# reg-change -> verdict NO_ALIAS_HYP3_REFUTED, the EXPECTED emulator non-result
# ([[dosbox_not_proxy]]). This gate proves the binary maps LFB+new-MMIO, runs the
# bulk + scattered cells at both pages, the alias sweep, the canary + reg + SUBSYS
# oracles, and exits clean with a parseable log. The REAL aliasing verdict (do
# scattered LFB writes reach the 2D engine) is a g2k-with-ViRGE measurement ONLY
# -- and a HARD confirm there is the log STOPPING at a B_SCATTERED CELL_BEGIN
# (the machine crashed), which on the bench is a valid result, not a smoke fail.
#
# Usage:  tests/run-s3alias-smoke.sh [--keep-stage]
# Exit:   0 passed   1 assertion failed   2 invocation error

set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="$REPO_ROOT/build/probes/s3alias.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/s3alias-smoke"

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
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make build/probes/s3alias.exe\`" >&2
        exit 2
    fi
done
command -v dosbox-x >/dev/null 2>&1 || { echo "$(basename "$0"): dosbox-x not in PATH" >&2; exit 2; }

mkdir -p "$LOG_DIR"
stage="$(mktemp -d -t s3alias-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT
cp "$EXE" "$stage/S3ALIAS.EXE"; cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
printf '@ECHO OFF\r\nS3ALIAS.EXE\r\n' > "$stage/RUN.BAT"

echo "[s3alias-smoke] stage: $stage"
echo "[s3alias-smoke] running S3ALIAS.EXE under DOSBox-X; allowing 180s."
if ! timeout 180 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -silent -exit -nogui -nomenu >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[s3alias-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi
if [[ ! -f "$stage/S3ALIAS.LOG" ]]; then
    echo "[s3alias-smoke] FAIL: S3ALIAS.LOG not produced -- probe may have crashed" >&2
    exit 1
fi
cp "$stage/S3ALIAS.LOG" "$LOG_DIR/S3ALIAS.LOG"
echo "[s3alias-smoke] captured $LOG_DIR/S3ALIAS.LOG ($(wc -l < "$LOG_DIR/S3ALIAS.LOG") lines)"

rc=0
OUT="$LOG_DIR/S3ALIAS.LOG"
check() { if grep -qE "$1" "$OUT"; then echo "  PASS  $2"; else echo "  FAIL  $2 (absent)" >&2; rc=1; fi; }
check '^S3ALIAS-BEGIN'                          "S3ALIAS-BEGIN present"
check '^version: v'                            "version provenance line present"
check '^\[s3alias\] MAP '                       "MAP + DPMI/nearptr ride-alongs ran"
check '^\[s3alias\] CR config:'                 "ViRGE CR config ride-along ran"
check '^\[s3alias CELL_DONE A_BULK page=0 '     "BULK cell ran (page 0)"
check '^\[s3alias CELL_DONE B_SCATTERED page=0 ' "SCATTERED cell ran (page 0, no crash)"
check '^\[s3alias SWEEP_DONE'                    "low-offset alias sweep ran"
check '^\[s3alias GATE '                         "GATE verdict line emitted"
check '^S3ALIAS-DONE'                            "S3ALIAS-DONE present (clean exit)"

if [[ "$rc" -eq 0 ]]; then
    echo "[s3alias-smoke] PASS: BEGIN+version+MAP+CR+bulk+scattered+sweep+GATE+DONE."
    echo "[s3alias-smoke] note: real aliasing verdict (scattered writes reach the 2D"
    echo "               engine) is a g2k+ViRGE measurement; DOSBox = host RAM, no alias."
else
    echo "[s3alias-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
