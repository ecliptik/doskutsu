#!/usr/bin/env bash
# run-membw-smoke.sh -- DOSBox-X correctness-only smoke for MEMBW.EXE
# (486-class campaign Phase-1 probe, build-qa lane).
#
# Stages MEMBW.EXE + CWSDPMI.EXE in a temp dir, runs the probe under
# DOSBox-X (parity config), then verifies MEMBW.OUT has:
#   - the "[membw] done" clean-exit tag
#   - a "[membw] test=LFB_" line (the VESA LFB-write section emitted)
#   - "[membw] LFB_sentinel=0xA5/0x5A" -- the LFB-mapping correctness
#     witness; any other value means the map+nearptr+modeset path failed
#
# NUMBERS ARE EMULATOR-FICTITIOUS. DOSBox-X's LFB is host memory; the
# MB/s figures do not match real HW per [[dosbox_not_proxy]]. This smoke
# gates emit-structure + the sentinel only. Real-HW bandwidth is a g2k /
# campaign-machine measurement.
#
# Companion to tests/run-hwinv-smoke.sh; recipe per probe-engineer's
# task #49 membw hand-off.
#
# Usage:
#   tests/run-membw-smoke.sh                # default
#   tests/run-membw-smoke.sh --keep-stage   # preserve temp dir for debug
#
# Exit codes:
#   0  smoke passed (clean exit tag + LFB section + sentinel)
#   1  one or more assertions failed
#   2  invocation error (probe binary not built, dosbox-x not installed)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/membw.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/membw-smoke"

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
            echo "  hint: build with \`make membw\`" >&2
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

stage="$(mktemp -d -t membw-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT

cp "$EXE"     "$stage/MEMBW.EXE"
cp "$CWSDPMI" "$stage/CWSDPMI.EXE"

# Minimal driver BAT -- just runs the probe. membw writes MEMBW.OUT
# directly via fopen (no shell redirect needed). CRLF the BAT for DOS.
printf '@ECHO OFF\r\nMEMBW.EXE\r\n' > "$stage/RUN.BAT"

echo "[membw-smoke] stage: $stage"
echo "[membw-smoke] running MEMBW.EXE under DOSBox-X (parity conf)..."
echo "[membw-smoke] note: the LFB section adds runtime; allowing up to 240s."

# membw switches video mode (VESA LFB graphics mode) then restores text
# mode 0x03 -- DOSBox-X logs a harmless "Unhandled videomode B9 on reset"
# mouse-driver line; that is not a failure.
if ! timeout 240 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" \
        -c "C:" \
        -c "CALL RUN.BAT" \
        -c "EXIT" -silent -exit -nogui -nomenu \
        >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[membw-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi

if [[ ! -f "$stage/MEMBW.OUT" ]]; then
    echo "[membw-smoke] FAIL: MEMBW.OUT not produced -- probe may have crashed" >&2
    exit 1
fi

cp "$stage/MEMBW.OUT" "$LOG_DIR/MEMBW.OUT"
echo "[membw-smoke] captured $LOG_DIR/MEMBW.OUT ($(wc -l < "$LOG_DIR/MEMBW.OUT") lines)"

# Assertions --------------------------------------------------------------

rc=0
OUT="$LOG_DIR/MEMBW.OUT"

if grep -q '^\[membw\] done' "$OUT"; then
    echo "  PASS  [membw] done -- clean exit tag present"
else
    echo "  FAIL  [membw] done -- clean exit tag absent (probe did not finish)" >&2
    rc=1
fi

if grep -qE '^\[membw\] test=LFB_' "$OUT"; then
    echo "  PASS  [membw] test=LFB_ -- VESA LFB-write section emitted"
else
    echo "  FAIL  [membw] test=LFB_ line absent -- LFB section did not emit" >&2
    rc=1
fi

# The LFB sentinel is the correctness witness: the probe writes two
# sentinel bytes through the linear-framebuffer mapping and reads them
# back. A correct map reports 0xA5/0x5A; any other value means the
# map+nearptr+modeset path failed and the LFB MB/s figures are meaningless.
if grep -qE '^\[membw\] LFB_sentinel=0xA5/0x5A' "$OUT"; then
    echo "  PASS  [membw] LFB_sentinel=0xA5/0x5A -- LFB mapping verified"
else
    sent_line="$(grep -E '^\[membw\] LFB_sentinel=' "$OUT" | head -1)"
    echo "  FAIL  LFB_sentinel wrong/absent -- got: ${sent_line:-no LFB_sentinel line}" >&2
    rc=1
fi

# S3-VIRGE campaign Lever-1 decider (size sweep): the LFB_WRITE_BOUND verdict
# line must emit with a recognized token. This is a derived-metric emit-check
# (probe_authoring_discipline) -- it gates STRUCTURE only; the BANDWIDTH vs
# OVERHEAD VALUE is emulator-fictitious here (DOSBox-X LFB = host memory, so a
# saturated curve / >100 GB/s asymptote is expected and the probe self-flags
# it). INDETERMINATE is a valid emitted token (e.g. DOSBox-X reports no usable
# VRAM size, or the host LFB does not show a per-byte slope).
bound_line="$(grep -E '^\[membw\] LFB_WRITE_BOUND=' "$OUT" | head -1)"
if [[ "$bound_line" =~ ^\[membw\]\ LFB_WRITE_BOUND=(BANDWIDTH|OVERHEAD|INDETERMINATE) ]]; then
    echo "  PASS  LFB_WRITE_BOUND verdict emitted: ${bound_line#*LFB_WRITE_BOUND=}"
else
    echo "  FAIL  LFB_WRITE_BOUND verdict absent/invalid -- got: ${bound_line:-no LFB_WRITE_BOUND line}" >&2
    rc=1
fi

if [[ "$rc" -eq 0 ]]; then
    echo "[membw-smoke] PASS: clean exit + LFB section + sentinel verified."
    echo "[membw-smoke] note: MB/s figures are emulator-fictitious (DOSBox-X LFB"
    echo "               = host memory); real bandwidth is a campaign-machine value."
else
    echo "[membw-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
