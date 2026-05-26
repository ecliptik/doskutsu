#!/usr/bin/env bash
# run-s3vram-smoke.sh -- DOSBox-X correctness-only smoke for S3VRAM.EXE
# (S3-2 de-risk task #10b probe, build-qa lane; recipe per probe-engineer).
#
# Stages S3VRAM.EXE + CWSDPMI.EXE in a temp dir, runs the probe under DOSBox-X
# (parity config, machine=svga_s3), then verifies S3VRAM.LOG has the structural
# markers of a clean run:
#   - "S3VRAM-BEGIN"               -- probe started + opened its log
#   - "version: v"                 -- provenance line (no stale-log ambiguity)
#   - "[s3vram] SELFTEST "         -- the verify-before-trust oracle ran
#   - "[s3vram GATE "              -- a verdict line emitted
#   - "S3VRAM-DONE"               -- clean bounded exit (no crash/hang)
#
# CORRECTNESS-ONLY. DOSBox-X's "LFB" is host RAM, so LFB_READ/WRITE == sysmem and
# the GATE verdict is emulator-fictitious (it will report GREEN with ~1.00x
# LFB_READ_PENALTY -- there is no VRAM read penalty on host RAM)
# ([[dosbox_not_proxy]]). This gate proves the binary runs, sets a VESA mode,
# maps the LFB, composites both scenarios, the sentinel + composite oracles pass,
# and it exits cleanly + writes a parseable log. The real VRAM-resident verdict
# (FRAME_VRAM vs FRAME_SYS, and the LFB_READ penalty) is a g2k-with-ViRGE
# measurement ONLY.
#
# Companion to tests/run-s3blt-smoke.sh / tests/run-membw-smoke.sh.
#
# Usage:
#   tests/run-s3vram-smoke.sh                # default
#   tests/run-s3vram-smoke.sh --keep-stage   # preserve temp dir for debug
#
# Exit codes:
#   0  smoke passed   1  assertion failed   2  invocation error

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/s3vram.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/s3vram-smoke"

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
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make s3vram\` (or probes-p27)" >&2
        exit 2
    fi
done

if ! command -v dosbox-x >/dev/null 2>&1; then
    echo "$(basename "$0"): dosbox-x not in PATH" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"
stage="$(mktemp -d -t s3vram-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT

cp "$EXE"     "$stage/S3VRAM.EXE"
cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
printf '@ECHO OFF\r\nS3VRAM.EXE\r\n' > "$stage/RUN.BAT"

echo "[s3vram-smoke] stage: $stage"
echo "[s3vram-smoke] running S3VRAM.EXE under DOSBox-X (parity conf, svga_s3)..."
echo "[s3vram-smoke] note: probe sets a VESA mode + maps the LFB; allowing 240s."

if ! timeout 240 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -silent -exit -nogui -nomenu \
        >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[s3vram-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi

if [[ ! -f "$stage/S3VRAM.LOG" ]]; then
    echo "[s3vram-smoke] FAIL: S3VRAM.LOG not produced -- probe may have crashed" >&2
    exit 1
fi

cp "$stage/S3VRAM.LOG" "$LOG_DIR/S3VRAM.LOG"
echo "[s3vram-smoke] captured $LOG_DIR/S3VRAM.LOG ($(wc -l < "$LOG_DIR/S3VRAM.LOG") lines)"

rc=0
OUT="$LOG_DIR/S3VRAM.LOG"
check() { if grep -qE "$1" "$OUT"; then echo "  PASS  $2"; else echo "  FAIL  $2 (absent)" >&2; rc=1; fi; }
check '^S3VRAM-BEGIN'            "S3VRAM-BEGIN present (probe started)"
check '^version: v'              "version provenance line present"
check '^\[s3vram\] SELFTEST '    "SELFTEST oracle ran (sentinel + composite)"
check '^\[s3vram GATE '          "GATE verdict line emitted"
check '^S3VRAM-DONE'             "S3VRAM-DONE present (clean exit)"

# The SELFTEST oracle must report OK under DOSBox-X (host-RAM FB is read/write
# correct); a FAIL there is a real probe bug, not an emulator artifact.
if grep -qE '^\[s3vram\] SELFTEST sentinel=OK composite=OK' "$OUT"; then
    echo "  PASS  SELFTEST oracle = OK (sentinel + composite verified)"
else
    echo "  FAIL  SELFTEST oracle not OK -- mapping/composite bug (not an emulator artifact)" >&2
    rc=1
fi

if [[ "$rc" -eq 0 ]]; then
    echo "[s3vram-smoke] PASS: BEGIN + version + SELFTEST + GATE + DONE (correctness-only)."
    echo "[s3vram-smoke] note: LFB_READ penalty + the VRAM-resident verdict are"
    echo "               g2k-with-ViRGE measurements (DOSBox LFB == host RAM)."
else
    echo "[s3vram-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
