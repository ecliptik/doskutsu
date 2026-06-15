#!/usr/bin/env bash
#
# tests/setup-review-walk.sh -- drive SETUP.EXE through every screen on a VISIBLE
# X server and capture a screenshot of each, so the operator + team-lead can A/B
# the redesigned TUI (T24/T25: top profile panel + audio-test popup) against the
# prior rounds WITHOUT a CF-card shuffle.
#
# This is a REVIEW / VISUAL-CAPTURE tool, not a pass/fail gate -- the
# deterministic pass/fail gate is tests/run-setup-e2e.sh. This script only
# launches SETUP, walks it, and shoots PNGs.
#
# It builds the AUDIOTEST=1 (live-audio) setup -- the dist build -- so the new
# T24 "Test SFX / Music" popup (Testing window + progress bar + "Did you hear
# it? Y/N") renders LIVE for capture. This is now safe: T14 (the real-HW
# audio-test HARD-FREEZE) is fixed + shipped, SKIP_DETECTION is set, and the
# SB16 device open is DOSBox-tolerant. (Prior rounds used the AUDIOTEST=0
# scaffold here precisely because T14 was unfixed; that constraint is lifted.)
#
# Usage:
#   # Operator-facing review on the local Xorg (:0). Leaves DOSBox-X OPEN at
#   # the end so the operator can poke at it live:
#   tests/setup-review-walk.sh
#
#   # Private tooling smoke on an own Xvfb (does not touch the operator's :0):
#   tests/setup-review-walk.sh --own-xvfb --display :88 --no-keep
#
# Flags:
#   --display :N   X display to drive (default :0 -- the operator's real Xorg).
#   --out DIR      Screenshot dir (default /tmp/setup-review).
#   --own-xvfb     Spin a private Xvfb on --display (for tooling smoke; the
#                  operator can't see an Xvfb, so never use this for the real
#                  review).
#   --no-keep      Kill DOSBox-X at the end (default: leave it running on :0 so
#                  the operator can drive it by hand after the walk).
#   --fast         Use dosbox-x-fast.conf (default: parity conf).
#
# NOTE on :0 vs :79: the operator's real Xorg is :0 (use it). :79 is a leftover
# dead Xvfb socket -- do NOT target it for the operator review.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE_DIR="$REPO_ROOT/build/stage"
SETUP_EXE="$REPO_ROOT/build/setup/setup.exe"
CWSDPMI_DIR="$REPO_ROOT/vendor/cwsdpmi"
CONF_PARITY="$REPO_ROOT/tools/dosbox-x.conf"
CONF_FAST="$REPO_ROOT/tools/dosbox-x-fast.conf"

XDISPLAY=":0"
OUT_DIR="/tmp/setup-review"
OWN_XVFB=0
KEEP=1
CONF=""
# Baseline audio backend for the deterministic start-state CFG. Default opl3
# (so the music test plays the real Title theme via OPL3). Override with
# REVIEW_BACKEND=organya to capture the organya-mode audio-test screens (the
# T36/#11 pre-rendered-snippet feature + its cache-absent tone fallback ABOUT).
REVIEW_BACKEND="${REVIEW_BACKEND:-opl3}"

while (($#)); do
  case "$1" in
    --display)  shift; XDISPLAY="${1:-:0}" ;;
    --out)      shift; OUT_DIR="${1:-/tmp/setup-review}" ;;
    --own-xvfb) OWN_XVFB=1 ;;
    --no-keep)  KEEP=0 ;;
    --fast)     CONF="$CONF_FAST" ;;
    -h|--help)  sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "setup-review-walk.sh: unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done
[[ -n "$CONF" ]] || CONF="$CONF_PARITY"

mkdir -p "$OUT_DIR"

