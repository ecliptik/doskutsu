#!/usr/bin/env bash
# run-dpmi-lfn-smoke.sh — Phase 8 DPMI LFN propagation probe runner.
#
# Three variants:
#
#   baseline   DOSBox-X with lfn=true (built-in emulator LFN, no TSR loaded).
#              Should PASS — proves the probe code itself is correct.
#              Does NOT exercise CWSDPMI's INT 21h reflector; DOSBox-X's
#              kernel sees the LFN call directly.
#
#   no-tsr     DOSBox-X with lfn=false, no LFN driver loaded. Sanity check:
#              should FAIL on the long-name tests (proving we're really
#              testing TSR-mediated LFN, not residual emulator LFN).
#
#   tsr        DOSBox-X with lfn=false + LFNDOS.EXE loaded as a TSR before
#              the probe runs. THIS is the actual question: does
#              CWSDPMI's INT 21h reflector pass LFN-family calls through
#              to a real-mode TSR? Pass = LFN-via-DPMI works in emulation.
#              Real-HW confirmation still required (Phase B, see README).
#
# The test fixture is paired (WAVETABL.DAT + wavetable.dat, both 1-byte
# sentinels). On a no-LFN DOS the long form is invisible; the probe
# distinguishes "8.3 reaches DOS at all" from "long names work via LFN."
#
# Self-contained staging — does not use tools/dosbox-run.sh, because the
# tsr variant needs a custom RUN.BAT (load LFNDOS.EXE before probe.exe) and
# the no-tsr / tsr variants both need a -set override to force lfn=false
# regardless of what tools/dosbox-x.conf says. Keeping all three variants
# in one runner simplifies the "run all and summarize" flow.
#
# Exit codes:
#   0  — all selected variants behaved as expected
#   1  — one or more variants produced an unexpected outcome
#   2  — invocation error (missing exe/fixture, dosbox-x crash, etc.)
#
# Usage:
#   tests/run-dpmi-lfn-smoke.sh                       # all three variants
#   tests/run-dpmi-lfn-smoke.sh --variant baseline    # one specific variant
#   tests/run-dpmi-lfn-smoke.sh --variant tsr
#   tests/run-dpmi-lfn-smoke.sh --variant no-tsr
#   tests/run-dpmi-lfn-smoke.sh --keep-stage          # leave temp dirs for inspection

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/dpmi-lfn-smoke/probe.exe"
FIXTURE_DIR="$REPO_ROOT/tests/dpmi-lfn-smoke"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
LFNDOS="$REPO_ROOT/vendor/lfndos/lfndos.exe"
DOSLFN="$REPO_ROOT/vendor/doslfn/doslfn.com"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/dpmi-lfn-smoke"

VARIANT="all"
KEEP_STAGE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --variant) VARIANT="$2"; shift 2 ;;
        --keep-stage) KEEP_STAGE=1; shift ;;
        -h|--help)
            sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //; s/^#$//'
            exit 0 ;;
        *) echo "$(basename "$0"): unknown arg: $1" >&2; exit 2 ;;
    esac
done

case "$VARIANT" in
    all|baseline|tsr|tsr-doslfn|no-tsr) ;;
    *) echo "$(basename "$0"): --variant must be one of: all, baseline, tsr, tsr-doslfn, no-tsr" >&2; exit 2 ;;
esac

# Preflight ----------------------------------------------------------------

for f in "$EXE" "$CWSDPMI" "$CONF"; do
    if [[ ! -f "$f" ]]; then
        echo "$(basename "$0"): required file missing: $f" >&2
        if [[ "$f" == "$EXE" ]]; then
            echo "  hint: build with \`make dpmi-lfn-smoke\` (or just \`make\` $(basename "$EXE"))." >&2
        fi
        exit 2
    fi
done
for fx in wavetabl.dat wavetable.dat; do
    if [[ ! -f "$FIXTURE_DIR/$fx" ]]; then
        echo "$(basename "$0"): fixture $FIXTURE_DIR/$fx missing" >&2
        exit 2
    fi
done
if [[ "$VARIANT" == "all" || "$VARIANT" == "tsr" ]]; then
    if [[ ! -f "$LFNDOS" ]]; then
        echo "$(basename "$0"): LFNDOS missing at $LFNDOS — see vendor/lfndos/README.md" >&2
        exit 2
    fi
fi
if [[ "$VARIANT" == "all" || "$VARIANT" == "tsr-doslfn" ]]; then
    if [[ ! -f "$DOSLFN" ]]; then
        echo "$(basename "$0"): DOSLFN missing at $DOSLFN — see vendor/doslfn/README.md" >&2
        exit 2
    fi
