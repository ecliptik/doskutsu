#!/usr/bin/env bash
# run-s3blt-smoke.sh -- DOSBox-X correctness-only smoke for S3BLT.EXE
# (S3-VIRGE campaign P3 probe, build-qa lane; recipe per probe-engineer).
#
# Stages S3BLT.EXE + CWSDPMI.EXE in a temp dir, runs the probe under
# DOSBox-X (parity config, machine=svga_s3), then verifies S3BLT.LOG has
# the structural markers of a clean run:
#   - "S3BLT-BEGIN"     -- the probe started + opened its log
#   - "S3BLT-DONE"      -- the probe reached its clean-exit tag (no crash/hang)
#   - "[s3blt SUITE_DONE ... verdict=..." -- a verdict line emitted
#
# THIS IS A CORRECTNESS-ONLY GATE. DOSBox-X emulates an S3 Trio64 (svga_s3),
# NOT a ViRGE, and does not model the ViRGE 2D BitBLT engine
# ([[dosbox_not_proxy]]); the MB/s figures are emulator-fictitious. With FORCE
# (below) the probe runs the full path on the Trio64 and the ViRGE register
# sequence produces a NO-OP -> every BLT fails readback-verify -> blt_MBps=
# UNVERIF and the gate reports verdict=RED_BLT_VERIFY_FAIL (base ok via VGA-thru-
# MMIO, sequence is ViRGE-specific). That is the EXPECTED, watchdog-bounded,
# clean-exit outcome and a PASS for this smoke -- it proves the binary runs the
# detection + 4 MB-aperture map + MMIO + BLT-register + verify + teardown path
# without crashing or hanging. Real 2D numbers are a g2k-with-ViRGE iter only.
#
# Companion to tests/run-membw-smoke.sh / tests/run-blttile-smoke.sh.
#
# Usage:
#   tests/run-s3blt-smoke.sh                # default
#   tests/run-s3blt-smoke.sh --keep-stage   # preserve temp dir for debug
#
# Exit codes:
#   0  smoke passed (BEGIN + DONE + a SUITE_DONE verdict line)
#   1  one or more assertions failed
#   2  invocation error (probe binary not built, dosbox-x not installed)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/s3blt.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/s3blt-smoke"

KEEP_STAGE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --keep-stage) KEEP_STAGE=1; shift ;;
        -h|--help)
            sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //; s/^#$//'
            exit 0 ;;
        *) echo "$(basename "$0"): unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Preflight ---------------------------------------------------------------

for f in "$EXE" "$CWSDPMI" "$CONF"; do
    if [[ ! -f "$f" ]]; then
        echo "$(basename "$0"): required file missing: $f" >&2
        if [[ "$f" == "$EXE" ]]; then
            echo "  hint: build with \`make s3blt\` (or the probes-pNN target)" >&2
        fi
        exit 2
    fi
done

if ! command -v dosbox-x >/dev/null 2>&1; then
    echo "$(basename "$0"): dosbox-x not in PATH" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"

# Stage + run -------------------------------------------------------------

stage="$(mktemp -d -t s3blt-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT

cp "$EXE"     "$stage/S3BLT.EXE"
cp "$CWSDPMI" "$stage/CWSDPMI.EXE"

# S3BLT writes S3BLT.LOG directly via fopen (no shell redirect). CRLF the BAT.
# Pass FORCE so the smoke exercises the high-risk path (4 MB-aperture LFB map +
# MMIO map + nearptr + MMIO-decode gate + teardown) that the S3-ViRGE detection
# gate would otherwise skip on DOSBox-X's Trio64 emulation. The real g2k+ViRGE
# iter runs S3BLT.EXE with NO args (auto-detect). Under DOSBox-X the forced run
# is expected to reach RED_MMIO_NO_DECODE (no ViRGE 2D model) -- still a clean,
# bounded, watchdog-protected exit, which is exactly what this smoke verifies.
printf '@ECHO OFF\r\nS3BLT.EXE FORCE\r\n' > "$stage/RUN.BAT"

echo "[s3blt-smoke] stage: $stage"
echo "[s3blt-smoke] running S3BLT.EXE under DOSBox-X (parity conf, svga_s3)..."
echo "[s3blt-smoke] note: probe sets a graphics mode + maps LFB/MMIO; allowing 240s."

# S3BLT switches to a VESA 8bpp LFB graphics mode (if it detects S3) then
# restores text mode 0x03 -- DOSBox-X may log a harmless videomode-reset line.
if ! timeout 240 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" \
        -c "C:" \
        -c "CALL RUN.BAT" \
        -c "EXIT" -silent -exit -nogui -nomenu \
        >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[s3blt-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi

if [[ ! -f "$stage/S3BLT.LOG" ]]; then
    echo "[s3blt-smoke] FAIL: S3BLT.LOG not produced -- probe may have crashed" >&2
    exit 1
fi

cp "$stage/S3BLT.LOG" "$LOG_DIR/S3BLT.LOG"
echo "[s3blt-smoke] captured $LOG_DIR/S3BLT.LOG ($(wc -l < "$LOG_DIR/S3BLT.LOG") lines)"

# Assertions --------------------------------------------------------------

rc=0
OUT="$LOG_DIR/S3BLT.LOG"

if grep -q '^S3BLT-BEGIN' "$OUT"; then
    echo "  PASS  S3BLT-BEGIN present (probe started)"
else
    echo "  FAIL  S3BLT-BEGIN absent" >&2
    rc=1
fi

if grep -q '^S3BLT-DONE' "$OUT"; then
    echo "  PASS  S3BLT-DONE present (clean exit -- no crash/hang)"
else
    echo "  FAIL  S3BLT-DONE absent (probe crashed, hung, or watchdog never returned)" >&2
    rc=1
fi

# A verdict line must emit. We accept ANY verdict token -- under DOSBox-X the
# expected outcomes are SKIP_NOT_S3_VIRGE / RED_MMIO_NO_DECODE / ABORT_*; the
# real GREEN/RED gate is a g2k-with-ViRGE measurement, not an emulator one.
verdict_line="$(grep -E '^\[s3blt (SUITE_DONE|GATE_16x16)' "$OUT" | head -1)"
if [[ -n "$verdict_line" ]]; then
    echo "  PASS  verdict line emitted: ${verdict_line}"
else
    echo "  FAIL  no [s3blt SUITE_DONE / GATE_16x16] verdict line" >&2
    rc=1
fi

if [[ "$rc" -eq 0 ]]; then
    echo "[s3blt-smoke] PASS: BEGIN + DONE + verdict line (correctness-only)."
    echo "[s3blt-smoke] note: 2D-engine MB/s are NOT measured under DOSBox-X"
    echo "               (no ViRGE 2D model); real numbers are a g2k+ViRGE iter."
else
    echo "[s3blt-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
