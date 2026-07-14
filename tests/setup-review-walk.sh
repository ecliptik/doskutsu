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
# AUDIOTEST=1 output is setup-release.exe since the v1.6.2 stub/release split
# (setup/Makefile EXE := setup-release.exe under AUDIOTEST=1). Staging the old
# build/setup/setup.exe path installs the AUDIOTEST=0 scaffold stub -> the Test
# SFX/Music popup shows "not yet linked" instead of playing. Mirrors the
# tests/run-setup-e2e.sh fix (c2b0a72).
SETUP_EXE="$REPO_ROOT/build/setup/setup-release.exe"
CWSDPMI_DIR="$REPO_ROOT/vendor/cwsdpmi"
CONF_PARITY="$REPO_ROOT/tools/dosbox-x.conf"
CONF_FAST="$REPO_ROOT/tools/dosbox-x-fast.conf"
# shellcheck source=../tools/dosbox-teardown.sh
source "$REPO_ROOT/tools/dosbox-teardown.sh"   # dbx_kill_conf -- conf-scoped teardown

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
  if [[ "$KEEP" == "0" ]]; then dbx_kill_conf "$CONF" >/dev/null 2>&1 || true; fi
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
#   - The SYSTEM PROFILE panel is pinned ABOVE the menu (non-selectable). The
#     main menu re-seeds the highlight to idx0 each entry; HOME anchors to idx0.
#     Reach main item N = Home then Down x N, Enter. T47 indices (9 -> 7):
#       0 Sound (SUBMENU)   1 System speed   2 Input (SUBMENU)
#       3 Advanced          4 Auto-detect    5 Save and exit   6 Quit
#   - UX v2 MAIN MENU (8 items): 0 Auto-detect best settings, 1 Sound, 2 System
#     speed, 3 Input, 4 Advanced, 5 Save and run DOSKUTSU, 6 Save and exit,
#     7 Quit without saving. Auto-detect moved to FIRST and "Save and run" is
#     new, so EVERY main-menu Down-count shifted vs v1.
#   - UX v2 SOUND MENU: 0 Express setup, 1 Music Type [3-state CYCLE row],
#     2 Select Music Card [flat hardware picker], 3 Select Sound FX Card,
#     4 Music Options, 5 Test music, 6 Test sound effects, 7 Back. ESC silent.
#     Music Type cycles IN PLACE with Left/Right/Space (no popup); the card
#     picker is a tui_picklist. Greyed rows are SKIPPED by nav: the card row is
#     greyed unless Type==MIDI, Test music is greyed under No Music, Test SFX is
#     greyed when the FX device is No Sound FX -- so the Down-counts depend on
#     the Type. The baseline (AUDIO_BACKEND=opl3 -> Type=MIDI) keeps every row
#     live, which is why the fixed counts below are exact.
#   - Do NOT press Home inside a picker: its start row is derived from the live
#     cfg and carries "(current)"; Home would jump off it.
#   - HOME anchors the first selectable row on every screen, so captures are
#     deterministic regardless of where the cursor last sat.
#   - Editing screens (Music and volumes / Custom setup / Advanced): T62 model --
#     ONLY Space/Right cycle a value +1, Left -1. On Music-and-volumes + Advanced
#     Enter COMMITS the row + advances (no prompt). On Custom setup (T47) Enter
#     OPENS a pick-list instead -- so this walk edits Custom fields with Right,
#     never Enter. T44 SESSION-EDIT MODEL: subscreen F10 does nothing; ESC asks
#     "Save setting?" only if the screen changed since the last commit (default
#     Yes = keep), else silent back. Persistence is ONLY at the main menu (F10 /
#     "Save and exit"). This REVIEW walk NEVER saves (ensure_stage rewrites the
#     baseline each run), so capture-time edits never reach the CFG.
#   - Tests (T47): inline rows of the Sound submenu (the separate chooser was
#     retired). Enter on "Test sound effects" / "Test music" runs the test
#     directly: opens the device, auto-plays the bounded/until-key test, then
#     "Did you hear it?" Yes/No menu (default-Yes; y/n shortcuts; ESC=No) -> back
#     to the submenu with a result badge (Working / Not working / Not tested).
#   - System speed = a tui_picklist popup; Auto-detect = a modal tui_message box
#     (Return dismiss).
# Greyed rows are skipped by nav; we deliberately set the enabling state to
# capture both the greyed and un-greyed appearances the operator asked to A/B.

