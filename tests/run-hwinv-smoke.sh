#!/usr/bin/env bash
# run-hwinv-smoke.sh -- DOSBox-X correctness-only smoke for HWINV.EXE
# (wave-41 task #10, probe-engineer lane).
#
# Stages HWINV.EXE + CWSDPMI.EXE + HWINV.BAT in a temp dir, runs the
# probe under DOSBox-X (parity config), then verifies HWINV.LOG has:
#   - Every section's BEGIN sentinel ([HWINV-<CAT>-BEGIN])
#   - Every section's DONE sentinel ([HWINV-<CAT>-DONE])
#   - The final [HWINV-EXIT_OK] marker
#   - DOSBOX_DETECTED=<0|1> emit line (build-qa essential per
#     WAVE-41-HW-INVENTORY-PROBE-PLAN.md sec. 5.2)
#
# NUMBERS ARE EMULATOR-FICTITIOUS. DOSBox-X's CPUID / DPMI / PCI /
# Cirrus emulation does not match real-HW per [[dosbox_not_proxy]].
# This smoke gates emit-structure correctness only. Real-HW gate runs
# on g2k as part of the wave-41-hwinv iter tarball (task #12).
#
# Companion: build-qa's 86Box parity-smoke target (task #11).
#
# Usage:
#   tests/run-hwinv-smoke.sh                # default
#   tests/run-hwinv-smoke.sh --keep-stage   # preserve temp dir for debug
#
# Exit codes:
#   0  smoke passed (all 9 sections + EXIT_OK emit)
#   1  one or more sentinels missing
#   2  invocation error (probe binary not built, dosbox-x not installed)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/hwinv.exe"
BAT="$REPO_ROOT/tests/probes/hwinv.bat"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/hwinv-smoke"

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

for f in "$EXE" "$BAT" "$CWSDPMI" "$CONF"; do
    if [[ ! -f "$f" ]]; then
        echo "$(basename "$0"): required file missing: $f" >&2
        if [[ "$f" == "$EXE" ]]; then
            echo "  hint: build with \`make hwinv\`" >&2
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

stage="$(mktemp -d -t hwinv-smoke.XXXXXX)"
trap 'if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi' EXIT

cp "$EXE"     "$stage/HWINV.EXE"
cp "$CWSDPMI" "$stage/CWSDPMI.EXE"

# Use a minimal driver BAT (not the operator-facing hwinv.bat which has
# TYPE/FIND post-processing -- those add noise under DOSBox-X). The probe
# itself produces HWINV.LOG.
#
# Set DOSKUTSU_ENVIRONMENT=dosbox-x per the WAVE-41-TRI-ENV-CORRELATION-
# PLAN.md sec. 4.3 belt-and-suspenders pattern: AND-gated auto-detect
# may false-negative on DOSBox-X builds where the hypervisor leaf is not
# exposed in conf; the env-var override forces the correct classification.
# Real-HW does NOT set this var; operator's DOSBox-X conf (or this BAT)
# does. Same pattern applies for 86Box via tools/86box-x.conf.
cat > "$stage/RUN.BAT" <<'EOF_BAT'
@ECHO OFF
SET DOSKUTSU_ENVIRONMENT=dosbox-x
HWINV.EXE
EOF_BAT
# CRLF the BAT.
sed -i 's/$/\r/' "$stage/RUN.BAT"

echo "[hwinv-smoke] stage: $stage"
echo "[hwinv-smoke] running HWINV.EXE under DOSBox-X (parity conf)..."

if ! dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" \
        -c "C:" \
        -c "CALL RUN.BAT" \
        -c "EXIT" -silent -exit -nogui -nomenu \
        >"$LOG_DIR/dosbox.out" 2>&1; then
    echo "[hwinv-smoke] FAIL: dosbox-x exited non-zero" >&2
    sed 's/^/    /' "$LOG_DIR/dosbox.out" | tail -20
    exit 1
fi

if [[ ! -f "$stage/HWINV.LOG" ]]; then
    echo "[hwinv-smoke] FAIL: HWINV.LOG not produced -- probe may have crashed" >&2
    exit 1
fi

cp "$stage/HWINV.LOG" "$LOG_DIR/HWINV.LOG"
echo "[hwinv-smoke] captured $LOG_DIR/HWINV.LOG ($(wc -l < "$LOG_DIR/HWINV.LOG") lines)"

# Sentinel verification ---------------------------------------------------

