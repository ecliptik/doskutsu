#!/usr/bin/env bash
#
# tests/setup-palette-matrix.sh -- capture the SETUP.EXE palette + title-bar A/B
# matrix for the operator's review-2 decision. Two SETUP-startup env toggles
# (nx-engine, commit ed6faed) select all variants from ONE scaffold build, so
# this re-launches SETUP once per variant with the env set, and shoots the two
# decision screens (main menu + Music and volumes) each time.
#
#   DOSKUTSU_SETUP_PALETTE  = cs (default) | classic
#   DOSKUTSU_SETUP_TITLEBAR = 1 (default, full-width bar) | 0 (plain centered)
#
# Variants captured (main menu + Music and volumes each = 8 PNGs):
#   csbar     CS palette + title bar      (default: no env)
#   csnobar   CS palette, no title bar    (TITLEBAR=0)
#   classicbar     classic palette + bar  (PALETTE=classic)
#   classicnobar   classic palette, no bar (PALETTE=classic + TITLEBAR=0)
#
# Uses the SCAFFOLD build (AUDIOTEST=0) -- safe, no real-device open; the palette
# is identical in the AUDIOTEST=1 dist build (display-only).
#
# Usage:
#   tests/setup-palette-matrix.sh                       # :0, /tmp/setup-review-2/matrix
#   tests/setup-palette-matrix.sh --own-xvfb --display :88   # private tooling smoke
#
# Flags: --display :N | --out DIR | --own-xvfb | --fast

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE_DIR="$REPO_ROOT/build/stage"
CWSDPMI_DIR="$REPO_ROOT/vendor/cwsdpmi"
CONF_PARITY="$REPO_ROOT/tools/dosbox-x.conf"
CONF_FAST="$REPO_ROOT/tools/dosbox-x-fast.conf"
# shellcheck source=../tools/dosbox-teardown.sh
source "$REPO_ROOT/tools/dosbox-teardown.sh"   # dbx_kill_conf -- conf-scoped teardown

XDISPLAY=":0"
OUT_DIR="/tmp/setup-review-2/matrix"
OWN_XVFB=0
CONF=""

while (($#)); do
  case "$1" in
    --display)  shift; XDISPLAY="${1:-:0}" ;;
    --out)      shift; OUT_DIR="${1:-/tmp/setup-review-2/matrix}" ;;
    --own-xvfb) OWN_XVFB=1 ;;
    --fast)     CONF="$CONF_FAST" ;;
    -h|--help)  sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "setup-palette-matrix.sh: unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done
[[ -n "$CONF" ]] || CONF="$CONF_PARITY"

mkdir -p "$OUT_DIR"
XVFB_PID=""
DBX_WIN=""

log() { echo "[palette-matrix] $*"; }

focus_dosbox() {
  DBX_WIN="$(DISPLAY="$XDISPLAY" xdotool search --name DOSBox 2>/dev/null | head -1)"
  [[ -n "$DBX_WIN" ]] || return 1
  DISPLAY="$XDISPLAY" xdotool windowactivate --sync "$DBX_WIN" windowfocus "$DBX_WIN" >/dev/null 2>&1 || true
  sleep 0.25
}
shoot() { rm -f "$OUT_DIR/$1.png"   # scrot appends _000 instead of overwriting; clear first
          DISPLAY="$XDISPLAY" scrot -u "$OUT_DIR/$1.png" >/dev/null 2>&1 || true; log "  shot $1.png"; }