# The base confs set oplmode=none, so SETUP's OPL3 detect @0x388 fails ("no
# chip") and the backend=opl3 music test cannot play the real Title theme (it
# returns rc=1 -> "Not working"). For the review capture we want DOSBox-X to
# emulate the YMF262 so the OPL3 title theme actually plays (audible on :0 + the
# "Real Title theme ... via OPL3" result line). Same one-line sed the E2E suite
# uses (run-setup-e2e.sh CONF_OPL3). oplmode only enables FM emulation; nothing
# else in the capture changes.
if grep -q 'oplmode' "$CONF" 2>/dev/null; then
  CONF_OPL3="$OUT_DIR/dosbox-review-opl3.conf"
  sed 's/oplmode *= *none/oplmode    = opl3/' "$CONF" > "$CONF_OPL3"
  CONF="$CONF_OPL3"
fi

XVFB_PID=""

log()   { echo "[setup-review] $*"; }

DBX_WIN=""   # DOSBox-X window id (set after launch)

# Assert KEYBOARD INPUT focus on the DOSBox-X window. On a headless Xvfb (no WM)
# the only window has focus automatically, but on the operator's real :0 Xorg the
# WM raises a window on windowactivate WITHOUT giving it keyboard focus -- so
# xdotool's XTEST key events (which go to the FOCUSED window) land nowhere unless
# we ALSO windowfocus. We re-assert before every batch because focus can drift on
# a live desktop. (XTEST-to-focused is more reliable for SDL2 apps like DOSBox-X
# than XSendEvent via `key --window`, which SDL ignores.)
focus_dosbox() {
  [[ -n "$DBX_WIN" ]] || DBX_WIN="$(DISPLAY="$XDISPLAY" xdotool search --name DOSBox 2>/dev/null | head -1)"
  [[ -n "$DBX_WIN" ]] || return 1
  DISPLAY="$XDISPLAY" xdotool windowactivate --sync "$DBX_WIN" windowfocus "$DBX_WIN" >/dev/null 2>&1 || true
  sleep 0.25
}

shoot() { rm -f "$OUT_DIR/$1.png"   # scrot appends _000 instead of overwriting; clear first
          DISPLAY="$XDISPLAY" scrot -u "$OUT_DIR/$1.png" >/dev/null 2>&1 \
            || DISPLAY="$XDISPLAY" import -window "$(DISPLAY="$XDISPLAY" xdotool getactivewindow)" \
                 "$OUT_DIR/$1.png" >/dev/null 2>&1 || true
          log "  shot $1.png"; }

# send_keys k1 k2 ... -- xdotool keysyms, sent one at a time with a TUI-redraw
# settle. Re-asserts DOSBox focus first so keys land on :0 (real WM), not just
# headless Xvfb. Keysyms: Up Down Left Right Return Escape space F10 Home ...
send_keys() {
  local k
  focus_dosbox
  for k in "$@"; do
    DISPLAY="$XDISPLAY" xdotool key --clearmodifiers --delay 120 "$k" >/dev/null 2>&1
    sleep 0.2
  done
}

