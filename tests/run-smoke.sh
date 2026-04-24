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
EXPECTED="DOSKUTSU smoketest: hello from DJGPP under DPMI"

usage() {
    cat <<'USAGE'
Usage: tests/run-smoke.sh [--exe PATH] [--fast] [--expected STRING]

  --exe PATH           Executable to run (default: build/hello.exe)
  --fast               Use dosbox-x-fast.conf (cycles=max)
  --expected STRING    Expected exact stdout (first line, CR/LF stripped)
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)       EXE="$2"; shift 2 ;;
        --fast)      FAST_ARG="--fast"; shift ;;
        --expected)  EXPECTED="$2"; shift 2 ;;
        -h|--help)   usage; exit 0 ;;
        *) echo "run-smoke.sh: unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

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

if ! "$REPO_ROOT/tools/dosbox-run.sh" --exe "$EXE" --stdout "$out_file" $FAST_ARG; then
    echo "run-smoke.sh: dosbox-run.sh failed" >&2
    exit 3
fi

if [[ ! -s "$out_file" ]]; then
    echo "run-smoke.sh: captured stdout is empty" >&2
    exit 3
fi

# DOS writes CRLF — strip CR before comparison
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