SECTIONS=(ENV CPU MEM VID AUD DSK IRQ PORT PCI)
rc=0

for sec in "${SECTIONS[@]}"; do
    if ! grep -q "^\[HWINV-${sec}-BEGIN\]" "$LOG_DIR/HWINV.LOG"; then
        echo "[hwinv-smoke] FAIL: missing [HWINV-${sec}-BEGIN] sentinel"
        rc=1
    fi
    if ! grep -q "^\[HWINV-${sec}-DONE\]" "$LOG_DIR/HWINV.LOG"; then
        echo "[hwinv-smoke] FAIL: missing [HWINV-${sec}-DONE] sentinel"
        rc=1
    fi
done

if ! grep -q "^\[HWINV-EXIT_OK\]" "$LOG_DIR/HWINV.LOG"; then
    echo "[hwinv-smoke] FAIL: missing [HWINV-EXIT_OK] final marker"
    rc=1
fi

# DOSBOX_DETECTED line MUST emit (value-agnostic; build-qa keys on absence
# to verify 86Box runs were not silently routed to DOSBox-X). Note: v2
# AND-gate may emit =0 under DOSBox-X if hypervisor leaf is not exposed
# in conf; the ENVIRONMENT= line is the essential 3-state classifier.
if ! grep -q "DOSBOX_DETECTED=" "$LOG_DIR/HWINV.LOG"; then
    echo "[hwinv-smoke] FAIL: missing DOSBOX_DETECTED= emit (build-qa essential)"
    rc=1
fi

# ENVIRONMENT= line MUST emit AND under DOSBox-X smoke MUST report =dosbox-x
# (the RUN.BAT sets DOSKUTSU_ENVIRONMENT=dosbox-x as the belt-and-suspenders
# override per the tri-env plan sec. 4.3). False classification would
# corrupt the RUNMANIFEST schema's environment field.
if ! grep -q "^\[HWINV-ENV\] ENVIRONMENT=dosbox-x" "$LOG_DIR/HWINV.LOG"; then
    echo "[hwinv-smoke] FAIL: ENVIRONMENT line missing or != dosbox-x (override not applied?)"
    rc=1
fi

# RUNMANIFEST block: required by schema v1. Check the BEGIN+END sentinels
# AND every required key is present per docs/internal/WAVE-41-TRI-ENV-
# CORRELATION-PLAN.md sec. 4.2.
if ! grep -q "^\[RUNMANIFEST-BEGIN\]" "$LOG_DIR/HWINV.LOG"; then
    echo "[hwinv-smoke] FAIL: missing [RUNMANIFEST-BEGIN] sentinel"
    rc=1
fi
if ! grep -q "^\[RUNMANIFEST-END\]" "$LOG_DIR/HWINV.LOG"; then
    echo "[hwinv-smoke] FAIL: missing [RUNMANIFEST-END] sentinel"
    rc=1
fi
for key in schema_version environment binary_sha12 wave_tag scene env_block_sha started_utc \
           duration_s exit_code fps_p50 fps_p95 audio_vital_status regime \
           banner_required_hit banner_required_total banner_forbidden_hit \
           banner_optional_hit critical_count warn_count; do
    if ! grep -q "^${key}=" "$LOG_DIR/HWINV.LOG"; then
        echo "[hwinv-smoke] FAIL: RUNMANIFEST missing key '${key}'"
        rc=1
    fi
done

# Section-timeout indicator: any STEP-TIMEOUT is informational (section bailed
# early under section budget); not a smoke failure but worth flagging.
if grep -q "STEP-TIMEOUT" "$LOG_DIR/HWINV.LOG"; then
    echo "[hwinv-smoke] WARN: at least one section hit its 500 ms watchdog under DOSBox-X:"
    grep "STEP-TIMEOUT" "$LOG_DIR/HWINV.LOG" | sed 's/^/  /'
fi

# Summary -----------------------------------------------------------------

echo
if [[ "$rc" == "0" ]]; then
    echo "[hwinv-smoke] PASS: all 9 sections + EXIT_OK present; DOSBOX_DETECTED emit OK"
    echo "[hwinv-smoke] log: $LOG_DIR/HWINV.LOG"
    grep "DOSBOX_DETECTED=" "$LOG_DIR/HWINV.LOG" | sed 's/^/  /'
else
    echo "[hwinv-smoke] FAIL: see above; full log at $LOG_DIR/HWINV.LOG"
fi

exit $rc
