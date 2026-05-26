#!/usr/bin/env bash
# run-s3ckey-smoke.sh -- DOSBox-X correctness-only smoke for S3CKEY.EXE
# (S3-2 de-risk task #10a K4 colorkey-BLT probe, build-qa lane; recipe per
# probe-engineer). Sibling of run-s3blt-smoke.sh.
#
# Runs S3CKEY.EXE FORCE under DOSBox-X (svga_s3 / Trio64) so the high-risk path
# (CR53 new-MMIO enable + LFB/MMIO map + the opaque + COLORKEY BLT register
# sequences + the readback oracle + teardown) executes despite DOSBox emulating
# a Trio64 (not a ViRGE). Verifies S3CKEY.LOG has:
#   - "S3CKEY-BEGIN"               -- probe started
#   - "version: s3ckey"            -- provenance line (no stale-log ambiguity)
#   - "[s3ckey GATE_16x16 "        -- a K4 verdict line emitted
#   - "S3CKEY-DONE"               -- clean bounded exit (no crash/hang)
#
# CORRECTNESS-ONLY. DOSBox-X's svga_s3 does NOT model the ViRGE 2D engine, so the
# opaque + colorkey BLTs no-op -> the readback oracle reports UNVERIF and the
# verdict is RED_OPAQUE_BLT_VERIFY_FAIL (or CK_UNVERIF) -- the EXPECTED, watchdog-
# bounded, clean-exit emulator outcome ([[dosbox_not_proxy]]). This gate proves
# the binary runs the full path + the colorkey register-ladder + verify-oracle +
# teardown without crashing/hanging, and writes a parseable log with the K4
# tokens. The real K4 verdict (does colorkey keep v6's ~3.7x@16x16) is a g2k-
# with-ViRGE measurement ONLY.
#
# Usage:  tests/run-s3ckey-smoke.sh [--keep-stage]
# Exit:   0 passed   1 assertion failed   2 invocation error

set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="$REPO_ROOT/build/probes/s3ckey.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/s3ckey-smoke"

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
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make s3ckey\` (or probes-p26)" >&2
        exit 2
    fi
done
command -v dosbox-x >/dev/null 2>&1 || { echo "$(basename "$0"): dosbox-x not in PATH" >&2; exit 2; }

mkdir -p "$LOG_DIR"
stage="$(mktemp -d -t s3ckey-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT
cp "$EXE" "$stage/S3CKEY.EXE"; cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
# FORCE bypasses the ViRGE-detect gate so the path runs on DOSBox's Trio64.
printf '@ECHO OFF\r\nS3CKEY.EXE FORCE\r\n' > "$stage/RUN.BAT"

echo "[s3ckey-smoke] stage: $stage"
echo "[s3ckey-smoke] running S3CKEY.EXE FORCE under DOSBox-X (svga_s3); allowing 240s."
if ! timeout 240 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -silent -exit -nogui -nomenu >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[s3ckey-smoke] FAIL: dosbox-x exited non-zero (or timed out)" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi
if [[ ! -f "$stage/S3CKEY.LOG" ]]; then
    echo "[s3ckey-smoke] FAIL: S3CKEY.LOG not produced -- probe may have crashed" >&2
    exit 1
fi
cp "$stage/S3CKEY.LOG" "$LOG_DIR/S3CKEY.LOG"
echo "[s3ckey-smoke] captured $LOG_DIR/S3CKEY.LOG ($(wc -l < "$LOG_DIR/S3CKEY.LOG") lines)"

rc=0
OUT="$LOG_DIR/S3CKEY.LOG"
check() { if grep -qE "$1" "$OUT"; then echo "  PASS  $2"; else echo "  FAIL  $2 (absent)" >&2; rc=1; fi; }
check '^S3CKEY-BEGIN'                 "S3CKEY-BEGIN present (probe started)"
check '^version: s3ckey'             "version provenance line present"
check '^\[s3ckey GATE_16x16 '         "K4 GATE_16x16 verdict line emitted"
check '^S3CKEY-DONE'                  "S3CKEY-DONE present (clean exit)"

if [[ "$rc" -eq 0 ]]; then
    echo "[s3ckey-smoke] PASS: BEGIN + version + GATE + DONE (correctness-only)."
    echo "[s3ckey-smoke] note: colorkey/opaque BLTs UNVERIF under DOSBox (no ViRGE 2D"
    echo "               model); the real K4 ck/cpu verdict is a g2k+ViRGE iter."
else
    echo "[s3ckey-smoke] FAIL: one or more assertions failed -- see above." >&2
fi
exit "$rc"
