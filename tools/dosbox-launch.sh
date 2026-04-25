#!/usr/bin/env bash
# dosbox-launch.sh — launch DOSBox-X visible on the local X session for
# manual testing, screenshot capture, and xdotool automation.
#
# Differs from tools/dosbox-run.sh (which stages one exe and exits): this
# launcher mounts the repo and vendored CWSDPMI and leaves DOSBox-X running
# so you can drive it by hand or by script.
#
# Usage:
#   tools/dosbox-launch.sh                         # open DOSBox-X at C:\ (repo root)
#   tools/dosbox-launch.sh --fast                  # use dosbox-x-fast.conf
#   tools/dosbox-launch.sh --kill-first            # kill any running instance first
#   tools/dosbox-launch.sh --exe build/doskutsu.exe  # auto-run on launch
#   tools/dosbox-launch.sh --stage                 # mount build/stage/ as C:
#   tools/dosbox-launch.sh --stage --exe DOSKUTSU.EXE  # stage + auto-run
#
# The --stage form runs `make stage` first (so DOSKUTSU.EXE + CWSDPMI.EXE +
# data/ all sit together) and mounts that staging dir as C:. This matches the
# eventual install layout under C:\DOSKUTSU\ on a real CF card and is what
# NXEngine-evo's ResourceManager (SDL_GetBasePath() + "data/") expects. Use
# this for any test that needs the game's data tree.
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
STAGE=0

usage() {
  cat <<'USAGE'
Usage: dosbox-launch.sh [--fast] [--kill-first] [--stage] [--exe PATH]

  --fast, -f         Use dosbox-x-fast.conf (cycles=max) instead of parity config
  --kill-first, -k   Kill any running dosbox-x process first
  --stage, -s        Run `make stage` and mount build/stage/ as C: (where
                     DOSKUTSU.EXE + CWSDPMI.EXE + data/ sit together — the
                     layout NXEngine-evo's ResourceManager expects on DOS).
  --exe PATH         Path to an .exe to auto-run. Without --stage, PATH is
                     relative to repo root. With --stage, PATH should be a
                     bare DOS-side filename (e.g. DOSKUTSU.EXE).
  -h, --help         Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fast|-f)       CONF="$CONF_FAST"; shift ;;
    --kill-first|-k) KILL_FIRST=1; shift ;;
    --stage|-s)      STAGE=1; shift ;;
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

# Mount layout depends on --stage:
#   default:  C: = repo root, D: = vendor/cwsdpmi
#             (handy for sdl3-smoke and one-off .exe testing)
#   --stage:  C: = build/stage/, D: = vendor/cwsdpmi
#             (the runtime layout DOSKUTSU.EXE expects: DOSKUTSU.EXE +
#              CWSDPMI.EXE + data/ all co-located, matching the install
#              layout under C:\DOSKUTSU\ on real CF cards. SDL_GetBasePath()
#              + "data/" then resolves correctly.)
#
# D: always points at vendor/cwsdpmi so CWSDPMI.EXE is on PATH for DJGPP
# binaries that aren't yet staged. The BLASTER env var matches the
# [sblaster] block in dosbox-x.conf; SDL3-DOS reads it.
#
# SDL_DOS_AUDIO_SB_SKIP_DETECTION — escape hatch from patches/SDL/0001. The
# real-HW timing fixes in that patch are correct, but DOSBox-X's emulated SB16
# returns 0xFF on the DSP detection read regardless of timing tuning. Setting
# this env var tells SDL3-DOS to skip detection and trust BLASTER, which is the
# only way audio inits in the emulator. Real hardware (g2k Phase 8) MUST NOT
# set this — it would mask a legitimate Vibra16S regression.

if [[ "$STAGE" == "1" ]]; then
  echo "Running 'make stage' to populate build/stage/..."
  make -C "$REPO_ROOT" stage >/dev/null
  C_DRIVE="$REPO_ROOT/build/stage"
else
  C_DRIVE="$REPO_ROOT"
fi

DBX_ARGS=(-conf "$CONF" -nopromptfolder
          -c "MOUNT C $C_DRIVE"
          -c "MOUNT D $REPO_ROOT/vendor/cwsdpmi"
          -c 'SET PATH=Z:\;C:\;D:\'
          -c 'SET BLASTER=A220 I5 D1 H5 T6'
          -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1'
          -c 'SET SDL_INVALID_PARAM_CHECKS=0'
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