walk() {
  # send_keys re-asserts DOSBox keyboard focus before each batch (needed on :0).
  # Home is idempotent, so the leading double-Home is a harmless anchor + a
  # first-keystroke-race absorber.
  send_keys Home Home
  shoot "00-main-menu"                 # idx0 highlighted + TOP PROFILE PANEL + help box

  # ===========================================================================
  # UX v2 MAIN MENU (8 items -- Auto-detect FIRST, + Save and run DOSKUTSU):
  #   0 Auto-detect best settings   1 Sound        2 System speed   3 Input
  #   4 Advanced                    5 Save and run DOSKUTSU
  #   6 Save and exit               7 Quit without saving
  # EVERY main-menu Down-count below shifted by +1 vs v1 (Sound was idx0).
  #
  # UX v2 SOUND MENU (screen_sound_menu ROW_* enum):
  #   0 Express setup       1 Music Type [CYCLE row]   2 Select Music Card
  #   3 Select Sound FX Card 4 Music Options           5 Test music
  #   6 Test sound effects  7 Back
  # Music Type is a 3-state CYCLE row -- Left/Right/Space change it IN PLACE, no
  # popup. Cycle order is Organya -> MIDI -> No Music -> Organya (MTYPE_* enum),
  # so from the opl3 baseline (Type=MIDI) one Right lands on No Music.
  # GREY RULES matter for nav (greyed rows are SKIPPED by Up/Down):
  #   Select Music Card  greyed unless Type == MIDI
  #   Test music         greyed when Type == No Music
  #   Test sound effects greyed when the FX device is No Sound FX
  # The deterministic baseline is AUDIO_BACKEND=opl3 -> Type=MIDI, Card=Sound
  # Blaster, so on entry every row is live and the Down-counts are exact.
  # ===========================================================================

  # --- Sound menu (main idx1) ----------------------------------------------
  send_keys Home Down Return; sleep 1   # main idx1 Sound -> the Sound menu
  shoot "10-sound-menu"                # v2: Music Type + Select Music Card rows

  # --- Express setup (Sound row0): DF-UX Phase 2 one-key detect -------------
  # Red warning modal -> re-run probes (brief video-bench flash) -> evidence
  # modal w/ DSP version -> "Test it now?" prompt. We decline (n) so the walk
  # stays deterministic; Express has written the detected BLASTER + opl3 to the
  # session (review only -- this walk never saves).
  send_keys Home Return; sleep 1        # Sound row0 Express -> red DANGER modal
  shoot "12-express-warning"
  send_keys Return; sleep 2             # dismiss -> probes -> evidence modal
  shoot "13-express-evidence"
  send_keys Return; sleep 0.6           # dismiss -> "Test it now?" prompt
  shoot "14-express-test-prompt"
  send_keys n; sleep 0.6                # decline -> back to the Sound menu
  shoot "15-express-back-to-sound"

  # --- Inline audio tests (Sound rows 5,6) ---------------------------------
  # Captured FIRST, on the clean opl3 baseline (edits are sticky). Test music is
  # ROW_TESTMUS=5, Test sound effects ROW_TESTSFX=6 -- note v2 puts MUSIC BEFORE
  # SFX (v1 had them the other way round).
  send_keys Home Down Down Down Down Down; sleep 0.5   # row5 Test music
  send_keys Return; sleep 1.0           # real Title theme via OPL3 (until-key)
  shoot "40-test-music-playing"
  send_keys space; sleep 0.5            # stop the until-key play -> "Did you hear it?"
  send_keys y; sleep 0.8                # Yes -> badge Working
  shoot "41-test-music-badge-working"
  send_keys Down; sleep 0.3             # row6 Test sound effects
  send_keys Return; sleep 1.5           # SFX test auto-plays
  shoot "42-test-sfx-playing"
  send_keys y; sleep 0.8                # Yes -> badge Working
  shoot "43-test-sfx-badge-working"

  # --- Music Type CYCLE row (Sound row1) -----------------------------------
  # The v2 headline. Left/Right change the value in place -- NO picker popup.
  send_keys Home Down; sleep 0.4        # row1 Music Type (value: MIDI)
  shoot "16-musictype-midi"            # Card row LIVE, showing "Sound Blaster (OPL3 FM)"
  send_keys Right; sleep 0.6            # MIDI -> No Music
  shoot "17-musictype-nomusic"         # Card row + Test music now BOTH GREYED
  send_keys Right; sleep 0.6            # No Music -> Organya
  shoot "18-musictype-organya"         # Card row greyed but still shows the REMEMBERED card
  send_keys Right; sleep 0.6            # Organya -> MIDI (restores the remembered card)
  shoot "19-musictype-back-to-midi"    # Card row LIVE again, card restored

  # --- Select Music Card (Sound row2): the restored FLAT hardware picker ----
  # Baseline card = Sound Blaster (index 1), so the picker opens ON it, tagged
  # "(current)". Do NOT press Home inside the picker -- the start row is derived
  # from the live cfg and Home would jump off it.
  send_keys Home Down Down; sleep 0.4   # row2 Select Music Card
  send_keys Return; sleep 1             # -> the flat card picker
  shoot "20-musiccard-picklist"        # Auto-detect/Sound Blaster (OPL3 FM)/AdLib/WB/GenMIDI/Gravis
  send_keys Escape; sleep 0.8           # ESC = abandon, NO cfg write
  shoot "21-musiccard-esc-backout"     # Sound menu unchanged
  send_keys Return; sleep 1             # re-open the picker
  send_keys Return; sleep 1.2           # pick Sound Blaster (current) -> BLASTER hardware
  shoot "30-sound-hardware"            # the SB Port/IRQ/DMA screen, walked by the pick
  send_keys Home; shoot "31-hw-ioport-row"
  send_keys Return; sleep 0.8           # Enter opens the I/O-port pick-list
  shoot "35-hw-ioport-picklist"
  send_keys Escape; sleep 0.4
  send_keys Down Down Down Down; sleep 0.4   # row0 -> row4 (Card type)
  shoot "33-hw-cardtype-traditional-name"
  send_keys Escape; sleep 0.8           # leave hardware -> v2 TEST-AFTER-PICK prompt
  shoot "36-test-after-pick-prompt"    # "Test music now?" (default Yes)
  send_keys n; sleep 0.8                # decline -> back to the Sound menu
  shoot "37-after-pick-back-to-sound"

  # --- Music Options (Sound row4): adaptive rows ---------------------------
  # Under Type=MIDI (opl3): MIDI music set is LIVE (v2 Q2 fix -- it is now gated
  # on Type==MIDI, so it shows for GUS too, not just wb/opl3/auto); Organya
  # pre-render is greyed; Audio quality is live (it governs the SFX mix rate on
  # the SB family, which is why v2 does NOT make it Organya-only).
  send_keys Home Down Down Down Down Return; sleep 1   # row4 Music Options
  shoot "22-musicopts-midi"            # pre-render greyed, quality live
  send_keys Escape; sleep 0.6           # browse-only -> silent ESC

  # Now flip to Organya and re-open: the pre-render row must UN-GREY. Cycling to
  # Organya must NOT pop a hardware screen (v2 grammar: a cycle row changes the
  # value in place) -- if a BLASTER screen appears here, that is a regression.
  send_keys Home Down; sleep 0.3        # row1 Music Type
  send_keys Left; sleep 0.6             # MIDI -> Organya (Left = -1)
  shoot "23-musictype-organya-nomodal"  # MUST still be the Sound menu, no modal
  # Under Organya the CARD row (row2) is greyed and nav SKIPS it, so row1 -> Down
  # lands on row3 (Select Sound FX Card) and a second Down reaches row4 (Music
  # Options). Down x3 would overshoot onto Test music and PLAY it -- which is
  # exactly what the first run of this walk did. Grey-skip changes the counts.
  send_keys Down Down; sleep 0.4        # row1 -> row3 -> row4 Music Options
  send_keys Return; sleep 1
  shoot "24-musicopts-organya"         # Organya pre-render row now LIVE
  send_keys Escape; sleep 0.6
  send_keys Home Down; sleep 0.3        # back to Music Type
  send_keys Right; sleep 0.6            # Organya -> MIDI (restore the baseline)
  send_keys Escape; sleep 0.5           # Sound menu -> main

  # --- System speed (idx1): DF-style pick-list ----------------------------
  send_keys Home Down Down Return; sleep 1   # main idx2 System speed -> pick-list
  shoot "55-system-speed-picklist"      # Slow/Normal/Fast/Very Fast/Auto-detect + (recommended) + DESCRIPTION
  send_keys Escape; sleep 0.5           # ESC = cancel, no change -> main

  # --- Input submenu (idx2) -----------------------------------------------
  # T47 shell: row0 Joystick on/off (live), rows 1-3 (Configure keyboard /
  # joystick / Restore defaults) GREYED as Phase-3 signposts, row4 Back. ESC
  # backs out silently (live edit kept).
  send_keys Home Down Down Down Return; sleep 1   # main idx3 Input
  shoot "50-input-submenu"              # row0 Joystick + greyed Configure/Restore rows
  send_keys space; sleep 0.3; shoot "51-input-joystick-on"   # toggle Joystick On
  send_keys space; sleep 0.3            # toggle back Off (baseline)
  send_keys Escape; sleep 0.5           # submenu -> main (silent)

  # --- Advanced / troubleshooting (idx3): PERF rows folded in -------------
  # T47: PERF_MODE + FIXED_TIMESTEP are now the first two rows here (Performance
  # is gone as a top-level item), followed by the compat rows.
  send_keys Home Down Down Down Down Return; sleep 1   # main idx4 Advanced
  shoot "70-advanced"                   # row0 PERF_MODE + per-line help
  send_keys Down; sleep 0.3; shoot "71-advanced-row1-help"   # row1 FIXED_TIMESTEP
  send_keys Escape; sleep 0.5           # browse-only -> silent ESC

  # --- Auto-detect best settings (idx4): modal message box ----------------
  send_keys Home Return; sleep 1   # main idx0 Auto-detect (v2: FIRST item)
  shoot "80-autodetect-port"; send_keys Return; sleep 0.4

  # --- R-I Enter-opens-pick-list demo (editing-screen) --------------------
  # On Advanced (idx3) Enter opens a modal pick-list for the highlighted row;
  # Space/Left/Right still cycle in place. ESC closes the list with a full clean
  # redraw (R-H -- no overlay/texture residue).
  send_keys Home Down Down Down Down Return; sleep 1   # Advanced (idx4)
  send_keys Home; sleep 0.3             # row0 PERF_MODE; status bar "Enter Open list ..."
  shoot "85-advanced-perf-row"
  send_keys Return; sleep 0.5           # Enter opens the PERF_MODE pick-list
  shoot "86-advanced-perf-picklist"     # 0 Faithful / 1 Smooth / 2 Fast popup
  send_keys Escape; sleep 0.5           # close list -> clean redraw (R-H), no residue
  shoot "87-advanced-after-picklist-clean"
  send_keys Escape; sleep 0.5           # no edit -> silent back to main

  # --- T44 session-edit model demo: edit -> ESC KEEPS -> re-enter shows the
  #     kept edit -> Save and exit highlight. Uses Advanced (idx3). We do NOT
  #     press Save and exit -- the window is left live + unsaved; ensure_stage
  #     rewrites the baseline each run, so the staged CFG is untouched.
  send_keys Home Down Down Down Down Return; sleep 1   # Advanced (idx4)
  send_keys Home Right; sleep 0.3       # row0 PERF_MODE: an edit
  shoot "90-sessionedit-made"           # the edit, in-screen
  send_keys Escape; sleep 0.5           # ESC (changed) -> "Save setting?" modal
  shoot "90b-save-setting-prompt"       # T52: "Save setting?" (default Yes)
  send_keys Return; sleep 0.5           # Return = default Yes = KEEP -> main menu
  shoot "91-sessionedit-menu-unsaved"   # main menu status now shows "* UNSAVED"
  send_keys Home Down Down Down Down Return; sleep 1   # re-enter Advanced (idx4)
  send_keys Home; sleep 0.3             # row0 PERF_MODE
  shoot "92-sessionedit-kept"           # PERF_MODE STILL changed -> edit KEPT across ESC
  send_keys Escape; sleep 0.5           # re-enter was view-only -> silent ESC
  # v2: idx5 = "Save and run DOSKUTSU" (new), idx6 = "Save and exit".
  send_keys Home Down Down Down Down Down; sleep 0.3  # idx5 = "Save and run DOSKUTSU"
  shoot "95-save-and-run-item"          # the new Save-and-run row (ERRORLEVEL 10 chain)
  send_keys Down; sleep 0.3                           # idx6 = "Save and exit"
  shoot "93-sessionedit-save-and-exit"  # "Save and exit" highlighted (* UNSAVED in status)
  # T52 QUIT GUARD: ESC at the main menu ALWAYS pops "Quit without saving
  # settings?" (default NO), even when clean -- capture it, then ESC again = No =
  # STAY (a second ESC resolves to No, does NOT exit). Leaves the window live.
  send_keys Escape; sleep 0.5           # ESC at main menu -> quit guard
  shoot "94-quit-without-saving-prompt" # T52: "Quit without saving settings?" (default No)
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
