#!/usr/bin/env bash
# run-s3crtc-smoke.sh -- DOSBox-X correctness-only smoke for S3CRTC.EXE
# (S3-2 task #15 direct-CRTC display-start probe, build-qa lane; recipe per
# probe-engineer). Sibling of run-s3blt-smoke.sh / run-crtcswap (Cirrus).
#
# Runs S3CRTC.EXE under DOSBox-X (svga_s3) -- NO FORCE needed (the probe has no
# ViRGE-detect gate; it finds an 8bpp LFB mode + programs the CRTC on any VBE
# card). Verifies S3CRTC.LOG has:
#   - "S3CRTC-BEGIN"               -- probe started
#   - "version: v"                 -- provenance line
#   - "[s3crtc] CR67="             -- the streams-mode check ran
#   - "[s3crtc GATE "              -- a verdict line emitted
#   - "S3CRTC-DONE"               -- clean bounded exit (no crash/hang)
#   - "[s3crtc] SELFTEST fills=OK" -- the fill oracle passed (host-RAM FB)
#
# CORRECTNESS-ONLY. DOSBox-X's svga_s3 is a Trio64, not a ViRGE: it accepts the
# CRTC writes (all 3 units latch) and reports streams=no, and there is NO operator
# eye -- so the verdict is LATCH_OK_OPERATOR_CONFIRMS_UNITS, the EXPECTED emulator
# outcome ([[dosbox_not_proxy]]). This gate proves the binary sets the LFB mode,
# fills the pages, programs + latch-reads the CRTC start under all 3 units, runs
# the streams check, and exits clean with a parseable log. The WORKING UNITS (the
# one that turns the screen BRIGHT = page 1) + the real streams verdict are a
# g2k-with-ViRGE measurement ONLY (operator eyeball).
#
# Usage:  tests/run-s3crtc-smoke.sh [--keep-stage]
# Exit:   0 passed   1 assertion failed   2 invocation error

set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="$REPO_ROOT/build/probes/s3crtc.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/s3crtc-smoke"

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
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make s3crtc\`" >&2
        exit 2
    fi
done
command -v dosbox-x >/dev/null 2>&1 || { echo "$(basename "$0"): dosbox-x not in PATH" >&2; exit 2; }

mkdir -p "$LOG_DIR"
stage="$(mktemp -d -t s3crtc-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT
cp "$EXE" "$stage/S3CRTC.EXE"; cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
printf '@ECHO OFF\r\nS3CRTC.EXE\r\n' > "$stage/RUN.BAT"

echo "[s3crtc-smoke] stage: $stage"
echo "[s3crtc-smoke] running S3CRTC.EXE under DOSBox-X (svga_s3); allowing 180s."
if ! timeout 180 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -silent -exit -nogui -nomenu >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[s3crtc-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi
if [[ ! -f "$stage/S3CRTC.LOG" ]]; then
    echo "[s3crtc-smoke] FAIL: S3CRTC.LOG not produced -- probe may have crashed" >&2
    exit 1
fi
cp "$stage/S3CRTC.LOG" "$LOG_DIR/S3CRTC.LOG"
echo "[s3crtc-smoke] captured $LOG_DIR/S3CRTC.LOG ($(wc -l < "$LOG_DIR/S3CRTC.LOG") lines)"

rc=0
OUT="$LOG_DIR/S3CRTC.LOG"
check() { if grep -qE "$1" "$OUT"; then echo "  PASS  $2"; else echo "  FAIL  $2 (absent)" >&2; rc=1; fi; }
check '^S3CRTC-BEGIN'                "S3CRTC-BEGIN present (probe started)"
check '^version: v'                 "version provenance line present"
check '^\[s3crtc\] CR67='            "streams-mode (CR67) check ran"
check '^\[s3crtc GATE '              "GATE verdict line emitted"
check '^S3CRTC-DONE'                 "S3CRTC-DONE present (clean exit)"
check '^\[s3crtc\] SELFTEST fills=OK' "fill oracle = OK (host-RAM FB read/write correct)"

if [[ "$rc" -eq 0 ]]; then
    echo "[s3crtc-smoke] PASS: BEGIN + version + CR67 + GATE + DONE + fills (correctness-only)."
    echo "[s3crtc-smoke] note: WORKING UNITS + real streams verdict are a g2k+ViRGE"
    echo "               measurement (operator eyeballs which unit flips to page 1)."
else
    echo "[s3crtc-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