cleanup() {
  if [[ "$KEEP" == "0" ]]; then pkill -x dosbox-x >/dev/null 2>&1 || true; fi
  if [[ -n "$XVFB_PID" ]]; then kill "$XVFB_PID" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
require_tools() {
  local missing=0 t
  for t in dosbox-x xdotool scrot; do
    command -v "$t" >/dev/null 2>&1 || { echo "missing tool: $t" >&2; missing=1; }
  done
  [[ "$OWN_XVFB" == "0" ]] || command -v Xvfb >/dev/null 2>&1 \
    || { echo "missing tool: Xvfb (needed for --own-xvfb)" >&2; missing=1; }
  [[ "$missing" == "0" ]] || exit 3
}

start_own_xvfb() {
  pkill -f "Xvfb $XDISPLAY " >/dev/null 2>&1 || true
  sleep 0.5
  Xvfb "$XDISPLAY" -screen 0 800x600x24 >"$OUT_DIR/xvfb.log" 2>&1 &
  XVFB_PID=$!
  sleep 2
  kill -0 "$XVFB_PID" 2>/dev/null || { echo "Xvfb failed on $XDISPLAY" >&2; exit 4; }
  log "private Xvfb up on $XDISPLAY (PID $XVFB_PID)"
}

# Build the AUDIOTEST=1 (live-audio) SETUP.EXE + assemble the stage (DOSKUTSU.EXE
# + CWSDPMI + data). `make stage` builds the DEFAULT (scaffold, AUDIOTEST=0)
# setup, so -- exactly as tests/run-setup-e2e.sh does -- we stage that first,
# then build the AUDIOTEST=1 variant LAST and install it over the scaffold.
ensure_stage() {
  log "make stage (lays out DOSKUTSU.EXE + CWSDPMI + data)..."
  make -C "$REPO_ROOT" stage >"$OUT_DIR/make-stage.log" 2>&1 \
    || { echo "make stage failed -- see $OUT_DIR/make-stage.log" >&2; exit 5; }
  log "make setup AUDIOTEST=1 (live-audio build -- the dist build)..."
  make -C "$REPO_ROOT/setup" clean >>"$OUT_DIR/make-stage.log" 2>&1 || true
  make -C "$REPO_ROOT/setup" AUDIOTEST=1 >>"$OUT_DIR/make-stage.log" 2>&1 \
    || { echo "make setup AUDIOTEST=1 failed -- see $OUT_DIR/make-stage.log" >&2; exit 5; }
  install -m 0644 "$SETUP_EXE" "$STAGE_DIR/SETUP.EXE"
  [[ -f "$STAGE_DIR/SETUP.EXE" ]] || { echo "SETUP.EXE not staged" >&2; exit 5; }
  # Two-witness: confirm it IS the AUDIOTEST=1 build (scaffold marker ABSENT),
  # so the Test SFX/Music popup will actually play. Retry the read: install on a
  # freshly-rebuilt tree can race a single strings read -- settle, read up to 3x.
  local i scaffold=1
  sync 2>/dev/null || true
  for i in 1 2 3; do
    if ! strings "$STAGE_DIR/SETUP.EXE" 2>/dev/null | grep -q 'not yet linked'; then
      scaffold=0; break
    fi
    sleep 0.3
  done
  if [[ "$scaffold" == "0" ]]; then
    log "  AUDIOTEST=1 confirmed: 'not yet linked' scaffold marker ABSENT."
  else
    log "  WARNING: staged SETUP.EXE still has the scaffold marker after 3 reads --"
    log "           AUDIOTEST=1 did not take; the Test SFX/Music popup will be a stub."
  fi
  # Deterministic START STATE: write a known DOSKUTSU.CFG so SETUP loads fixed
  # values (no first-run recommend_apply seeding the backend from detected HW,
  # which would make fixed Right-counts land on different backends). This is the
  # same trick the E2E suite uses; it makes the greying-state captures (Organya
  # un-grey, Sound Disabled grey-out) reproducible.
  #
  # AUDIO_BACKEND=opl3 (backend list order Auto,WaveBlaster,OPL3,Organya): chosen
  # so the audio-test MUSIC cell plays the REAL Title theme via OPL3 (team-lead
  # review-4 ask) -- the audiotest resolves auto/organya/pcm to the PCM tone, so
  # only an explicit opl3 backend exercises the curly.mid SMF on the emulated
  # YMF262. From opl3, Right x1 reaches Organya for the note-demo (shot 23).
  # AUDIO_OFF=0 -> Sound starts Enabled.
  {
    printf '; baseline written by setup-review-walk.sh\r\n'
    printf 'AUDIO_BACKEND=%s\r\n' "$REVIEW_BACKEND"
    printf 'AUDIO_OFF=0\r\n'
    printf 'AUDIO_TIER2=1\r\n'
    printf 'SB16_VOICE_VOL=31\r\n'
    printf 'SB16_FM_VOL=28\r\n'
    printf 'ORG_PRERENDER=0\r\n'
  } > "$STAGE_DIR/DOSKUTSU.CFG"
  log "  wrote deterministic baseline DOSKUTSU.CFG (backend=$REVIEW_BACKEND, sound on)."
}

launch_setup() {
  if pgrep -x dosbox-x >/dev/null 2>&1; then
    echo "dosbox-x already running -- kill it first (pkill -x dosbox-x)." >&2
    exit 1
  fi
  local args=(-conf "$CONF" -nopromptfolder
    -c "MOUNT C $STAGE_DIR"
    -c "MOUNT D $CWSDPMI_DIR"
    -c 'SET PATH=Z:\;C:\;D:\'
    -c 'SET BLASTER=A220 I5 D1 H5 T6'
    -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1'
    -c 'SET SDL_INVALID_PARAM_CHECKS=0'
    -c 'SET DOSKUTSU_LOG_VERBOSE=1'
    -c 'C:'
    -c 'SETUP.EXE')
  log "launching SETUP.EXE on DISPLAY=$XDISPLAY (conf=$(basename "$CONF"))..."
  DISPLAY="$XDISPLAY" dosbox-x "${args[@]}" >"$OUT_DIR/dosbox.log" 2>&1 &
  local i
  for i in $(seq 1 20); do pgrep -x dosbox-x >/dev/null 2>&1 && break; sleep 0.5; done
  pgrep -x dosbox-x >/dev/null 2>&1 || { echo "dosbox-x did not start" >&2; exit 6; }
  for i in $(seq 1 10); do
    DBX_WIN="$(DISPLAY="$XDISPLAY" xdotool search --name DOSBox 2>/dev/null | head -1)"
    [[ -n "$DBX_WIN" ]] && break
    sleep 0.5
  done
  sleep 8          # SETUP boot (profile_detect; scaffold has no SDL link, so faster)
  focus_dosbox     # assert keyboard focus (windowactivate + windowfocus) for :0
}

# ===========================================================================
# SCREEN WALK  (T24/T25 final layout: nx-engine 58d0933/5065939/9cadca7 +
#               sdl-engine dd1ef08 audio-test progress/log-suppress)
# ===========================================================================
# Nav model (matches tests/run-setup-e2e.sh):
#   - T25 DROPPED "System profile" from the menu -- it is now an always-on
#     PANEL pinned ABOVE the menu (non-selectable). The main menu re-seeds the
#     highlight to idx0 each entry; HOME anchors to idx0. Reach main item N =
#     Home then Down x N, Enter. New indices:
#       0 Sound setup  1 Sound hardware  2 Test SFX/Music  3 Input  4 Performance
#       5 Advanced     6 Auto-detect     7 Save and exit    8 Quit
#   - HOME anchors the first selectable row on every screen, so captures are
#     deterministic regardless of where the cursor last sat.
#   - Editing screens (Sound/Hardware/Input/Perf/Advanced): T62 (facea95) model --
#     ONLY Space/Right cycle a value +1, Left -1; Enter NO LONGER cycles. Enter
#     COMMITS the current row (advances the ESC revert baseline, no prompt) and
#     moves to the next live row. The status bar reads "Space/Left-Right Change
#     Enter Save+Next   ESC Back". T44 SESSION-EDIT MODEL: subscreen F10 does
#     nothing; ESC asks "Save setting?" only if the screen changed SINCE THE LAST
#     COMMIT (default Yes = keep), else silent back. Persistence is
#     ONLY at the main menu (F10 / "Save and exit"). This is a REVIEW walk that
#     NEVER saves (it leaves the live window without Save-and-exit, and
#     ensure_stage rewrites the baseline each run), so capture-time edits never
#     reach the CFG -- but they DO persist in-session, so any destructive edit
#     (e.g. the Sound-setup note-demo) is reverted before a dependent later
#     capture (the audio test needs the baseline backend).
#   - Test SFX/Music (idx2): T24 chooser {Test SFX, Test music, Back}; Enter on a
#     test pops the modal "Testing ..." popup (progress bar, auto-plays the
#     bounded test), then repaints "Did you hear it?" as a Yes/No MENU (round-5/
#     T27: default-Yes highlight, Left/Right/Up/Down toggle, Enter confirms, y/n
#     immediate shortcuts, ESC=No). Answering -> back to chooser, setting a
#     result badge (round-5: "Working" / "Not working" / "Not tested" -- was
#     "Heard" / "No sound" / "Could not play" / "Not tested"). ESC -> main menu.
#   - Message screens (Auto-detect) are modal tui_message boxes -- Return dismiss.
# Greyed rows are skipped by nav; we deliberately set the enabling state to
# capture both the greyed and un-greyed appearances the operator asked to A/B.

walk() {
  # send_keys re-asserts DOSBox keyboard focus before each batch (needed on :0).
  # Home is idempotent, so the leading double-Home is a harmless anchor + a
  # first-keystroke-race absorber.
  send_keys Home Home
  shoot "00-main-menu"                 # idx0 highlighted + TOP PROFILE PANEL + help box

  # --- Test SFX / Music (idx2): chooser + LIVE popup (AUDIOTEST=1) ---------
  # CAPTURED FIRST, on the clean opl3 baseline (T44: edits are sticky now, so
  # this must run before the destructive Sound note-demo or organya/disabled
  # would leak into the music test). Baseline CFG = AUDIO_BACKEND=opl3, so the
  # music cell plays the REAL Title theme via OPL3. Round-6 (T31): per-highlighted
  # ABOUT box; result popup is question-only + VERTICAL Yes/No (y/n shortcuts).
  # This is the FIRST real nav after boot -- absorb the first-keystroke race
  # (Downs can drop on :0 right after the window appears) with a settle + a
  # separate anchor batch BEFORE the Down-nav, so "item 2" lands reliably.
  send_keys Home Home; sleep 1.0        # anchor + settle (race absorber)
  send_keys Down Down; sleep 0.5        # -> item 2 (Test SFX/Music)
  send_keys Return; sleep 1.5           # enter the chooser
  shoot "40-audiotest-chooser-sfx-about"   # Test SFX highlighted -> SFX ABOUT (full-width highlight)
  send_keys Return; sleep 1.5           # "Playing Sound Effect..." popup -> result popup
  shoot "40b-sfx-result-yesno-vertical"  # question-only + VERTICAL Yes/No menu (no status line)
  send_keys y; sleep 0.5                 # 'y' = Yes -> chooser (sel0)
  send_keys Down; sleep 0.5             # chooser sel1 = Test music -> ABOUT switches to music text
  shoot "40c-audiotest-chooser-music-about"  # Test music highlighted -> music ABOUT (OPL3)
  send_keys Return                     # -> "Playing Music..." popup; real Title theme (OPL3) until-key
  sleep 0.5; shoot "41-popup-playing-music"   # "Playing Music..." caption + progress bar
  send_keys space; sleep 0.5            # stop the until-key play -> question-only result popup
  shoot "42-music-result-yesno-vertical"   # question-only + VERTICAL Yes/No menu
  send_keys y; sleep 0.8               # 'y' = Yes -> chooser, music badge -> Working
  shoot "43-chooser-badge-working"      # both badges "Working"
  send_keys Escape; sleep 0.5          # chooser -> main menu (clean baseline still intact)

  # --- Sound setup (idx0): the T16 redesign + greying showcase ------------
  send_keys Home Return; sleep 1
  shoot "20-sound-default"             # Enabled, backend MIDI (OPL3), full names, help
  send_keys Home;  shoot "21-sound-row-enabled-help"   # row0 help
  send_keys Down;  shoot "22-sound-row-backend-help"   # row1 backend help (full names)
  # Cycle backend opl3->organya (1 Right; list order Auto,WaveBlaster,OPL3,
  # Organya) so Organya pre-render UN-greys; on a slow CPU this also pops the
  # centered slow-CPU Note box (CPU-gated -- may not render under DOSBox-X's fast
  # emulated CPU; the operator confirms on the 486).
  send_keys Right; sleep 0.5
  # The Sound NOTE box FOLLOWS the highlighted row. On the backend row (organya
  # selected, NOT the pre-render row) the round-6 note reads "Organya is
  # demanding on this CPU - enable Organya pre-render to improve performance."
  # (round-6 items 2/3: ";" -> "-", "heavy" -> "demanding").
  shoot "23-sound-organya-note-backend-row"   # reworded note on a NON-prerender row
  # Step to the pre-render row: the note switches to the pre-render-specific text
  # (note-follows-row A/B vs shot 23). Round-6 item 4: the keyed "On" help line
  # now WRAPS indented inside the box (was overflowing outside).
  send_keys Down;  shoot "24-sound-prerender-row-note"  # pre-render note + wrapped per-line help
  # Audio quality row (row3): T45 changed the VALUE from "On"/"Off" to
  # "11025Hz"/"22050Hz" (cfg key AUDIO_TIER2 unchanged) + added a conditional
  # Organya NOTE here (visible because backend=organya right now). Capture the
  # value at 11025Hz (baseline) + the new note, then Right->22050Hz + capture,
  # then Left back to 11025Hz (restore baseline before the revert below).
  send_keys Down; sleep 0.3; shoot "26-sound-audioquality-11025hz"   # value "11025Hz" + help + organya note
  send_keys Right; sleep 0.3; shoot "27-sound-audioquality-22050hz"  # toggled value "22050Hz"
  send_keys Left; sleep 0.3            # back to 11025Hz (baseline)
  # Toggle Sound -> Disabled (Home=row0, Space=toggle): rows 1-5 GREY out.
  # NOTE: xdotool's spacebar keysym is lowercase 'space' (capital 'Space' silently
  # no-ops). This also visually confirms the T17 Space=change binding works.
  send_keys Home space; sleep 0.5
  shoot "25-sound-disabled-greys-others"
  # T44: ESC now KEEPS edits. NO revert needed here -- the audio test (which
  # needs the baseline opl3) runs EARLIER in the walk (before this destructive
  # note-demo), and everything after this (hardware/input/perf/advanced/
  # autodetect) is backend/sound-independent. The session-edit finale runs on
  # the Performance screen (independent of the disabled-Sound state left here).
  # The walk never saves + ensure_stage rewrites the baseline each run, so this
  # in-session dirty state (organya + sound-disabled) reaches nothing it breaks.
  # T52: ESC from this CHANGED Sound screen pops "Save setting?" (default Yes) --
  # answer Return (Yes = keep). (The finale captures this modal; here we just
  # answer it.) -> main menu (session dirty -- harmless, nothing after needs it).
  send_keys Escape; sleep 0.5          # ESC (changed screen) -> "Save setting?"
  send_keys Return; sleep 0.5          # Return = Yes = keep -> main menu

  # --- Sound hardware (idx1) ----------------------------------------------
  # Round-5: Card Type values now carry traditional names ("T6 (Sound Blaster
  # 16)", "T4 (Sound Blaster Pro)", ...). Rows: 0 Override AUTOEXEC.BAT (T37
  # renamed from "Override AUTOEXEC BLASTER"), 1 I/O port, 2 IRQ,
  # 3 8-bit DMA, 4 16-bit DMA, 5 MPU-401 port, 6 Card type (LAST). (Row 1 label
  # is "I/O port" as of 6dd6fc6 -- was "Base I/O port".) The traditional name
  # shows on EVERY shot (always rendered); we also land the highlight on the
  # Card type row to capture its per-line help.
  send_keys Home Down Return; sleep 1
  shoot "30-sound-hardware"
  send_keys Home; shoot "31-hw-override-help"      # row0 override + per-line help
  send_keys Down; sleep 0.3; shoot "32-hw-ioport-full-width-highlight"   # row1 I/O port: round-6 full-width highlight (item 1 supersedes stop-at-value)
  send_keys Down Down Down Down Down; sleep 0.4    # row1 -> row6 (Card type, last row)
  shoot "33-hw-cardtype-traditional-name"          # Card type highlighted: "T6 (Sound Blaster 16)" + help
  # T54 (c668d7b) FIXED the spurious-prompt bug: a NO-EDIT browse of the Sound
  # Hardware screen now ESCs SILENTLY (it snapshots the seeded entry state and
  # only prompts on a real field/override change). So NO workaround -- a bare ESC
  # backs out to the main menu. This bare-ESC run IS the T54 regression witness:
  # if browse+ESC ever pops "Save setting?" again, the walk desyncs = fix
  # regressed. (HWBLASTER E2E scenario, which DOES change the BLASTER, is the
  # positive control -- it still prompts + answers.)
  send_keys Escape; sleep 0.5
  shoot "34-hw-browse-esc-no-prompt"   # T54 witness: browse + ESC -> MAIN MENU, NO "Save setting?" modal

  # (Audio test 40-43 captured earlier, right after the main menu, on the clean
  # opl3 baseline -- before the destructive Sound note-demo.)

  # --- Input / joystick (idx3) --------------------------------------------
  # Round-5: per-line On/Off help (was a prose blob). Capture row0 + row1 help
  # to show the help text FOLLOWS the highlighted row.
  send_keys Home Down Down Down Return; sleep 1
  shoot "50-input-joystick"                        # row0 + its per-line help
  send_keys Down; sleep 0.3; shoot "51-input-row1-help"
  send_keys Escape; sleep 0.5          # browse-only -> silent ESC (T54: no spurious prompt)

  # --- Performance (idx4) -------------------------------------------------
  send_keys Home Down Down Down Down Return; sleep 1
  shoot "60-performance"                           # row0 + its per-line help
  send_keys Down; sleep 0.3; shoot "61-perf-row1-help"
  send_keys Escape; sleep 0.5          # browse-only -> silent ESC (T54)

  # --- Advanced / troubleshooting (idx5) ----------------------------------
  send_keys Home Down Down Down Down Down Return; sleep 1
  shoot "70-advanced"                              # row0 + its per-line help
  send_keys Down; sleep 0.3; shoot "71-advanced-row1-help"
  send_keys Escape; sleep 0.5          # browse-only -> silent ESC (T54)

  # --- Auto-detect best settings (idx6): modal message box ----------------
  # Round-5: the detected Sound line reads "Port 0x..." (was "base 0x..."),
  # matching the Sound-hardware screen + main-menu profile panel wording.
  send_keys Home Down Down Down Down Down Down Return; sleep 1
  shoot "80-autodetect-port"; send_keys Return; sleep 0.4

  # --- T62 Enter=Save+Next then ESC=silent-back demo (team-lead review ask) ----
  # Demonstrates the NEW interaction split on an editing screen. We use Advanced
  # (idx5) because it has >=2 live rows, so the Enter cursor-ADVANCE is visible
  # (the Input screen has a single live row, where the advance wraps in place).
  #   1. Space/Right cycles a value (Enter no longer cycles).
  #   2. Enter COMMITS the row + advances the cursor to the next live row, with
  #      NO "Save setting?" prompt (it also advances the ESC revert baseline).
  #   3. ESC right after that Enter is a SILENT back -- nothing is pending, so no
  #      "Save setting?" modal (contrast shot 90b, where ESC follows an UNcommitted
  #      edit and DOES prompt). The status bar "Space/Left-Right Change  Enter
  #      Save+Next  ESC Back" is visible in every shot here.
  # Advanced's in-session edit is harmless: the walk never saves + ensure_stage
  # rewrites the baseline each run, and nothing after this depends on Advanced.
  send_keys Home Down Down Down Down Down Return; sleep 1   # Advanced (idx5)
  send_keys Home Right; sleep 0.3       # row0 edited via Right (Enter would NOT cycle)
  shoot "85-t62-edit-via-right"         # row0 edit + new status bar (Enter Save+Next)
  send_keys Return; sleep 0.4           # Enter = commit row0 + advance to row1, NO prompt
  shoot "86-t62-enter-commit-advanced"  # highlight moved to row1; NO "Save setting?" modal
  send_keys Escape; sleep 0.5           # ESC after a committed Enter = SILENT back (nothing pending)
  shoot "87-t62-esc-silent-back"        # back at main menu, NO modal (contrast 90b)

  # --- T44 session-edit model demo (review-10): edit -> ESC KEEPS -> re-enter
  #     shows the kept edit -> Save and exit. Demonstrates the new persistence
  #     flow + the "* UNSAVED" dirty marker. Uses the PERFORMANCE screen so it's
  #     independent of the Sound note-demo's left-disabled state. Done LAST (its
  #     edit affects nothing after it). We do NOT press Save and exit -- the
  #     window is left live + unsaved; ensure_stage rewrites the baseline each
  #     run, so the staged CFG is untouched.
  send_keys Home Down Down Down Down Return; sleep 1   # Performance (idx4)
  send_keys Home Right; sleep 0.3       # row0 Performance mode: an edit (0 -> 1)
  shoot "90-sessionedit-made"           # the edit, in-screen
  # T52: ESC from the CHANGED Performance screen pops the "Save setting?" modal
  # (vertical Yes/No, default YES) -- this is now part of the session-edit flow.
  send_keys Escape; sleep 0.5           # ESC (changed) -> "Save setting?" modal
  shoot "90b-save-setting-prompt"       # T52: "Save setting?" (default Yes) -- review-11 item
  send_keys Return; sleep 0.5           # Return = default Yes = KEEP -> main menu
  shoot "91-sessionedit-menu-unsaved"   # main menu status now shows "* UNSAVED"
  send_keys Home Down Down Down Down Return; sleep 1   # re-enter Performance
  send_keys Home; sleep 0.3             # row0 Performance mode
  shoot "92-sessionedit-kept"           # Performance mode STILL changed -> edit KEPT across ESC
  send_keys Escape; sleep 0.5           # back to main menu (re-enter was view-only -> silent ESC, T54)
  send_keys Home Down Down Down Down Down Down Down; sleep 0.3  # item 7 = "Save and exit"
  shoot "93-sessionedit-save-and-exit"  # "Save and exit" highlighted (persist trigger; * UNSAVED in status)
  # T52 QUIT GUARD: ESC at the main menu ALWAYS pops "Quit without saving
  # settings?" (default NO), even when clean -- capture it, then ESC again = No =
  # STAY (a second ESC resolves to No, does NOT exit). Leaves the window live.
  send_keys Escape; sleep 0.5           # ESC at main menu -> quit guard
  shoot "94-quit-without-saving-prompt" # T52: "Quit without saving settings?" (default No) -- review-11 item
  send_keys Escape; sleep 0.5           # ESC again = No = stay -> main menu

  shoot "99-main-menu-final"            # (session dirty -- left live, unsaved)
}

# ---------------------------------------------------------------------------
require_tools
[[ "$OWN_XVFB" == "1" ]] && start_own_xvfb
ensure_stage
launch_setup
walk

log "screenshots in $OUT_DIR/:"
ls -1 "$OUT_DIR"/*.png 2>/dev/null | sed 's/^/  /' || true
if [[ "$KEEP" == "1" ]]; then
  log "DOSBox-X left RUNNING on $XDISPLAY for live operator review."
  log "  stop it with: pkill -x dosbox-x"
else
  log "stopping DOSBox-X (--no-keep)."
fi