send_keys() {
  local k; focus_dosbox
  for k in "$@"; do
    DISPLAY="$XDISPLAY" xdotool key --clearmodifiers --delay 120 "$k" >/dev/null 2>&1; sleep 0.2
  done
}
kill_dosbox() {
  dbx_kill_conf "$CONF" >/dev/null 2>&1 || true
  local i; for i in $(seq 1 40); do pgrep -x dosbox-x >/dev/null 2>&1 || { sleep 0.5; return 0; }; sleep 0.5; done
  dbx_kill_conf "$CONF" KILL >/dev/null 2>&1 || true; sleep 1
}
cleanup() {
  kill_dosbox
  [[ -n "$XVFB_PID" ]] && kill "$XVFB_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

require_tools() {
  local t; for t in dosbox-x xdotool scrot; do
    command -v "$t" >/dev/null 2>&1 || { echo "missing tool: $t" >&2; exit 3; }
  done
}

ensure_stage() {
  log "make stage (scaffold SETUP.EXE, AUDIOTEST=0)..."
  make -C "$REPO_ROOT" stage >"$OUT_DIR/make-stage.log" 2>&1 \
    || { echo "make stage failed -- see $OUT_DIR/make-stage.log" >&2; exit 5; }
  local i; for i in 1 2 3; do
    strings "$STAGE_DIR/SETUP.EXE" 2>/dev/null | grep -q 'not yet linked' && { log "  scaffold confirmed (AUDIOTEST=0)."; break; }
    sleep 0.3
    [[ "$i" == 3 ]] && log "  NOTE: scaffold marker not seen after 3 reads (palette is display-only either way)."
  done
  # Deterministic CFG so the Sound setup screen shows a stable state.
  { printf '; palette-matrix baseline\r\n'
    printf 'AUDIO_BACKEND=auto\r\nAUDIO_OFF=0\r\nAUDIO_TIER2=1\r\n'
    printf 'SB16_VOICE_VOL=31\r\nSB16_FM_VOL=28\r\nORG_PRERENDER=0\r\n'
  } > "$STAGE_DIR/DOSKUTSU.CFG"
}

start_own_xvfb() {
  pkill -f "Xvfb $XDISPLAY " >/dev/null 2>&1 || true; sleep 0.5
  Xvfb "$XDISPLAY" -screen 0 800x600x24 >"$OUT_DIR/xvfb.log" 2>&1 &
  XVFB_PID=$!; sleep 2
  kill -0 "$XVFB_PID" 2>/dev/null || { echo "Xvfb failed on $XDISPLAY" >&2; exit 4; }
}

# capture_variant <label> <palette|""> <titlebar|""> -- launch SETUP with the
# given SETUP env toggles, shoot main menu + Sound setup, kill.
capture_variant() {
  local label="$1" pal="$2" bar="$3"
  kill_dosbox    # guarantee a clean single instance before each launch
  local envcmds=()
  [[ -n "$pal" ]] && envcmds+=(-c "SET DOSKUTSU_SETUP_PALETTE=$pal")
  [[ -n "$bar" ]] && envcmds+=(-c "SET DOSKUTSU_SETUP_TITLEBAR=$bar")
  local args=(-conf "$CONF" -nopromptfolder
    -c "MOUNT C $STAGE_DIR" -c "MOUNT D $CWSDPMI_DIR"
    -c 'SET PATH=Z:\;C:\;D:\'
    "${envcmds[@]}"
    -c 'C:' -c 'SETUP.EXE')
  log "[$label] launch (PALETTE=${pal:-cs} TITLEBAR=${bar:-1})..."
  DISPLAY="$XDISPLAY" dosbox-x "${args[@]}" >"$OUT_DIR/dosbox-$label.log" 2>&1 &
  local i; for i in $(seq 1 20); do pgrep -x dosbox-x >/dev/null 2>&1 && break; sleep 0.5; done
  pgrep -x dosbox-x >/dev/null 2>&1 || { echo "[$label] dosbox did not start" >&2; return 1; }
  for i in $(seq 1 10); do focus_dosbox && break; sleep 0.5; done
  sleep 8                              # SETUP boot
  shoot "${label}-1-main"
  # T47: Sound is now a submenu; the palette-representative editing screen is
  # "Music and volumes" (the old Sound setup). Home->idx0 Sound, Enter -> submenu;
  # Home,Down x2 -> "Music and volumes", Enter.
  send_keys Home Return; sleep 0.6
  send_keys Home Down Down Return; sleep 1
  shoot "${label}-2-sound"
  send_keys Escape                     # leave sound screen
  send_keys Escape                     # leave Sound submenu -> main
  kill_dosbox
}

# ---------------------------------------------------------------------------
require_tools
[[ "$OWN_XVFB" == "1" ]] && start_own_xvfb
ensure_stage

capture_variant csbar         ""        ""     # default: CS + title bar
capture_variant csnobar       ""        "0"    # CS, no title bar
capture_variant classicbar    "classic" ""     # classic + title bar
capture_variant classicnobar  "classic" "0"    # classic, no title bar

log "matrix screenshots in $OUT_DIR/:"
ls -1 "$OUT_DIR"/*.png 2>/dev/null | sed 's/^/  /'
