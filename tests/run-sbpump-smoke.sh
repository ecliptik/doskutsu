#!/usr/bin/env bash
# run-sbpump-smoke.sh -- DOSBox-X correctness-only smoke for SBPUMP.EXE
# (T20, probe-engineer lane). The SETUP audio-test HARD-FREEZE A/B probe.
#
# Stages SBPUMP.EXE + CWSDPMI.EXE and runs all three cells (A1/B1/A2) each in
# its own DOSBox-X session (parity conf), then verifies each LOGS\<TAG>PROBE.LOG:
#   - the sbpump header (version + sha + cell)
#   - "init: SDL_Init(AUDIO) ok"
#   - "open: device live, stream bound"
#   - "prime: OK" (ring primed >0 -- the device is actually streaming)
#   - the cell-specific terminal marker:
#       A1/A2  -> "SURVIVED unserviced window" + "NO-WEDGE"
#       B1     -> "serviced window complete -- CLEAN" + >=1 heartbeat
#   - "done: closed cleanly, exit 0"
#
# REAL-HW-ONLY MECHANISM (per [[dosbox_not_proxy]]): DOSBox-X does NOT reproduce
# the real-SB16 wedge. Mode A runs CLEAN here and prints NO-WEDGE -- that is the
# EXPECTED DOSBox result and proves ONLY structural correctness (binary runs,
# device opens, ring primes, timer terminates, log parses, clean exit). The
# wedge confirmation is g2k-only (the T19 real-HW iter). A clean DOSBox A-cell
# must NOT be read as "no repro".
#
# BLASTER is set in the driver BAT (the SDL DOS audio backend needs it to find
# the SB16); the parity conf configures sb16/220/IRQ5/DMA1/HDMA5 to match g2k.
#
# Usage:
#   tests/run-sbpump-smoke.sh                # run all 3 cells
#   tests/run-sbpump-smoke.sh --keep-stage   # preserve temp dirs for debug
#
# Exit codes:
#   0  smoke passed (all 3 cells structurally correct)
#   1  one or more expected markers missing
#   2  invocation error (probe not built, dosbox-x absent)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/sbpump.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/sbpump-smoke"
BLASTER="A220 I5 D1 H5 P330 T6"   # g2k VIBRA profile (matches parity conf)

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
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make probes-sbpump\`" >&2
        exit 2
    fi
done
command -v dosbox-x >/dev/null 2>&1 || { echo "$(basename "$0"): dosbox-x not in PATH" >&2; exit 2; }

mkdir -p "$LOG_DIR"
rc=0

# run_cell <cell> <tag> <required>
#   required=1 -> a missing marker FAILS the gate (rc=1)
#   required=0 -> informational only (WARN); A2 is the broken-config (44100/
#     stereo) cell whose SB16 autoinit path is DOSBox-nondeterministic (it
#     completes ~3/4 runs, stopping at varying points -- classic
#     [[dosbox_not_proxy]] for this exact config). Its meaningful result is the
#     g2k wedge-vs-clean (T19), which DOSBox cannot produce. The probe's
#     structural correctness is fully proven by A1+B1, which share all code with
#     A2 (only freq/channels/frames differ). So A2 here is best-effort.
run_cell() {
    local cell="$1" tag="$2" required="${3:-1}"
    local stage; stage="$(mktemp -d -t sbpump-smoke-"$cell".XXXXXX)"
    cp "$EXE" "$stage/SBPUMP.EXE"
    cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
    printf "@ECHO OFF\r\nSET BLASTER=%s\r\nSET DOSKUTSU_LOG_TAG=%s\r\nSBPUMP.EXE %s\r\n" \
        "$BLASTER" "$tag" "$cell" > "$stage/RUN.BAT"

    echo "[sbpump-smoke] cell $cell (tag $tag): running under DOSBox-X..."
    timeout 90 dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -exit -nogui -nomenu >"$LOG_DIR/dosbox-$cell.out" 2>&1

    local sev="FAIL"; [[ "$required" == "0" ]] && sev="WARN"

    local log="$stage/LOGS/${tag}PROBE.LOG"
    if [[ ! -f "$log" ]]; then
        echo "[sbpump-smoke] $sev ($cell): LOGS\\${tag}PROBE.LOG not produced"
        [[ "$required" == "1" ]] && rc=1
        [[ "$KEEP_STAGE" == "0" ]] && rm -rf "$stage"
        return
    fi
    cp "$log" "$LOG_DIR/${tag}PROBE.LOG"

    local need=(
        "sbpump v1"
        "init: SDL_Init(AUDIO) ok"
        "open: device live, stream bound"
        "prime: OK"
        "done: closed cleanly, exit 0"
    )
    case "$cell" in
        A1|A2) need+=("SURVIVED unserviced window" "NO-WEDGE") ;;
        B1)    need+=("serviced window complete -- CLEAN" "B: heartbeat") ;;
    esac

    local miss=0 m
    for m in "${need[@]}"; do
        if ! grep -qF "$m" "$log"; then
            echo "[sbpump-smoke] $sev ($cell): missing marker: $m"
            miss=1; [[ "$required" == "1" ]] && rc=1
        fi
    done
    # Sentinel guard: a primed ring must be >0 (device actually streaming).
    if grep -qF "INVALID: ring did NOT prime" "$log"; then
        echo "[sbpump-smoke] $sev ($cell): ring did not prime (device not streaming)"
        [[ "$required" == "1" ]] && rc=1
    fi
    if [[ "$miss" == "0" ]]; then
        echo "[sbpump-smoke] OK   ($cell): all markers present"
    elif [[ "$required" == "0" ]]; then
        echo "[sbpump-smoke] note ($cell): incomplete under DOSBox-X (non-fatal; "
        echo "               this broken-config cell is DOSBox-nondeterministic, g2k-only signal)"
    fi
    [[ "$KEEP_STAGE" == "0" ]] && rm -rf "$stage"
}

run_cell A1 PA1 1   # game config, no service  -- REQUIRED
run_cell B1 PB1 1   # game config, serviced=fix -- REQUIRED
run_cell A2 PA2 0   # broken config, no service -- informational (DOSBox-flaky)

if [[ "$rc" == "0" ]]; then
    echo "[sbpump-smoke] PASS -- structural correctness only (NUMBERS/no-wedge are"
    echo "               DOSBox-fictitious; the real-SB16 wedge confirms on g2k, T19)."
else
    echo "[sbpump-smoke] FAIL -- see markers above; logs in $LOG_DIR"
fi
exit "$rc"
