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
#   - SOUND HUB (#9 Type-first, screen_sound_menu ROW_* enum) -- 0 Express setup,
#     1 Select Music Type, 2 Select Sound FX Device, 3 Music options, 4 Test
#     sound effects, 5 Test music, 6 Back. Its ESC backs out SILENTLY. There is
#     NO "Custom setup" row: the BLASTER hardware screen is reached INLINE from
#     whichever picker puts the SB into use (an SB-family MIDI synth, Organya,
#     Auto-detect, or "Sound Blaster" in the FX picker).
#   - Row 1 is TWO levels: Select Music Type (Organya / MIDI / Auto-detect / No
#     Music) -> picking MIDI opens Select MIDI Synth (OPL3 FM / WaveBlaster /
#     General MIDI / AdLib / Gravis). ESC at EITHER level abandons with no cfg
#     write. Both pickers START on the row derived from the live cfg (the
#     "(current)" tag), so this walk never sends Home inside them. "Performance" is GONE: PERF_MODE
#     + FIXED_TIMESTEP are the first two rows of Advanced (idx3). "Input" (idx2)
#     is a submenu: row0 Joystick on/off (live), rows 1-3 GREYED (Phase 3), 4
#     Back; ESC silent. "System speed" (idx1) is a tui_picklist popup.
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

  # --- Sound submenu (idx0) -----------------------------------------------
  send_keys Home Return; sleep 1        # main idx0 Sound -> submenu
  shoot "10-sound-submenu"             # Express/Custom/Music+volumes/Test x2/Back + badges

  # --- Express setup (Sound submenu row0): DF-UX Phase 2 one-key detect ----
  # T-DFUX-P2: red warning modal (DF 3328) -> re-run probes (brief video-bench
  # flash) -> evidence modal w/ DSP version (DF 3329) -> "Test it now?" Y/N. We
  # decline the test (n) so the walk stays deterministic; Express has written the
  # detected BLASTER + opl3 to the session (review only -- never saved).
  send_keys Home Return; sleep 1        # submenu row0 Express -> red DANGER modal
  shoot "12-express-warning"           # DF 3328 red "DETECTING HARDWARE" modal
  send_keys Return; sleep 2             # dismiss -> re-run probes (mode flash) -> evidence
  shoot "13-express-evidence"          # DF 3329 "DETECTION COMPLETE" (DSP version found)
  send_keys Return; sleep 0.6           # dismiss -> "Test it now?" Yes/No prompt
  shoot "14-express-test-prompt"
  send_keys n; sleep 0.6                # decline -> back to the Sound submenu
  shoot "15-express-back-to-submenu"

  # --- Inline audio tests (Sound submenu rows 3,4): LIVE popup (AUDIOTEST=1) --
  # CAPTURED FIRST, on the clean opl3 baseline (edits are sticky; run before the
  # destructive Music-and-volumes note-demo). T47: the tests are inline rows now
  # (the separate chooser was retired) -- Enter on a test row opens the device +
  # auto-plays, then "Did you hear it?" Yes/No (default-Yes; y/n shortcuts).
  # #9 SOUND-HUB REORDER: the submenu is now { 0 Express, 1 Select Music Card,
  # 2 Select Sound FX Device, 3 Music options, 4 Test sound effects, 5 Test
  # music, 6 Back } (screen_sound_menu ROW_* enum). The test rows shifted +1 vs
  # the old { ...2 Music+volumes, 3 Test SFX, 4 Test music } -- so Test SFX is
  # ROW_TESTSFX=4 (Down x4 from Home), Test music ROW_TESTMUS=5 (one more Down).
  send_keys Home Down Down Down Down; sleep 0.5   # submenu ROW_TESTSFX=4 Test sound effects
  send_keys Return; sleep 1.5           # run SFX test -> "Playing..." popup auto-plays
  shoot "40-test-sfx-playing"
  send_keys y; sleep 0.8                 # 'y' = Yes -> back to submenu (row4 badge)
  shoot "41-test-sfx-badge-working"
  send_keys Down; sleep 0.3             # submenu ROW_TESTMUS=5 Test music
  send_keys Return; sleep 1.0           # run music test (real Title theme via OPL3, until-key)
  shoot "42-test-music-playing"
  send_keys space; sleep 0.5            # stop the until-key play -> "Did you hear it?"
  send_keys y; sleep 0.8                 # 'y' = Yes -> back to submenu (row4 badge)
  shoot "43-test-music-badge-working"

  # --- #9 TYPE-FIRST music selection (submenu ROW_MUSTYPE=1) ---------------
  # The flat "Select Music Card" list is GONE. Row 1 is now "Select Music Type":
  #   Select Music Type   rows: 0 Organya, 1 MIDI, 2 Auto-detect, 3 No Music
  #     -> picking MIDI opens "Select MIDI Synth":
  #        rows: 0 OPL3 FM (Sound Blaster 16/Pro), 1 WaveBlaster daughterboard,
  #              2 General MIDI (external module), 3 AdLib (OPL2, music only),
  #              4 Gravis UltraSound
  # NO Home inside either picker: the start row is DERIVED from the live cfg
  # (music_type_of / midi_synth_index) and carries the "(current)" tag -- Home
  # would jump off it to row 0 and desync the fixed Down-counts below. With the
  # deterministic baseline AUDIO_BACKEND=opl3: type picker starts on MIDI (row1,
  # "(current)"), synth picker starts on OPL3 FM (row0, "(current)").
  send_keys Home Down Return; sleep 1   # submenu row1 -> Select Music Type picker
  shoot "16-music-type-picklist"        # Organya/MIDI/Auto-detect/No Music; MIDI (current)
  send_keys Up; sleep 0.4               # row1 MIDI -> row0 Organya
  shoot "17-music-type-organya-row"     # Organya desc: "No sound card needed" + pre-render note
  send_keys Down; sleep 0.4             # back to row1 MIDI

  # (a) MIDI -> ESC = abandon at the SECOND level: no cfg write at all. The hub
  #     banner must still read "MIDI: OPL3 FM" after backing out.
  send_keys Return; sleep 0.8           # MIDI -> Select MIDI Synth picker
  shoot "18-midi-synth-picklist"        # OPL3 FM (current) / WB / GenMIDI / AdLib / Gravis
  send_keys Escape; sleep 0.8           # ESC in the synth picker -> abandon, NO change
  shoot "19-midi-synth-esc-backout"     # back at the hub; banner unchanged

  # (b) MIDI -> OPL3 FM: writes AUDIO_BACKEND=opl3 (already current -> not dirty)
  #     and walks the SB-family sub-screen, which is how the BLASTER hardware
  #     screen is reached (there is no standalone "Custom setup" hub row).
  #     Rows there (R-M removed the override row): 0 I/O port, 1 IRQ, 2 DMA,
  #     3 MPU-401 port, 4 Card type, then the R-B live rows 5 Sound, 6 SFX
  #     volume, 7 Music volume. R-I: Enter on a BLASTER field OPENS a DF
  #     pick-list (detected tagged, Other... on A / P); Right/Left cycle in place
  #     + write the composed BLASTER live. ESC backs out SILENTLY (edits live).
  send_keys Home Down Return; sleep 1   # submenu row1 -> type picker (MIDI current)
  send_keys Return; sleep 0.8           # MIDI -> synth picker (OPL3 FM current, row0)
  send_keys Return; sleep 1             # pick OPL3 FM -> screen_hardware() opens
  shoot "30-sound-hardware"             # the real BLASTER hardware screen (Port/IRQ/DMA/Type)
  send_keys Home; shoot "31-hw-ioport-row"         # row0 I/O port + DESCRIPTION
  send_keys Return; sleep 0.8           # R-I: Enter opens the I/O port pick-list
  shoot "35-hw-ioport-picklist"         # 0x220 (detected) + Other... popup, dimmed backdrop
  send_keys Escape; sleep 0.4           # close the pick-list (no change), back on row0
  send_keys Down Down Down Down; sleep 0.4         # row0 -> row4 (Card type)
  shoot "33-hw-cardtype-traditional-name"          # "Sound Blaster 16 (T6)" + help
  send_keys Escape; sleep 0.5           # browse + ESC -> submenu (silent)
  shoot "34-hw-esc-clean"

  # (c) Organya: the type pick writes AUDIO_BACKEND=organya, walks the BLASTER
  #     screen (Organya renders to the SB DAC), then runs the silent
  #     recommend_org_prerender() auto-suggest -- no modal, it just sets
  #     ORG_PRERENDER=1 on a sub-Pentium CPU. Under this conf DOSBox profiles as
  #     sub-586, so it DOES fire: shot 20 shows the pre-render row flipped from
  #     the baseline CFG's 0 to "On". This is the path that un-greys that row.
  send_keys Home Down Return; sleep 1   # submenu row1 -> type picker (MIDI current, row1)
  send_keys Up; sleep 0.4               # row0 Organya
  send_keys Return; sleep 1.2           # pick Organya -> screen_hardware()
  shoot "36-organya-hardware"           # the SB screen, walked by the Organya pick
  send_keys Escape; sleep 0.8           # ESC (browse-only) -> silent back to the hub
  shoot "37-organya-hub-banner"         # hub banner + row value now read "Organya"

  # --- Music options (submenu ROW_MUSOPTS=3) -------------------------------
  # THE UX WIN: the Organya pre-render row is now ACTIVE (un-greyed) because the
  # type pick above set backend=organya. Under the old flat picker Organya was
  # buried as a peer card, so this row stayed greyed and undiscoverable.
  # Rows: GUS voices + GUS hi-fi (gus only -- not visible here), MIDI music set
  # (only with >=2 sets installed), Organya pre-render, Audio quality.
  send_keys Home Down Down Down Return; sleep 1   # submenu row3 -> Music options
  shoot "20-music-options-organya"      # pre-render row SELECTABLE + its note
  send_keys Down; sleep 0.3
  shoot "21-music-options-quality"      # Audio quality row, "11025Hz"
  # Leave a REAL net change (no Left back): screen_sound's ESC path compares an
  # entry snapshot (scfg_differs), so a net-zero toggle would ESC SILENTLY and a
  # trailing Return would land on the hub and re-open a screen. One Right =
  # genuinely changed -> the T52 "Save setting?" modal is deterministic.
  send_keys Right; sleep 0.3
  shoot "22-music-options-quality-22050hz"
  send_keys Escape; sleep 0.6           # ESC (changed) -> "Save setting?" (default Yes)
  shoot "23-music-options-save-prompt"
  send_keys Return; sleep 0.6           # Return = Yes = keep -> Sound submenu

  # (d) Auto-detect type: writes AUDIO_BACKEND=auto + walks the BLASTER screen.
  send_keys Home Down Return; sleep 1   # submenu row1 -> type picker (Organya current, row0)
  send_keys Down Down; sleep 0.4        # row0 Organya -> row2 Auto-detect
  shoot "24-music-type-autodetect-row"  # "Detect installed sound hardware."
  send_keys Return; sleep 1.2           # pick Auto-detect -> screen_hardware()
  send_keys Escape; sleep 0.8           # ESC -> back to the hub
  shoot "25-music-type-auto-banner"     # hub banner + row value now read "Auto-detect"

  send_keys Escape; sleep 0.5           # Sound submenu -> main

  # --- System speed (idx1): DF-style pick-list ----------------------------
  send_keys Home Down Return; sleep 1   # main idx1 System speed -> pick-list
  shoot "55-system-speed-picklist"      # Slow/Normal/Fast/Very Fast/Auto-detect + (recommended) + DESCRIPTION
  send_keys Escape; sleep 0.5           # ESC = cancel, no change -> main

  # --- Input submenu (idx2) -----------------------------------------------
  # T47 shell: row0 Joystick on/off (live), rows 1-3 (Configure keyboard /
  # joystick / Restore defaults) GREYED as Phase-3 signposts, row4 Back. ESC
  # backs out silently (live edit kept).
  send_keys Home Down Down Return; sleep 1
  shoot "50-input-submenu"              # row0 Joystick + greyed Configure/Restore rows
  send_keys space; sleep 0.3; shoot "51-input-joystick-on"   # toggle Joystick On
  send_keys space; sleep 0.3            # toggle back Off (baseline)
  send_keys Escape; sleep 0.5           # submenu -> main (silent)

  # --- Advanced / troubleshooting (idx3): PERF rows folded in -------------
  # T47: PERF_MODE + FIXED_TIMESTEP are now the first two rows here (Performance
  # is gone as a top-level item), followed by the compat rows.
  send_keys Home Down Down Down Return; sleep 1
  shoot "70-advanced"                   # row0 PERF_MODE + per-line help
  send_keys Down; sleep 0.3; shoot "71-advanced-row1-help"   # row1 FIXED_TIMESTEP
  send_keys Escape; sleep 0.5           # browse-only -> silent ESC

  # --- Auto-detect best settings (idx4): modal message box ----------------
  send_keys Home Down Down Down Down Return; sleep 1
  shoot "80-autodetect-port"; send_keys Return; sleep 0.4

  # --- R-I Enter-opens-pick-list demo (editing-screen) --------------------
  # On Advanced (idx3) Enter opens a modal pick-list for the highlighted row;
  # Space/Left/Right still cycle in place. ESC closes the list with a full clean
  # redraw (R-H -- no overlay/texture residue).
  send_keys Home Down Down Down Return; sleep 1   # Advanced (idx3)
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
  send_keys Home Down Down Down Return; sleep 1   # Advanced (idx3)
  send_keys Home Right; sleep 0.3       # row0 PERF_MODE: an edit
  shoot "90-sessionedit-made"           # the edit, in-screen
  send_keys Escape; sleep 0.5           # ESC (changed) -> "Save setting?" modal
  shoot "90b-save-setting-prompt"       # T52: "Save setting?" (default Yes)
  send_keys Return; sleep 0.5           # Return = default Yes = KEEP -> main menu
  shoot "91-sessionedit-menu-unsaved"   # main menu status now shows "* UNSAVED"
  send_keys Home Down Down Down Return; sleep 1   # re-enter Advanced
  send_keys Home; sleep 0.3             # row0 PERF_MODE
  shoot "92-sessionedit-kept"           # PERF_MODE STILL changed -> edit KEPT across ESC
  send_keys Escape; sleep 0.5           # re-enter was view-only -> silent ESC
  send_keys Home Down Down Down Down Down; sleep 0.3  # idx5 = "Save and exit"
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
