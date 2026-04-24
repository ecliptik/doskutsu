#!/usr/bin/env bash
# run-smoke.sh — Phase 0 / 1 smoke test runner.
#
# Runs hello.exe (or any specified exe) under DOSBox-X headless, captures
# stdout, and byte-matches it against the expected string. Parity config
# by default; --fast selects the cycles=max config.
#
# Invoked by `make smoke` and `make smoke-fast`.
#
# Exit codes:
#   0 — stdout matched expected string
#   1 — stdout mismatched
#   2 — exe / config not found
#   3 — dosbox-run.sh failed to produce STDOUT.TXT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/hello.exe"
FAST_ARG=""
MERGE_STDERR_ARG=""
EXPECTED="DOSKUTSU smoketest: hello from DJGPP under DPMI"
CONTAINS=""
CAPTURE_PATH=""

usage() {
    cat <<'USAGE'
Usage: tests/run-smoke.sh [--exe PATH] [--fast] [--merge-stderr]
                          [--expected STRING | --contains STRING]
                          [--capture PATH]

  --exe PATH           Executable to run (default: build/hello.exe)
  --fast               Use dosbox-x-fast.conf (cycles=max)
  --merge-stderr       Capture stderr too (passes --merge-stderr to dosbox-run.sh).
                       Required for SDL_Log output (which goes to stderr).
  --expected STRING    Expected exact first-line stdout (default: hello.exe banner).
                       Mutually exclusive with --contains.
  --contains STRING    Pass if STRING appears anywhere in captured stdout.
                       Mutually exclusive with --expected.
  --capture PATH       Also copy captured stdout to PATH (kept after run).
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)         EXE="$2"; shift 2 ;;
        --fast)        FAST_ARG="--fast"; shift ;;
        --merge-stderr) MERGE_STDERR_ARG="--merge-stderr"; shift ;;
        --expected)    EXPECTED="$2"; CONTAINS=""; shift 2 ;;
        --contains)    CONTAINS="$2"; EXPECTED=""; shift 2 ;;
        --capture)     CAPTURE_PATH="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "run-smoke.sh: unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -n "$EXPECTED" && -n "$CONTAINS" ]]; then
    echo "run-smoke.sh: --expected and --contains are mutually exclusive" >&2
    exit 2
fi

if [[ ! -f "$EXE" ]]; then
    echo "run-smoke.sh: exe not found: $EXE" >&2
    echo "              build it first: make hello" >&2
    exit 2
fi

out_file="$(mktemp -t doskutsu-smoke.XXXXXX.out)"
trap 'rm -f "$out_file"' EXIT

if [[ -n "$FAST_ARG" ]]; then
    echo "[smoke] running $EXE under fast config (cycles=max)"
else
    echo "[smoke] running $EXE under parity config (cycles=fixed 40000)"
fi

if ! "$REPO_ROOT/tools/dosbox-run.sh" --exe "$EXE" --stdout "$out_file" $FAST_ARG $MERGE_STDERR_ARG; then
    echo "run-smoke.sh: dosbox-run.sh failed" >&2
    exit 3
fi

if [[ ! -s "$out_file" ]]; then
    echo "run-smoke.sh: captured stdout is empty" >&2
    exit 3
fi

# Optionally preserve the raw capture for the caller (e.g. fixtures, baselines).
if [[ -n "$CAPTURE_PATH" ]]; then
    cp "$out_file" "$CAPTURE_PATH"
fi

# DOS writes CRLF — strip CR before comparison.
if [[ -n "$CONTAINS" ]]; then
    # Substring match anywhere in capture (CRs stripped from each line).
    if tr -d '\r' < "$out_file" | grep -F -q -- "$CONTAINS"; then
        echo "[smoke] PASS: stdout contains \"$CONTAINS\""
        exit 0
    else
        echo "[smoke] FAIL: stdout missing substring" >&2
        echo "  contains: $CONTAINS" >&2
        echo "  full capture:" >&2
        sed 's/^/    /' "$out_file" >&2
        exit 1
    fi
else
    # Exact-match the first line (legacy behaviour preserved for hello.exe).
    got_first="$(sed -n '1s/\r$//p' "$out_file")"
    if [[ "$got_first" == "$EXPECTED" ]]; then
        echo "[smoke] PASS: stdout matched expected string"
        exit 0
    else
        echo "[smoke] FAIL: stdout mismatch" >&2
        echo "  expected: $EXPECTED" >&2
        echo "  got:      $got_first" >&2
        echo "  full capture:" >&2
        sed 's/^/    /' "$out_file" >&2
        exit 1
    fi
fi
