#!/usr/bin/env bash
# dosbox-launch.sh — launch DOSBox-X visible on the local X session for
# manual testing, screenshot capture, and xdotool automation.
#
# Differs from tools/dosbox-run.sh (which stages one exe and exits): this
# launcher mounts the repo and vendored CWSDPMI and leaves DOSBox-X running
# so you can drive it by hand or by script.
#
# Usage:
#   tools/dosbox-launch.sh                         # open DOSBox-X at C:\
#   tools/dosbox-launch.sh --fast                  # use dosbox-x-fast.conf
#   tools/dosbox-launch.sh --kill-first            # kill any running instance first
#   tools/dosbox-launch.sh --exe build/doskutsu.exe  # auto-run on launch
#
# After launch:
#   DISPLAY=:0 scrot -u /tmp/dosbox.png                   # capture focused window
#   DISPLAY=:0 xdotool search --name DOSBox windowactivate --sync
#   DISPLAY=:0 xdotool type --delay 40 'DOSKUTSU'
#   DISPLAY=:0 xdotool key Return
#   pkill -x dosbox-x                                     # stop it

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CONF_PARITY="$SCRIPT_DIR/dosbox-x.conf"
CONF_FAST="$SCRIPT_DIR/dosbox-x-fast.conf"
CONF="$CONF_PARITY"

KILL_FIRST=0
EXE=""

usage() {
  cat <<'USAGE'
Usage: dosbox-launch.sh [--fast] [--kill-first] [--exe PATH]

  --fast, -f         Use dosbox-x-fast.conf (cycles=max) instead of parity config
  --kill-first, -k   Kill any running dosbox-x process first
  --exe PATH         Path to an .exe to auto-run (relative to repo root)
  -h, --help         Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fast|-f)       CONF="$CONF_FAST"; shift ;;
    --kill-first|-k) KILL_FIRST=1; shift ;;
    --exe)           EXE="$2"; shift 2 ;;
    -h|--help)       usage; exit 0 ;;
    *) echo "dosbox-launch.sh: unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ "$KILL_FIRST" == "1" ]] && pgrep -x dosbox-x >/dev/null 2>&1; then
  echo "Stopping running dosbox-x..."
  pkill -x dosbox-x || true
  sleep 1
fi

if pgrep -x dosbox-x >/dev/null 2>&1; then
  echo "dosbox-x is already running. Use --kill-first to restart." >&2
  exit 1
fi

if [[ ! -f "$CONF" ]]; then
  echo "dosbox-launch.sh: conf not found: $CONF" >&2
  exit 2
fi

# Always target the local X session (:0), not any SSH-forwarded DISPLAY the
# caller's shell might have inherited. Override with DOSBOX_DISPLAY=... if
# genuinely needed. Matches the Snow / Basilisk / vellm emulator convention.
export DISPLAY="${DOSBOX_DISPLAY:-:0}"

# C: = repo root (so build/doskutsu.exe + data/ + vendor/cwsdpmi are all visible)
# D: = vendor/cwsdpmi (puts CWSDPMI.EXE on PATH so DJGPP binaries find DPMI host)
# BLASTER env var — matches the [sblaster] block in dosbox-x.conf; SDL3-DOS
# reads this.
DBX_ARGS=(-conf "$CONF" -nopromptfolder
          -c "MOUNT C $REPO_ROOT"
          -c "MOUNT D $REPO_ROOT/vendor/cwsdpmi"
          -c 'SET PATH=Z:\;C:\;D:\'
          -c 'SET BLASTER=A220 I5 D1 H5 T6'
          -c "C:")

if [[ -n "$EXE" ]]; then
  EXE_DOS="$(echo "$EXE" | tr '/' '\\' | tr '[:lower:]' '[:upper:]')"
  DBX_ARGS+=(-c "$EXE_DOS")
fi

CONF_NAME="$(basename "$CONF")"
echo "Launching DOSBox-X (DISPLAY=$DISPLAY, config=$CONF_NAME)..."
dosbox-x "${DBX_ARGS[@]}" &
DBX_PID=$!
echo "DOSBox-X running (PID $DBX_PID)."
echo
echo "  screenshot:  DISPLAY=:0 scrot -u /tmp/dosbox.png"
echo "  focus:       DISPLAY=:0 xdotool search --name DOSBox windowactivate --sync"
echo "  type:        DISPLAY=:0 xdotool type --delay 40 'DOSKUTSU'"
echo "  key:         DISPLAY=:0 xdotool key Return"
echo "  stop:        pkill -x dosbox-x    (or Ctrl+F9 in the window)"