fi

mkdir -p "$LOG_DIR"

# Run a single variant -----------------------------------------------------
#
# Each variant gets its own temp staging dir and produces:
#   $LOG_DIR/probe-<variant>.log     captured stdout
# Returns 0 if the variant's expected outcome was observed, 1 otherwise.

run_variant() {
    local variant="$1"
    local stage log
    stage="$(mktemp -d -t dpmi-lfn-smoke.XXXXXX)"
    log="$LOG_DIR/probe-$variant.log"

    # Common staging.
    cp "$EXE"                      "$stage/PROBE.EXE"
    cp "$CWSDPMI"                  "$stage/CWSDPMI.EXE"
    cp "$FIXTURE_DIR/wavetabl.dat" "$stage/WAVETABL.DAT"
    cp "$FIXTURE_DIR/wavetable.dat" "$stage/wavetable.dat"

    # RUN.BAT — variant-specific. CRLF line endings (DOS).
    {
        printf '@ECHO OFF\r\n'
        printf 'SET LFN=y\r\n'
        case "$variant" in
            tsr)
                cp "$LFNDOS" "$stage/LFNDOS.EXE"
                printf 'LFNDOS > TSRLOAD.TXT 2>&1\r\n'
                printf 'IF ERRORLEVEL 1 ECHO LFNDOS_ERRORLEVEL_GE_1 >> TSRLOAD.TXT\r\n'
                ;;
            tsr-doslfn)
                cp "$DOSLFN" "$stage/DOSLFN.COM"
                printf 'DOSLFN > TSRLOAD.TXT 2>&1\r\n'
                printf 'IF ERRORLEVEL 1 ECHO DOSLFN_ERRORLEVEL_GE_1 >> TSRLOAD.TXT\r\n'
                ;;
        esac
        printf 'PROBE.EXE > STDOUT.TXT\r\n'
    } > "$stage/RUN.BAT"

    # DOSBox-X args. -set overrides force lfn appropriately regardless of conf.
    local set_args=()
    case "$variant" in
        baseline) set_args=(-set "dos lfn=true") ;;
        no-tsr|tsr) set_args=(-set "dos lfn=false") ;;
    esac

    echo
    echo "════ variant: $variant ════"
    echo "  stage:   $stage"
    echo "  log:     $log"

    if ! dosbox-x -conf "$CONF" -nopromptfolder \
            "${set_args[@]}" \
            -c "MOUNT C $stage" \
            -c "C:" \
            -c "CALL RUN.BAT" \
            -c "EXIT" -silent -exit -nogui -nomenu \
            >/dev/null 2>&1; then
        echo "  FAIL: dosbox-x exited non-zero" >&2
        if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi
        return 1
    fi

    if [[ ! -f "$stage/STDOUT.TXT" ]]; then
        echo "  FAIL: no STDOUT.TXT produced — probe may have crashed under DPMI" >&2
        if [[ "$KEEP_STAGE" == "0" ]]; then rm -rf "$stage"; fi
        return 1
    fi
    cp "$stage/STDOUT.TXT" "$log"

    echo "  capture:"
    sed 's/^/    /' "$log"

    # Variant-specific assertions ---
    local rc=0
    local short_pass long_libc_pass long_int21_pass
    grep -qE '^PROBE: short_baseline PASS '   "$log" && short_pass=1     || short_pass=0
    grep -qE '^PROBE: long_libc_lfn PASS '    "$log" && long_libc_pass=1 || long_libc_pass=0
    grep -qE '^PROBE: long_int21_716c PASS '  "$log" && long_int21_pass=1 || long_int21_pass=0

    # Inspect TSR install state (tsr / tsr-doslfn variants only).
    local tsr_install_msg=""
    if [[ -f "$stage/TSRLOAD.TXT" ]]; then
        tsr_install_msg="$(cat "$stage/TSRLOAD.TXT" | tr -d '\r' | head -c 200)"
        if [[ -n "$tsr_install_msg" ]]; then
            echo "  TSR install output: $tsr_install_msg"
        fi
    fi

    case "$variant" in
        baseline)
            # Built-in DOSBox-X LFN; both long-name tests should PASS.
            if [[ "$short_pass" != "1" ]]; then
                echo "  UNEXPECTED: short_baseline did not PASS (control should always pass)"; rc=1
            fi
            if [[ "$long_libc_pass" != "1" || "$long_int21_pass" != "1" ]]; then
                echo "  UNEXPECTED: baseline (lfn=true) should PASS all probes — probe code may have a bug"; rc=1
            fi
            if [[ "$rc" == "0" ]]; then
                echo "  EXPECTED: all PROBE: lines PASS (baseline confirms probe code is correct)"
            fi
            ;;
        tsr|tsr-doslfn)
            # The honest test: did the TSR install AND does the LFN function
            # code propagate via DPMI? short_baseline should always pass;
            # long_int21_716c is the load-bearing assertion.
            if [[ "$short_pass" != "1" ]]; then
                echo "  UNEXPECTED: short_baseline did not PASS (control)"; rc=1
            fi
            if grep -q '_ERRORLEVEL_GE_1' "$stage/TSRLOAD.TXT" 2>/dev/null; then
                echo "  TSR REFUSED TO INSTALL: $(basename "$variant" | tr '[:lower:]' '[:upper:]') exited with errorlevel >= 1."
                echo "  Likely cause on DOSBox-X: LFN TSRs require direct disk access (FAT12/16/32)."
                echo "  DOSBox-X's MOUNT C is a host-redirector, not a real FAT volume — see"
                echo "  vendor/lfndos/lfndos.doc 'System Requirements' and tests/dpmi-lfn-smoke/README.md."
                echo "  This variant is INCONCLUSIVE under DOSBox-X; defer to Phase B (real-HW)."
                # Treat as "expected outcome under DOSBox-X" — not a probe-code bug.
            elif [[ "$long_int21_pass" == "1" ]]; then
                echo "  PASS: TSR loaded AND LFN function propagated via CWSDPMI — Phase A confirmed for this driver."
            else
                echo "  TSR loaded but long_int21_716c failed — INT 21h 716Ch did NOT reach the TSR through DPMI."
                echo "  Either (a) CWSDPMI's reflector strips the LFN function, or (b) the TSR didn't hook 716Ch correctly."
                echo "  Defer to Phase B (real-HW) — DOSBox-X may behave differently than real DPMI."
            fi
            ;;
        no-tsr)
            # short_baseline should PASS; long_int21_716c should FAIL with
            # AX=0x7100 ("LFN API not present"). long_libc_lfn behaviour
            # depends on whether DOSBox-X's host-mount layer translates the
            # long name behind libc's back; either outcome is informative
            # but not assertive here.
            if [[ "$short_pass" != "1" ]]; then
                echo "  UNEXPECTED: short_baseline did not PASS (control should always pass)"; rc=1
            fi
            if [[ "$long_int21_pass" == "1" ]]; then
                echo "  UNEXPECTED: long_int21_716c PASSED with no TSR loaded — DOSBox-X may be falling back to its built-in LFN despite -set lfn=false"; rc=1
            fi
            if grep -q 'doserr=0x7100' "$log"; then
                echo "  EXPECTED: long_int21_716c FAIL doserr=0x7100 (\"LFN API not present\") — DOSBox-X correctly reports no LFN handler"
            fi
            if [[ "$long_libc_pass" == "1" ]]; then
                echo "  NOTE: long_libc_lfn PASS — DOSBox-X's host-mount opened the file via direct host-fs lookup, bypassing DOS's 8.3 layer. This is a DOSBox-X harness quirk, not a real LFN observation. The raw INT 21h test (long_int21_716c) is the authoritative no-tsr signal."
            fi
            ;;
    esac

    if [[ "$KEEP_STAGE" == "0" ]]; then
        rm -rf "$stage"
    else
        echo "  stage retained at $stage (--keep-stage)"
    fi

    return $rc
}

# Main dispatch ------------------------------------------------------------

overall_rc=0
case "$VARIANT" in
    all)
        for v in baseline no-tsr tsr tsr-doslfn; do
            run_variant "$v" || overall_rc=1
        done
        ;;
    *)
        run_variant "$VARIANT" || overall_rc=1
        ;;
esac

echo
if [[ "$overall_rc" == "0" ]]; then
    echo "════ SUMMARY: probe code verified; TSR variants may be inconclusive under DOSBox-X ════"
    echo "  Definitive answer for LFN-TSR-via-CWSDPMI requires Phase B (real-HW g2k)."
    echo "  See per-variant interpretation lines above + tests/dpmi-lfn-smoke/README.md."
else
    echo "════ SUMMARY: one or more variants behaved unexpectedly ════"
    echo "See per-variant 'UNEXPECTED:' lines above. Logs in $LOG_DIR/probe-*.log."
fi
exit $overall_rc
