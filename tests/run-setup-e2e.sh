#!/usr/bin/env bash
#
# tests/run-setup-e2e.sh -- end-to-end SETUP.EXE -> DOSKUTSU.EXE config test.
#
# Fully headless (own Xvfb + DOSBox-X). For each config scenario it:
#
#   1. Writes a KNOWN-baseline DOSKUTSU.CFG into the stage so SETUP loads
#      deterministic start values (SETUP only seeds recommendations when NO
#      config file exists -- see setup/main.c; with a file present, INT/ENUM
#      fields have a known starting value, which is what makes the relative
#      Left/Right cycling deterministic).
#   2. Launches the REAL SETUP.EXE and drives its CP437 TUI via xdotool
#      keystrokes (operator's explicit choice -- no batch/CLI mode): navigate
#      the menus, change the target fields, Save and exit.
#   3. DETERMINISTIC VERIFY (does NOT depend on UI timing): parses the written
#      DOSKUTSU.CFG and asserts the intended KEY=VALUE lines are present (and a
#      couple of untouched keys retain their baseline -- catches stray edits).
#   4. Launches DOSKUTSU.EXE with that CFG and asserts the engine startup
#      banners reflect each configured value (the cross-check that the file
#      SETUP wrote actually drives the engine). Reads DEBUG.LOG + SDLDBG.LOG.
#   5. AUDIO scenario: runs DOSKUTSU with DOSBox-X wave capture active, then
#      asserts the captured WAV is non-silent (RMS over a floor) and that the
#      audio-init banner names the configured backend.
#
# Only the keystroke injection is UI-timed; everything asserted is
# deterministic (CFG content + log banners), per grep_anchor_confound
# discipline (every stop/validity grep is anchored to a unique line FORMAT,
# not a substring a banner could also contain).
#
# Two-phase readiness:
#   - The SETUP -> DOSKUTSU.CFG half (steps 1-3) needs only build/setup/setup.exe
#     and runs standalone.
#   - The DOSKUTSU-banner + audio half (steps 4-5) needs build/doskutsu.exe
#     (nx-engine's config-loading 0216 build). When that binary is absent the
#     banner/audio half is SKIPPED with a loud note and the run still exercises
#     (and gates) the SETUP-driving + CFG-writing half. It auto-engages the
#     moment doskutsu.exe is present.
#
# Usage:
#   tests/run-setup-e2e.sh                  # all scenarios
#   tests/run-setup-e2e.sh --only A         # one scenario (A|B|C|AUDIO)
#   tests/run-setup-e2e.sh --out /tmp/foo   # artifact dir (default /tmp/setup-e2e)
#   tests/run-setup-e2e.sh --keep-going     # don't stop at first failing scenario
#   tests/run-setup-e2e.sh --display :77    # Xvfb display number to use
#
# Exit: 0 = every run scenario passed; non-zero = at least one failed (a clear
# per-assertion diff is printed).

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE_DIR="$REPO_ROOT/build/stage"
SETUP_EXE="$REPO_ROOT/build/setup/setup.exe"
DOSKUTSU_EXE="$REPO_ROOT/build/doskutsu.exe"
CWSDPMI_EXE="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF_PARITY="$REPO_ROOT/tools/dosbox-x.conf"
CONF_FAST="$REPO_ROOT/tools/dosbox-x-fast.conf"
# OPL3-enabled fast conf (generated in ensure_stage from CONF_FAST). The repo
# confs set oplmode=none; the OPL3 per-backend audio test needs DOSBox-X to
# emulate the YMF262 so SETUP's FM register writes (port 0x388) produce
# capturable output.
CONF_OPL3=""   # set in ensure_stage
# CURRENT_CONF tracks the conf the most recent launch_dosbox used, so kill_dosbox
# can scope its teardown to that exact instance (this suite never runs two DOSBox-X
# instances concurrently -- launch_dosbox kills any prior one first). Empty until the
# first launch, which makes the cleanup-trap teardown a safe no-op if we exit early.
CURRENT_CONF=""
# shellcheck source=../tools/dosbox-teardown.sh
source "$REPO_ROOT/tools/dosbox-teardown.sh"   # dbx_kill_conf -- conf-scoped teardown

OUT_DIR="/tmp/setup-e2e"
ONLY=""
KEEP_GOING=0
XDISPLAY=":77"

# Per-key xdotool delay (ms) and inter-step settle (s). Generous: SETUP's TUI
# is instant but DOSBox-X keyboard delivery under Xvfb wants slack.
KEY_DELAY_MS=120
STEP_SLEEP=0.6

while (($#)); do
  case "$1" in
    --only)       shift; ONLY="${1:-}" ;;
    --out)        shift; OUT_DIR="${1:-/tmp/setup-e2e}" ;;
    --keep-going) KEEP_GOING=1 ;;
    --display)    shift; XDISPLAY="${1:-:77}" ;;
    -h|--help)    sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "run-setup-e2e.sh: unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$OUT_DIR"
RESULTS="$OUT_DIR/results.txt"
: > "$RESULTS"

XVFB_PID=""
FAILED_SCENARIOS=()

log()  { echo "[setup-e2e] $*" | tee -a "$RESULTS"; }
pass() { echo "[setup-e2e]   PASS: $*" | tee -a "$RESULTS"; }
fail() { echo "[setup-e2e]   FAIL: $*" | tee -a "$RESULTS"; }

cleanup() {
  dbx_kill_conf "$CURRENT_CONF" >/dev/null 2>&1 || true
  pulse_sink_down 2>/dev/null || true
  if [[ -n "$XVFB_PID" ]]; then kill "$XVFB_PID" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Environment / staging
# ---------------------------------------------------------------------------

require_tools() {
  local missing=0 t
  for t in dosbox-x Xvfb xdotool ffmpeg; do
    command -v "$t" >/dev/null 2>&1 || { echo "missing tool: $t" >&2; missing=1; }
  done
  [[ "$missing" == "0" ]] || exit 3
}

start_xvfb() {
  if pgrep -x dosbox-x >/dev/null 2>&1; then
    echo "[setup-e2e] error: dosbox-x already running -- kill it first." >&2
    exit 4
  fi
  # Tear down any stale server on our display number.
  pkill -f "Xvfb $XDISPLAY " >/dev/null 2>&1 || true
  sleep 0.5
  Xvfb "$XDISPLAY" -screen 0 800x600x24 >"$OUT_DIR/xvfb.log" 2>&1 &
  XVFB_PID=$!
  sleep 2
  if ! kill -0 "$XVFB_PID" 2>/dev/null; then
    echo "[setup-e2e] error: Xvfb failed to start on $XDISPLAY (see $OUT_DIR/xvfb.log)" >&2
    exit 4
  fi
  log "Xvfb up on $XDISPLAY (PID $XVFB_PID)"
}

# Build SETUP.EXE and assemble the stage. We build the AUDIOTEST=1 variant --
# the SDL-linked live-audio build that `make dist` ships -- so the suite tests
# the binary the operator actually gets (and so the T5 audio-test scenario can
# bring up the SB16 device). The top-level `make setup` defaults to the no-SDL
# scaffold (sdl-engine gated SDL behind AUDIOTEST=1, b37a367), so we invoke the
# setup sub-make with AUDIOTEST=1 directly and stage THAT over whatever
# `make stage` installs. The CFG-writing scenarios (A-C) behave identically on
# either build; only T5 needs the SDL link.
ensure_stage() {
  # NOTE on ordering: `make stage` depends on the top-level `setup` target,
  # which builds the DEFAULT (scaffold) setup.exe into build/setup/. So we must
  # build the AUDIOTEST=1 variant AFTER `make stage`, or stage would clobber it.
  if [[ -f "$DOSKUTSU_EXE" ]]; then
    log "make stage (DOSKUTSU.EXE + data layout)..."
    make -C "$REPO_ROOT" stage >>"$RESULTS" 2>&1 || { echo "make stage failed" >&2; exit 5; }
  else
    log "NOTE: build/doskutsu.exe ABSENT -- engine-banner half will be SKIPPED."
    log "      Assembling SETUP-only stage for the config half."
    mkdir -p "$STAGE_DIR/DOSKUTSU"
    [[ -f "$CWSDPMI_EXE" ]] && install -m 0644 "$CWSDPMI_EXE" "$STAGE_DIR/CWSDPMI.EXE"
    if [[ -d "$REPO_ROOT/data" ]]; then
      rm -f "$STAGE_DIR/data" "$STAGE_DIR/DATA"
      ln -s "$REPO_ROOT/data" "$STAGE_DIR/data"
    fi
  fi
  # Build the AUDIOTEST=1 variant -- the SDL-linked live-audio build that
  # `make dist` ships -- so the suite tests the binary the operator gets (and
  # so the T5 audio-test scenario can bring up the SB16 device). Done LAST so
  # `make stage`'s scaffold build doesn't clobber it. CFG-writing scenarios
  # (A-C) behave identically on either build; only T5 needs the SDL link.
  log "make setup AUDIOTEST=1 (SDL-linked live-audio build -- the dist build)..."
  make -C "$REPO_ROOT/setup" clean >>"$RESULTS" 2>&1 || true
  make -C "$REPO_ROOT/setup" AUDIOTEST=1 >>"$RESULTS" 2>&1 || { echo "make setup AUDIOTEST=1 failed" >&2; exit 5; }
  [[ -f "$SETUP_EXE" ]] || { echo "missing $SETUP_EXE after make setup AUDIOTEST=1" >&2; exit 5; }
  # Confirm it really is the SDL-linked build (two-witness: scaffold string absent).
  if strings "$SETUP_EXE" | grep -q 'not yet linked'; then
    echo "ERROR: staged SETUP.EXE is the scaffold build (AUDIOTEST=1 did not take)" >&2; exit 5
  fi
  mkdir -p "$STAGE_DIR/DOSKUTSU"
  install -m 0644 "$SETUP_EXE" "$STAGE_DIR/SETUP.EXE"
  [[ -f "$STAGE_DIR/SETUP.EXE" ]] || { echo "SETUP.EXE not staged" >&2; exit 5; }
  # OPL3-enabled conf for the FM per-backend audio test.
  CONF_OPL3="$OUT_DIR/dosbox-opl3.conf"
  sed 's/oplmode *= *none/oplmode    = opl3/' "$CONF_FAST" > "$CONF_OPL3"
  # Host OPC1 cache-fixture generator (setup/tests/gen-orgcache.c, committed
  # c49a488) so the BKORGCACHE scenario can stage a deterministic CURLY.PCM
  # without running the game. Built with the HOST cc, not DJGPP.
  GEN_ORGCACHE="$OUT_DIR/gen-orgcache"
  cc -O2 -o "$GEN_ORGCACHE" "$REPO_ROOT/setup/tests/gen-orgcache.c" -lm >>"$RESULTS" 2>&1 \
    || { echo "host build of gen-orgcache failed" >&2; exit 5; }
  # Default stage is cache-ABSENT so BKORG exercises the tone fallback
  # deterministically (a stale real CURLY.PCM can linger in build/stage from a
  # prior prerender run; remove it -- BKORGCACHE stages its own fixture).
  rm -f "$STAGE_DIR"/CACHE/*/CURLY.PCM 2>/dev/null || true
}

# org_cache_present / org_cache_absent -- control the organya prerender cache in
# the stage so the music test deterministically hits the loaded-snippet vs
# tone-fallback path (CACHE/11025_1/CURLY.PCM, the Tier-2 default tier).
org_cache_present() { "$GEN_ORGCACHE" "$STAGE_DIR" >>"$RESULTS" 2>&1; }
org_cache_absent()  { rm -f "$STAGE_DIR"/CACHE/*/CURLY.PCM 2>/dev/null || true; }

# ---------------------------------------------------------------------------
# DOSBox-X drive helpers
# ---------------------------------------------------------------------------

# launch_dosbox <conf> <extra -c args...> -- mount stage as C:, cwsdpmi as D:,
# replicate the launcher env, then run the trailing DOS command(s). Backgrounds
# DOSBox-X. Per-run stdout/stderr -> $DBX_LOG.
DBX_LOG="$OUT_DIR/dosbox.log"
launch_dosbox() {
  local conf="$1"; shift
  CURRENT_CONF="$conf"   # remember for kill_dosbox's conf-scoped teardown
  # CLAUDE.md: never run two DOSBox-X instances at once (mount-cache races +
  # ambiguous xdotool search). The SDL-linked SETUP.EXE can take several
  # seconds to fully exit; if a prior instance is still alive, a concurrent
  # mount of build/stage makes the just-written DOSKUTSU.CFG invisible to the
  # second instance (and can drop it on cache writeback). Guarantee a clean
  # slate before every launch.
  if pgrep -x dosbox-x >/dev/null 2>&1; then kill_dosbox; fi
  : > "$DBX_LOG"
  local args=(-conf "$conf" -nopromptfolder
    -c "MOUNT C $STAGE_DIR"
    -c "MOUNT D $REPO_ROOT/vendor/cwsdpmi"
    -c 'SET PATH=Z:\;C:\;D:\'
    -c 'SET BLASTER=A220 I5 D1 H5 T6'
    -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1'
    -c 'SET SDL_INVALID_PARAM_CHECKS=0'
    -c 'SET DOSKUTSU_LOG_VERBOSE=1'
    -c 'C:')
  args+=("$@")
  # When capturing audio (option b), route DOSBox-X's SDL2 audio output to the
  # PulseAudio null sink so the host can record what DOSBox plays. DOSBox-X is
  # SDL2, so SDL_AUDIODRIVER=pulseaudio + PULSE_SINK take effect.
  local aenv=()
  if [[ "$CAPTURE_AUDIO" == "1" && -n "$PULSE_MOD" ]]; then
    aenv=(SDL_AUDIODRIVER=pulseaudio "PULSE_SINK=$PULSE_SINK_NAME" "PULSE_SERVER=$PULSE_SERVER_ADDR")
  fi
  # Use `env` to apply the audio vars: env assignments expanded from a variable
  # are NOT treated as assignments by bash (they'd be read as the command name).
  DISPLAY="$XDISPLAY" env "${aenv[@]}" dosbox-x "${args[@]}" >>"$DBX_LOG" 2>&1 &
  # wait for process
  local i
  for i in $(seq 1 20); do
    pgrep -x dosbox-x >/dev/null 2>&1 && break
    sleep 0.5
  done
  pgrep -x dosbox-x >/dev/null 2>&1 || { fail "dosbox-x did not start"; return 1; }
  # focus the window
  for i in $(seq 1 10); do
    DISPLAY="$XDISPLAY" xdotool search --name DOSBox windowactivate --sync >/dev/null 2>&1 && break
    sleep 0.5
  done
  return 0
}

# Tear down DOSBox-X and BLOCK until the process is actually gone (poll, then
# SIGKILL escalate). A fixed `sleep 2` is not enough for the SDL-linked
# SETUP.EXE -- its SDL audio-device close lengthens shutdown, and a surviving
# instance races the next launch on the build/stage mount.
kill_dosbox() {
  dbx_kill_conf "$CURRENT_CONF" >/dev/null 2>&1 || true
  local i
  for i in $(seq 1 40); do
    pgrep -x dosbox-x >/dev/null 2>&1 || { sleep 0.5; return 0; }  # gone (+ fs settle)
    sleep 0.5
  done
  # still alive after ~20s -- force it
  dbx_kill_conf "$CURRENT_CONF" KILL >/dev/null 2>&1 || true
  sleep 1
}

shoot() {
  DISPLAY="$XDISPLAY" scrot -u "$OUT_DIR/$1.png" >/dev/null 2>&1 || true
}

# send_keys <k1> <k2> ... -- each is an xdotool keysym (Up/Down/Left/Right/
# Return/Escape). Sent individually with a settle between, for TUI redraw.
send_keys() {
  local k
  for k in "$@"; do
    DISPLAY="$XDISPLAY" xdotool key --clearmodifiers --delay "$KEY_DELAY_MS" "$k" >/dev/null 2>&1
    sleep 0.15
  done
}

# rep <n> <key> -- echo <key> n times (for building sequences).
rep() { local n="$1" k="$2"; local i; for ((i=0;i<n;i++)); do printf '%s\n' "$k"; done; }

# ---------------------------------------------------------------------------
# Baseline CFG (known start values so relative cycling is deterministic)
# ---------------------------------------------------------------------------

write_baseline_cfg() {
  # Registry defaults (see include/doskutsu_config_keys.h). Written CRLF; the
  # SETUP loader is CR-tolerant. AUDIO_BACKEND=auto is a valid enum value
  # (index 0) so the start index is known.
  local f="$STAGE_DIR/DOSKUTSU.CFG"
  {
    printf '; baseline written by run-setup-e2e.sh\r\n'
    printf 'AUDIO_BACKEND=auto\r\n'
    printf 'AUDIO_OFF=0\r\n'
    printf 'AUDIO_TIER2=1\r\n'
    printf 'SB16_VOICE_VOL=31\r\n'
    printf 'SB16_FM_VOL=28\r\n'
    printf 'ORG_PRERENDER=0\r\n'
    printf 'USE_JOYSTICK=0\r\n'
    printf 'PERF_MODE=0\r\n'
    printf 'FIXED_TIMESTEP=1\r\n'
    printf 'AUDIO_WB_DIRECT_PORT=1\r\n'
    printf 'DIRTY_RECTS=1\r\n'
    printf 'PIXEL_FORMAT_8=1\r\n'
    printf 'FORCE_PUMP_YIELD=0\r\n'
    printf 'THRASH_FULLCOVER=1\r\n'
  } > "$f"
}

# ---------------------------------------------------------------------------
# Deterministic CFG-content assertions
# ---------------------------------------------------------------------------

# CR-stripped view of the written CFG for grepping.
cfg_clean() { tr -d '\r' < "$STAGE_DIR/DOSKUTSU.CFG"; }

# assert_cfg_line <KEY> <VALUE> -- an UNCOMMENTED `KEY=VALUE` line must exist.
assert_cfg_line() {
  local key="$1" val="$2"
  if cfg_clean | grep -qE "^${key}=${val}$"; then
    pass "CFG has ${key}=${val}"
    return 0
  fi
  fail "CFG missing ${key}=${val} (got: $(cfg_clean | grep -E "^${key}=" || echo '<none>'))"
  return 1
}

# assert_cfg_absent_uncommented <KEY> -- no uncommented KEY= line (e.g.
# AUDIO_BACKEND=auto is emitted as a comment, so the live line must be absent).
assert_cfg_absent_uncommented() {
  local key="$1"
  if cfg_clean | grep -qE "^${key}="; then
    fail "CFG unexpectedly has uncommented ${key}= ($(cfg_clean | grep -E "^${key}="))"
    return 1
  fi
  pass "CFG has no uncommented ${key}= line (as expected)"
  return 0
}

# ---------------------------------------------------------------------------
# Engine-log banner assertions (the cross-check; needs build/doskutsu.exe)
# ---------------------------------------------------------------------------

DEBUG_LOG="$OUT_DIR/debug.log"
SDLDBG_LOG="$OUT_DIR/sdldbg.log"
# SETUP.EXE runs from the stage ROOT (build/stage/SETUP.EXE), so its startup
# profile dump lands at build/stage/LOGS/PROFILE.LOG (round-5/T27 add-on
# 8b1cdc8: written every launch, truncated fresh, one key=value per line, each
# fflush+fsync'd). realhw's T30 calibration consumes this on g2k.
PROFILE_LOG="$OUT_DIR/profile.log"
# SETUP's audio-test TRACE lands at build/stage/LOGS/SETUPDBG.LOG (audiotest_sdl.c,
# stage-root LOGS\). This carries the organya-snippet witnesses -- "org: loaded
# CURLY snippet ..." (cache present) vs "org: no pre-rendered cache ... -> tone
# fallback" (T36). Collected + folded into the assert_banner search set.
SETUP_LOG="$OUT_DIR/setupdbg.log"

collect_logs() {
  : > "$DEBUG_LOG"; : > "$SDLDBG_LOG"
  cp "$STAGE_DIR/LOGS/DEBUG.LOG"          "$DEBUG_LOG"  2>/dev/null \
    || cp "$STAGE_DIR/DEBUG.LOG"          "$DEBUG_LOG"  2>/dev/null || true
  cp "$STAGE_DIR/DOSKUTSU/LOGS/SDLDBG.LOG" "$SDLDBG_LOG" 2>/dev/null \
    || cp "$STAGE_DIR/DOSKUTSU/SDLDBG.LOG"  "$SDLDBG_LOG" 2>/dev/null || true
  # SETUP's profile dump (stage root LOGS\). Best-effort: absent on a DOSKUTSU-
  # only scenario or a SETUP launch failure (the assert guards on emptiness).
  : > "$PROFILE_LOG"
  cp "$STAGE_DIR/LOGS/PROFILE.LOG"         "$PROFILE_LOG" 2>/dev/null || true
  # SETUP audio-test trace (the org-snippet witnesses live here, NOT in SDLDBG).
  : > "$SETUP_LOG"
  cp "$STAGE_DIR/LOGS/SETUPDBG.LOG"        "$SETUP_LOG" 2>/dev/null || true
}

clear_logs() {
  rm -f "$STAGE_DIR/LOGS/DEBUG.LOG" "$STAGE_DIR/DEBUG.LOG" \
        "$STAGE_DIR/DOSKUTSU/LOGS/SDLDBG.LOG" "$STAGE_DIR/DOSKUTSU/SDLDBG.LOG" \
        "$STAGE_DIR/LOGS/PROFILE.LOG" "$STAGE_DIR/LOGS/SETUPDBG.LOG" 2>/dev/null || true
  # Also truncate the collected copies so a bailed run (e.g. launch failure)
  # can't let banner asserts match a PRIOR run's logs (stale false-pass guard).
  : > "$DEBUG_LOG"; : > "$SDLDBG_LOG"; : > "$PROFILE_LOG"; : > "$SETUP_LOG"
}

# assert_banner <human-label> <ERE> -- ERE must match >=1 line across the engine
# logs (DEBUG/SDLDBG) OR SETUP's audio-test trace (SETUPDBG -- carries the org
# snippet witnesses).
assert_banner() {
  local label="$1" re="$2"
  if grep -hE "$re" "$DEBUG_LOG" "$SDLDBG_LOG" "$SETUP_LOG" >/dev/null 2>&1; then
    pass "banner [$label]: /$re/"
    return 0
  fi
  fail "banner [$label] NOT found: /$re/"
  return 1
}

# assert_banner_absent <human-label> <ERE> -- ERE must match NO line. Used when
# a lever's "took effect" witness is the disappearance of the default-path
# banner (e.g. USE_JOYSTICK=1 removes the "Joystick: skipped" line; there is no
# positive banner when no joystick device is present, as under DOSBox-X).
assert_banner_absent() {
  local label="$1" re="$2"
  if grep -hE "$re" "$DEBUG_LOG" "$SDLDBG_LOG" "$SETUP_LOG" >/dev/null 2>&1; then
    fail "banner [$label] unexpectedly PRESENT: /$re/"
    return 1
  fi
  pass "banner [$label] absent (as expected): /$re/"
  return 0
}

# assert_profile_mhz -- SETUP wrote LOGS\PROFILE.LOG at startup and it carries a
# well-formed cpu_mhz_est line (round-5/T27 add-on; realhw's T30 calibration
# witness). DETERMINISTIC, emulator-safe: we assert the line EXISTS and is a
# non-negative integer, NOT a specific value -- the MHz under DOSBox-X's
# emulated CPU is meaningless (a real DX2-66 reads ~3 raw; the 486 divisor is a
# real-HW-only calibration, [[dosbox_not_proxy]]). Anchored to the unique line
# FORMAT '^cpu_mhz_est=<digits>$' (not a substring), per the calib-anchor lesson.
assert_profile_mhz() {
  if [[ ! -s "$PROFILE_LOG" ]]; then
    fail "profile-log: LOGS\\PROFILE.LOG missing/empty after SETUP launch"
    return 1
  fi
  if grep -qE '^cpu_mhz_est=[0-9]+$' "$PROFILE_LOG"; then
    pass "profile-log: cpu_mhz_est present + well-formed ($(grep -m1 '^cpu_mhz_est=' "$PROFILE_LOG"))"
    return 0
  fi
  fail "profile-log: no well-formed '^cpu_mhz_est=<int>' line in LOGS\\PROFILE.LOG"
  return 1
}

# assert_profile_video_speed -- DF-UX Phase 2: SETUP's startup profile bench
# wrote a well-formed video_speed_kbs line. DETERMINISTIC, emulator-safe: assert
# the line EXISTS + is a non-negative integer, NOT a value -- the KB/s under
# DOSBox-X is meaningless (g2k-only, [[dosbox_not_proxy]]); 0 is a legitimate
# "bench could not run" reading and still matches. Anchored to the unique line
# FORMAT '^video_speed_kbs=<digits>$' (not a substring), per the calib-anchor
# lesson. Pairs with the video_speed_path line (which fallback measured it).
assert_profile_video_speed() {
  if [[ ! -s "$PROFILE_LOG" ]]; then
    fail "profile-log: LOGS\\PROFILE.LOG missing/empty after SETUP launch"
    return 1
  fi
  if grep -qE '^video_speed_kbs=[0-9]+$' "$PROFILE_LOG"; then
    pass "profile-log: video_speed_kbs present + well-formed ($(grep -m1 '^video_speed_kbs=' "$PROFILE_LOG"); $(grep -m1 '^video_speed_path=' "$PROFILE_LOG"))"
    return 0
  fi
  fail "profile-log: no well-formed '^video_speed_kbs=<int>' line in LOGS\\PROFILE.LOG"
  return 1
}

# ---------------------------------------------------------------------------
# Scenario runner
# ---------------------------------------------------------------------------
# Each scenario function sets:
#   SCN_KEYS=(...)        keystroke sequence to drive SETUP
#   then calls cfg asserts + (if doskutsu present) banner asserts.
# Returns 0 on all-pass.

SCN_RC=0
note_fail() { SCN_RC=1; }

# run_setup_phase <name> [keep_cfg] -- write a known-baseline CFG (so relative
# INT/ENUM cycling is deterministic), launch SETUP, drive SCN_KEYS, exit. Pass
# "keep_cfg" to use a CFG the caller already wrote (e.g. the Sound Hardware
# scenario seeds a specific BLASTER for the screen to start from).
run_setup_phase() {
  local name="$1" keep_cfg="${2:-}"
  [[ "$keep_cfg" == "keep_cfg" ]] || write_baseline_cfg
  # Drop any prior PROFILE.LOG so the profile assert can't match a stale run
  # (SETUP truncates "wb" each launch, but guard the launch-failure path too).
  rm -f "$STAGE_DIR/LOGS/PROFILE.LOG" 2>/dev/null || true
  log "[$name] launching SETUP.EXE..."
  launch_dosbox "$CONF_FAST" -c 'SETUP.EXE' || { note_fail; return 1; }
  sleep 8                      # SETUP boot (profile_detect + SDL3 link init -- the
                               # AUDIOTEST=1 build is ~2 MB and boots slower)
  shoot "${name}-01-setup-main"
  # T44 model: SCN_KEYS end with main-menu F10 (Save and Exit) -- SETUP writes
  # DOSKUTSU.CFG then TERMINATES (no "Saved" modal). Give the write + exit a
  # moment to flush to the mounted stage before the shot + cp.
  send_keys "${SCN_KEYS[@]}"
  sleep 2
  shoot "${name}-02-after-save-exit"
  kill_dosbox
  log "[$name] SETUP exited; DOSKUTSU.CFG written."
  cp "$STAGE_DIR/DOSKUTSU.CFG" "$OUT_DIR/${name}.cfg" 2>/dev/null || true
  # Deterministic profile-dump witness: SETUP wrote LOGS\PROFILE.LOG at startup
  # with a well-formed cpu_mhz_est line (realhw T30 calibration source-of-truth).
  collect_logs
  assert_profile_mhz || note_fail
  assert_profile_video_speed || note_fail   # DF-UX Phase 2 startup bench witness
}

# run_doskutsu_phase <name> [capture] -- launch DOSKUTSU.EXE with the written
# CFG; collect logs. If "capture" passed, start wave capture for the audio test.
run_doskutsu_phase() {
  local name="$1" capture="${2:-}"
  clear_logs
  WAV_OUT=""
  if [[ "$capture" == "capture" ]]; then
    rm -rf "$OUT_DIR/cap"; mkdir -p "$OUT_DIR/cap"
    WAV_OUT="$OUT_DIR/cap/${name}.wav"
    if pulse_available && pulse_sink_up; then
      CAPTURE_AUDIO=1
      launch_dosbox "$CONF_PARITY" -c 'DOSKUTSU.EXE' || { CAPTURE_AUDIO=0; pulse_sink_down; note_fail; return 1; }
      sleep 6                                  # reach title (music playing)
      pulse_record 8 "$WAV_OUT"                # host capture window
      CAPTURE_AUDIO=0
      pulse_sink_down
    else
      log "  NOTE: PulseAudio sink unavailable -- skipping host capture this run."
      launch_dosbox "$CONF_PARITY" -c 'DOSKUTSU.EXE' || { note_fail; return 1; }
      sleep 8
    fi
  else
    launch_dosbox "$CONF_FAST" -c 'DOSKUTSU.EXE' || { note_fail; return 1; }
    sleep 8                                    # boot to title + emit banners
  fi
  shoot "${name}-03-doskutsu"
  kill_dosbox
  collect_logs
  log "[$name] DEBUG.LOG=$(wc -l <"$DEBUG_LOG" 2>/dev/null || echo 0) lines, SDLDBG.LOG=$(wc -l <"$SDLDBG_LOG" 2>/dev/null || echo 0) lines"
}

# ---------------------------------------------------------------------------
# Audio capture (option b): record DOSBox-X's SDL2 audio OUTPUT at the host via
# a PulseAudio null sink. This is robust where DOSBox-X's internal recwave
# capture is not (recwave/mapper events do not fire under headless Xvfb/SDL2).
# The capture chain is verified working (a host sine into the sink reads
# ~ -23 dB). IMPORTANT empirical finding: DOSBox-X does NOT reproduce the
# SDL3-DOS SB16 DMA backend's output -- both DOSKUTSU (Organya) and SETUP's
# sine-tone audio test record as digital silence (-91 dB) even though the
# device opens + the CT1745 mixer is programmed (banners prove it). So under
# emulation the RMS assert necessarily SKIPS with the measured level; the
# scenario's real gate is the deterministic device-open + backend banners.
# The capture is wired and will ASSERT non-silence the moment audio is present
# (e.g. a future DOSBox-X SB16 fix, or reuse of this path on real HW). RMS
# fidelity is a real-HW check per dosbox_not_proxy.
# ---------------------------------------------------------------------------
PULSE_SERVER_ADDR="unix:/run/user/$(id -u)/pulse/native"
PULSE_SINK_NAME="dosbox_cap"
PULSE_MOD=""
CAPTURE_AUDIO=0   # set to 1 around a capture launch (gates launch_dosbox env)

pulse_available() {
  [[ -S "/run/user/$(id -u)/pulse/native" ]] && command -v parec >/dev/null 2>&1 \
    && PULSE_SERVER="$PULSE_SERVER_ADDR" pactl info >/dev/null 2>&1
}
pulse_sink_up() {
  pulse_sink_down   # idempotent: drop any stale sink from a prior run
  PULSE_MOD="$(PULSE_SERVER="$PULSE_SERVER_ADDR" pactl load-module module-null-sink \
      sink_name="$PULSE_SINK_NAME" sink_properties=device.description="$PULSE_SINK_NAME" 2>/dev/null)"
  [[ -n "$PULSE_MOD" ]]
}
pulse_sink_down() {
  if [[ -n "$PULSE_MOD" ]]; then
    PULSE_SERVER="$PULSE_SERVER_ADDR" pactl unload-module "$PULSE_MOD" 2>/dev/null || true
    PULSE_MOD=""
  fi
}
# pulse_record <seconds> <outfile> -- record the sink monitor to a WAV.
pulse_record() {
  timeout "$1" parec --server="$PULSE_SERVER_ADDR" -d "${PULSE_SINK_NAME}.monitor" \
    --file-format=wav "$2" 2>/dev/null || true
}

# assert_wav_nonsilent <wav> [strict] -- measure mean volume; PASS if above the
# silence floor. Default (best-effort) SKIPS with a documented note when silent
# (DOSBox-X does not render the SDL3-DOS SB16 DMA path). "strict" FAILS when
# silent -- used for the OPL3 FM backend, whose register writes DO reach
# DOSBox-X's emulated YMF262 and are genuinely RMS-verifiable headless (verified
# ~ -43 dB under oplmode=opl3).
assert_wav_nonsilent() {
  local wav="$1" strict="${2:-}" mean
  if [[ -z "$wav" || ! -s "$wav" ]]; then
    if [[ "$strict" == "strict" ]]; then fail "no host audio captured (strict RMS expected)"; return 1; fi
    log "  [SKIP] no host audio captured -- audio gated on device-open + backend banners."
    return 0
  fi
  mean="$(ffmpeg -hide_banner -nostats -i "$wav" -af volumedetect -f null /dev/null 2>&1 \
          | sed -n 's/.*mean_volume: \(-\{0,1\}[0-9.]*\) dB.*/\1/p' | head -1)"
  log "  host-captured $(basename "$wav") mean_volume=${mean:-?} dB"
  if [[ -n "$mean" ]] && awk "BEGIN{exit !(${mean} > -80.0)}"; then
    pass "host-captured audio is non-silent (mean_volume=${mean} dB > -80)"
    return 0
  fi
  if [[ "$strict" == "strict" ]]; then
    fail "host-captured OPL3 audio is silent (mean_volume=${mean:-?} dB) -- expected FM output"
    return 1
  fi
  log "  [SKIP] host-captured audio is silent (mean_volume=${mean:-?} dB) -- DOSBox-X does"
  log "         not reproduce the SDL3-DOS SB16 DMA path; RMS non-silence is a real-HW check."
  log "         (Capture chain verified: a host sine into the same sink reads ~ -23 dB.)"
  return 0
}

DOSKUTSU_PRESENT=0
[[ -f "$DOSKUTSU_EXE" ]] && DOSKUTSU_PRESENT=1

# ===========================================================================
# NAV MODEL after T24/T25 (nx-engine 58d0933/5065939/9cadca7). READ THIS before
# editing any keystroke sequence below.
# ===========================================================================
# MAIN MENU after T47 (the Dark Forces UX restructure): TIGHTENED 9 -> 7 items.
# The SYSTEM PROFILE panel is still pinned above the menu (non-selectable, ZERO
# nav effect). New item map (Home then Down x N):
#   0 Sound               1 System speed    2 Input
#   3 Advanced / troubleshooting            4 Auto-detect best settings
#   5 Save and exit       6 Quit without saving
# Enter selects; F10 = Save and Quit; ESC quits (discard-confirm if dirty).
#
# T47 STRUCTURE CHANGE -- the old top-level "Sound setup / Sound hardware /
# Test SFX/Music / Performance" items are GONE. They moved:
#   - "Sound" (idx 0) is now a SUBMENU; reach a sound screen via Sound -> row:
#       0 Express setup        (Phase-1 STUB: an info message, no edit)
#       1 Custom setup         -> the Sound Hardware (BLASTER) screen
#       2 Music and volumes    -> the old "Sound setup" screen (rows below)
#       3 Test sound effects   -> inline SFX test (badge)
#       4 Test music           -> inline music test (badge)
#       5 Back
#     The submenu ESC backs out to the main menu SILENTLY (it is navigation, not
#     an editing screen -- no "Save setting?" prompt).
#   - "Performance" is GONE; PERF_MODE + FIXED_TIMESTEP are now the FIRST two
#     rows of the Advanced screen (idx 3): 0 PERF_MODE, 1 FIXED_TIMESTEP, then
#     the compat rows (AUDIO_WB_DIRECT_PORT [skip unless backend wb/auto],
#     DIRTY_RECTS, PIXEL_FORMAT_8, FORCE_PUMP_YIELD, THRASH_FULLCOVER).
#   - "Input" (idx 2) is a SUBMENU: row 0 Joystick on/off (live), rows 1-3
#     (Configure keyboard / joystick / Restore defaults) GREYED + skipped
#     (Phase-3), row 4 Back. Its ESC backs out SILENTLY (live edit kept).
#   - System speed (idx 1) is a tui_picklist popup (Slow/Normal/Fast/Very Fast/
#     Auto-detect); ESC cancels with no change.
# (Main + submenus are custom nav loops; the keys are IDENTICAL:
# Up/Down/Home/Enter/F10/ESC. The Custom-setup screen's Enter now OPENS a
# pick-list -- scenarios still edit fields with Right/Left, never Enter.)
#
# HOME ANCHOR (key Home, DOS scan 0x47): jumps to the first selectable row on
# EVERY screen (main menu -> idx0; editing screens -> first active row). We start
# each screen's nav with Home so Down-counts are deterministic regardless of
# where the cursor last sat. Reach item N on the main menu = Home then Down x N.
#
# EDITING SCREENS (Sound setup, Sound hardware, Input, Performance, Advanced):
#   - T62 INTERACTION MODEL (facea95): ONLY Space / Right cycle the highlighted
#     value +1 ; Left = -1. Enter NO LONGER cycles -- Enter COMMITS the current
#     row (advances the per-screen ESC revert baseline, no prompt) and moves to
#     the next live row. So all value changes in the keystroke sequences below
#     use Right/Left/space, never Enter. (Enter inside an editing screen only
#     advances the cursor + checkpoints the baseline; it never edits a value and
#     never leaves the screen.) The status bar reads "Space/Left-Right Change
#     Enter Save+Next   ESC Back".
#   - T44 SESSION-EDIT MODEL (1df352f): there is NO per-screen save. Subscreen
#     F10 does NOTHING. Edits live in the session until persisted at the main menu.
#   - T52/T62 ESC-CONFIRM: leaving a subscreen that CHANGED SINCE THE LAST COMMIT
#     via ESC pops a modal "Save setting?" (vertical Yes/No, default YES). So a
#     scenario that edits a screen (via Space/Right/Left) then ESCs MUST answer it
#     -- we send "Escape Return" (Return = the default Yes = keep the edit in the
#     session). The revert baseline starts at screen entry and advances on each
#     Enter, so an ESC right after an Enter is a SILENT back (nothing pending).
#     (ESC from an UNCHANGED screen also backs out silently, no prompt -- e.g. the
#     read-only audio-test chooser, or a no-edit browse of any editing screen.)
#   - PERSIST: DOSKUTSU.CFG is written ONLY at the main menu -- F10 (Save and
#     Exit accelerator) or Enter on "Save and exit" (item 7); both WRITE then
#     EXIT setup with NO prompt. THE DETERMINISTIC SAVE -- every CFG-writing
#     scenario ends: ...edit screens ("Escape Return" each) -> main menu -> F10.
#   - DISCARD (T52): ESC at the MAIN MENU ALWAYS pops "Quit without saving
#     settings?" (default NO) even when clean; Y / Down+Enter exits no-write.
#     "Quit without saving" (item 8) routes the same prompt. We don't use this
#     path in the CFG-writing scenarios (F10 is the clean save).
#   - Greyed rows are SKIPPED by Up/Down/Home (state-dependent). Enabling state
#     must be set first (we set backend before reaching Organya pre-render, etc.)
#
# MUSIC screen rows (R-B trimmed it to playback-only), top->bottom:
#   0 Music backend  1 Organya pre-render [SKIP unless organya]  2 Audio quality
#   [SKIP unless organya]. Sound on/off + SFX/Music volume MOVED to Custom setup.
#   With backend!=organya, only row0 is live (rows 1,2 greyed) -> Home lands on
#   backend; Down wraps back to row0.
#   #39: a conditional "MIDI music set" row inserts at index 1 ONLY when a MIDI
#   backend is selected AND >=2 MIDI sets are installed (data/midi + data/orgmid).
#   The E2E stage ships at most one set, so the row stays HIDDEN here and the
#   indices above are unchanged. (If a future scenario stages two sets, the row
#   appears at index 1 and pushes pre-render/quality to 2/3 -- but these
#   scenarios only Home+Right on row0, so they are unaffected either way.)
#
# CUSTOM SETUP rows (R-M removed the override row -- fields always editable):
# 0 I/O port, 1 IRQ, 2 DMA channel, 3 MPU port, 4 Card type, 5 Sound
# (Enabled/Disabled), 6 SFX volume, 7 Music volume. Rows 6/7 grey when sound is
# Disabled. ALL rows edit the session LIVE now (a BLASTER field writes the
# composed BLASTER on each change; Sound/volume cycle their scfg key) and ESC
# backs out SILENTLY -- there is no "Save setting?" prompt on this screen any
# more. (The single DMA pick derives the SB16 D + H BLASTER slots.)
#
# BACKEND enum (Right/Space cycles fwd): auto(0)->wb(1)->opl3(2)->organya(3).
# Saved cfg writes the raw value; "auto" is omitted from the file.

# ---- Scenario A: AUDIO_BACKEND=opl3 + PERF_MODE=1 -------------------------
scenario_A() {
  local name="A"; SCN_RC=0
  log "=== Scenario $name: AUDIO_BACKEND=opl3 + PERF_MODE=1 ==="
  # Sound(idx0)->submenu; Music(submenu row2): Home,Down x2,Enter -> screen_sound;
  #   Home->row0 backend, Right x2 (auto->wb->opl3); ESC Return (keep) -> submenu;
  #   ESC -> main (silent).
  # Advanced(idx3): Home,Down x3,Enter; Home->row0 PERF_MODE, Right (0->1);
  #   ESC Return. Then main-menu F10 = Save and Exit (writes CFG + exits).
  SCN_KEYS=( Home Return   Home $(rep 2 Down) Return
             Home $(rep 2 Right) Escape Return   Escape
             Home $(rep 3 Down) Return   Home Right Escape Return
             F10 )
  run_setup_phase "$name"
  assert_cfg_line AUDIO_BACKEND opl3 || note_fail
  assert_cfg_line PERF_MODE 1        || note_fail
  assert_cfg_line FIXED_TIMESTEP 1   || note_fail   # untouched -> baseline
  assert_cfg_line SB16_FM_VOL 28     || note_fail   # untouched -> baseline
  if [[ "$DOSKUTSU_PRESENT" == "1" ]]; then
    run_doskutsu_phase "$name"
    assert_banner "config-load"   'config: loaded DOSKUTSU\.CFG \([0-9]+ keys\)' || note_fail
    assert_banner "perf-mode"     'perf-mode: level=1' || note_fail
    # Explicit opl3 (set via the CFG-driven hint) emits the "Phase 10 Stage 4"
    # selection banner -- DISTINCT from the unset-default "default since wave 46"
    # text, so this is a real discriminator that the CFG drove the backend.
    assert_banner "opl3-backend"  'audio backend: opl3 \(Phase 10 Stage 4' || note_fail
  else
    log "  [SKIP] banner half (no build/doskutsu.exe)"
  fi
  return $SCN_RC
}

# ---- Scenario B: AUDIO_BACKEND=org + USE_JOYSTICK=1 ----------------------
scenario_B() {
  local name="B"; SCN_RC=0
  log "=== Scenario $name: AUDIO_BACKEND=org + USE_JOYSTICK=1 ==="
  # Sound(idx0)->submenu; Music(row2): Home,Down x2,Enter; Home->row0 backend,
  #   Right x3 (auto->wb->opl3->organya); ESC Return; ESC -> main. (Organya may
  #   pop a T8 pre-render note -- harmless here.)
  # Input(idx2)->submenu: Home,Down x2,Enter; Home->row0 Joystick, Right (0->1);
  #   ESC (Input submenu backs out silently, edit kept). Then F10 = Save+Exit.
  SCN_KEYS=( Home Return   Home $(rep 2 Down) Return
             Home $(rep 3 Right) Escape Return   Escape
             Home $(rep 2 Down) Return   Home Right Escape
             F10 )
  run_setup_phase "$name"
  assert_cfg_line AUDIO_BACKEND organya || note_fail
  assert_cfg_line USE_JOYSTICK 1        || note_fail
  assert_cfg_line PERF_MODE 0           || note_fail   # untouched -> baseline
  if [[ "$DOSKUTSU_PRESENT" == "1" ]]; then
    run_doskutsu_phase "$name"
    assert_banner "config-load"  'config: loaded DOSKUTSU\.CFG \([0-9]+ keys\)' || note_fail
    assert_banner "organya-backend" 'audio backend: organya \(forced' || note_fail
    # USE_JOYSTICK=1 flips input.cpp's value-check so the default-path
    # "Joystick: skipped" line is NOT emitted. There is no positive banner when
    # no joystick device is present (DOSBox-X has none -- input.cpp only logs
    # Opened/Couldn't-open when SDL enumerates >=1 device), so the discriminator
    # is the ABSENCE of the skipped line.
    assert_banner_absent "joystick-enabled" 'Joystick: skipped' || note_fail
  else
    log "  [SKIP] banner half (no build/doskutsu.exe)"
  fi
  return $SCN_RC
}

# ---- Scenario C: SB16_FM_VOL=20 + FIXED_TIMESTEP=0 -----------------------
scenario_C() {
  local name="C"; SCN_RC=0
  log "=== Scenario $name: SB16_FM_VOL=20 + FIXED_TIMESTEP=0 ==="
  # R-B/R-M: SB16 FM (Music) volume is Custom row 7 (no override row now).
  #   Sound(idx0)->submenu; Custom(row1): Home,Down,Enter -> screen_hardware;
  #   Home->row0 (I/O port), Down x7 -> row7 Music volume; Left x8 (28->20, live
  #   edit); ESC (silent, no prompt) -> submenu; ESC -> main. (Backend untouched
  #   -> auto -> omitted.)
  # Advanced(idx3): Home,Down x3,Enter; Home->row0 PERF_MODE, Down->row1
  #   FIXED_TIMESTEP, Right (1->0); ESC Return. Then main-menu F10 = Save+Exit.
  SCN_KEYS=( Home Return   Home Down Return
             Home $(rep 7 Down) $(rep 8 Left) Escape   Escape
             Home $(rep 3 Down) Return   Home Down Right Escape Return
             F10 )
  run_setup_phase "$name"
  assert_cfg_line SB16_FM_VOL 20            || note_fail
  assert_cfg_line FIXED_TIMESTEP 0          || note_fail
  assert_cfg_absent_uncommented AUDIO_BACKEND || note_fail  # auto -> comment only
  if [[ "$DOSKUTSU_PRESENT" == "1" ]]; then
    run_doskutsu_phase "$name"
    assert_banner "config-load"     'config: loaded DOSKUTSU\.CFG \([0-9]+ keys\)' || note_fail
    assert_banner "sb16-fm-vol"     'SB16 mixer balance:.*fm=20' || note_fail
    assert_banner "fixed-timestep"  'fixed-timestep: DISABLED' || note_fail
  else
    log "  [SKIP] banner half (no build/doskutsu.exe)"
  fi
  return $SCN_RC
}

# ---- Scenario AUDIO: organya backend -> audio device live (+ best-effort WAV)
scenario_AUDIO() {
  local name="AUDIO"; SCN_RC=0
  log "=== Scenario $name: organya backend -> SB16 device live + WAV capture ==="
  # AUDIO_BACKEND=organya: Organya renders PCM through the SB16 DMA path. The
  # deterministic gate is the SB16 device-open banner + the organya-backend
  # select (proves SETUP's audio config brought the device up and selected the
  # configured backend). WAV-RMS non-silence is attempted as a best-effort
  # bonus (see assert_wav_nonsilent -- DOSBox-X headless recwave limitation).
  # Sound(idx0)->submenu; Music(row2): Home,Down x2,Enter; Home->row0 backend,
  #   Right x3 (->organya); ESC Return; ESC -> main. Then F10 = Save and Exit.
  SCN_KEYS=( Home Return   Home $(rep 2 Down) Return
             Home $(rep 3 Right) Escape Return   Escape   F10 )
  run_setup_phase "$name"
  assert_cfg_line AUDIO_BACKEND organya || note_fail
  if [[ "$DOSKUTSU_PRESENT" == "1" ]]; then
    run_doskutsu_phase "$name" capture
    assert_banner "config-load"     'config: loaded DOSKUTSU\.CFG \([0-9]+ keys\)' || note_fail
    assert_banner "organya-backend" 'audio backend: organya \(forced' || note_fail
    # Deterministic audio gate: the SB16 device actually opened (at the engine
    # frame rate) -- proves the configured-audio path is live, not just selected.
    assert_banner "sb16-device-open" 'Sound system: device frame-rate: requesting [0-9]+ Hz' || note_fail
    assert_wav_nonsilent "$WAV_OUT"     # host-captured RMS (asserts if present; skips on emulator silence)
  else
    log "  [SKIP] banner + WAV-capture half (no build/doskutsu.exe)"
  fi
  return $SCN_RC
}

# ---- Scenario SETUPAUDIO (T5): SETUP's OWN Test SFX/Music screen ----------
# Drives the AUDIOTEST=1 SETUP.EXE into its "Test SFX / Music" screen and
# triggers Play SFX + Play music. The screen's audiotest_init opens the REAL
# SB16 device through the same SDL3-DOS backend the game uses and programs the
# CT1745 mixer from the configured SDL_HINT_DOSKUTSU_SB16_* levels -- emitting
# the SDL/0074 mixer-balance banner to SETUP's OWN SDLDBG.LOG. We pre-seed a
# distinctive SB16_FM_VOL so that banner is a discriminator: it proves SETUP's
# built-in audio test brought the SB16 path up with the user's configured
# levels (the deterministic gate). WAV-RMS is the same best-effort bonus as the
# AUDIO scenario (DOSBox-X headless recwave limitation).
scenario_SETUPAUDIO() {
  local name="SETUPAUDIO"; SCN_RC=0
  log "=== Scenario T5: SETUP's Test SFX/Music screen -> SB16 device live ==="
  # Distinctive FM level (22) the audio test must apply to the CT1745 mixer.
  {
    printf '; T5 SETUP audio-test probe\r\n'
    printf 'SB16_FM_VOL=22\r\n'
    printf 'AUDIO_BACKEND=organya\r\n'
  } > "$STAGE_DIR/DOSKUTSU.CFG"
  cp "$STAGE_DIR/DOSKUTSU.CFG" "$OUT_DIR/${name}.cfg" 2>/dev/null || true

  clear_logs
  rm -rf "$OUT_DIR/cap"; mkdir -p "$OUT_DIR/cap"
  local wav="$OUT_DIR/cap/${name}.wav"
  local captured=0
  if pulse_available && pulse_sink_up; then CAPTURE_AUDIO=1; captured=1; fi
  log "[$name] launching SETUP.EXE (AUDIOTEST=1) into the audio-test screen..."
  launch_dosbox "$CONF_FAST" -c 'SETUP.EXE' || { CAPTURE_AUDIO=0; pulse_sink_down; note_fail; return 1; }
  sleep 8                                  # SETUP boot (profile_detect + SDL link)
  shoot "${name}-01-setup-main"
  # T47 flow: the tests are now inline rows in the Sound submenu (the separate
  # chooser screen was retired). Home,Enter -> Sound submenu; Home,Down x3 ->
  # "Test sound effects" (row 3); Enter runs sound_inline_test(0), which opens
  # the SB16 device (emits the mixer-balance banner) and auto-plays the bounded
  # SFX (blocking), then asks "Did you hear it?" (Yes/No; 'y' = immediate Yes).
  send_keys Home Return                    # main -> Sound submenu
  send_keys Home $(rep 3 Down) Return      # row3 Test sound effects -> SB16 opens + SFX plays
  sleep 2                                  # bounded SFX play
  shoot "${name}-02-popup-sfx"
  send_keys y                              # Yes/No menu -> 'y' = Yes -> back to submenu (row3)
  sleep 0.5
  # Test music is the next row (row 4). The capture window must overlap the play,
  # so start the host record BEFORE the Return. Music is until-key (organya ->
  # looping PCM tone, 10 s cap); the play stage consumes the stop key, the answer
  # is a SEPARATE key -- let the tone fill the window, then 'space' stops the
  # play and 'y' answers.
  send_keys Down                           # row4 Test music
  local cap_pid=""
  if [[ "$captured" == "1" ]]; then ( pulse_record 6 "$wav" ) & cap_pid=$!; sleep 0.5; fi
  send_keys Return                         # play music popup; PCM tone loops until-key
  [[ -n "$cap_pid" ]] && wait "$cap_pid" 2>/dev/null   # ~6 s of tone fills the WAV
  send_keys space                          # stop the until-key play -> answer prompt
  sleep 0.5
  send_keys y                              # Yes/No menu -> 'y' = Yes -> back to submenu (row4)
  send_keys Escape                         # submenu -> main menu
  sleep 1
  CAPTURE_AUDIO=0; pulse_sink_down
  kill_dosbox
  collect_logs                             # grabs SETUP's SDLDBG.LOG
  log "[$name] SETUP SDLDBG.LOG=$(wc -l <"$SDLDBG_LOG" 2>/dev/null || echo 0) lines"
  # Deterministic gate: SETUP's audio test opened the SB16 device AND applied
  # the configured FM level (fm=22) to the CT1745 mixer.
  assert_banner "setup-sb16-open" 'SB16 mixer balance: .*fm=22' || note_fail
  assert_wav_nonsilent "$wav"              # host-captured RMS (asserts if present; skips on silence)
  return $SCN_RC
}

# ===========================================================================
# T11: Sound Hardware selection + real per-backend audio
# ===========================================================================

# run_setup_audio <name> <backend> <conf> -- write a CFG for <backend>, drive
# the AUDIOTEST=1 SETUP.EXE into the Sound submenu's inline "Test music" row
# (T47), host-capture the output. Sets WAV_OUT + collects SETUP's SDLDBG.LOG.
run_setup_audio() {
  local name="$1" backend="$2" conf="$3"
  { printf '; T11 per-backend audio: %s\r\n' "$backend"
    printf 'AUDIO_BACKEND=%s\r\n' "$backend"
    printf 'SB16_FM_VOL=31\r\n'            # max FM level -> OPL3 audibility
  } > "$STAGE_DIR/DOSKUTSU.CFG"
  cp "$STAGE_DIR/DOSKUTSU.CFG" "$OUT_DIR/${name}.cfg" 2>/dev/null || true
  clear_logs
  rm -rf "$OUT_DIR/cap"; mkdir -p "$OUT_DIR/cap"
  WAV_OUT="$OUT_DIR/cap/${name}.wav"
  local captured=0
  if pulse_available && pulse_sink_up; then CAPTURE_AUDIO=1; captured=1; fi
  log "[$name] SETUP audio test, backend=$backend, conf=$(basename "$conf")..."
  launch_dosbox "$conf" -c 'SETUP.EXE' || { CAPTURE_AUDIO=0; pulse_sink_down; note_fail; return 1; }
  sleep 8                                  # SETUP boot
  # T47: Test music is row 4 of the Sound submenu. Home,Enter -> submenu;
  # Home,Down x4 -> "Test music". The device opens when Enter runs the test.
  send_keys Home Return                    # main -> Sound submenu
  send_keys Home $(rep 4 Down)             # row4 Test music (positioned, not yet entered)
  shoot "${name}-02-sound-submenu"
  # T28 popup flow (audiotest_sdl.c): Test music = Enter -> the play stage runs
  # UNTIL-KEY now, not a bounded arpeggio. With the stage's data/midi/curly.mid
  # present the opl3/wb backends play the REAL Title theme (15 s cap); organya/
  # pcm play the looping tone (10 s cap). The play stage CONSUMES the stop key;
  # the "Did you hear it?" answer is a SEPARATE key in the answer stage -- so a
  # single 'y' no longer both stops AND answers (the old 4x bounded-play loop is
  # invalid). ONE play now fills the whole capture window: start record, Return,
  # let the real song fill the 8 s window, then 'space' stops the play and 'y'
  # answers. ('space' is ignored at the answer prompt, so this is robust even if
  # a backend falls back to the bounded arpeggio.)
  local cap_pid=""
  if [[ "$captured" == "1" ]]; then ( pulse_record 8 "$WAV_OUT" ) & cap_pid=$!; sleep 0.5; fi
  send_keys Return                         # start the (real Title theme / tone) play
  [[ -n "$cap_pid" ]] && wait "$cap_pid" 2>/dev/null   # ~8 s of real song fills the WAV
  send_keys space                          # stop the until-key play -> answer prompt
  sleep 0.5
  send_keys y                              # Yes/No menu -> 'y' = Yes -> back to submenu (row4)
  send_keys Escape                         # submenu -> main menu
  sleep 1
  CAPTURE_AUDIO=0; pulse_sink_down
  kill_dosbox
  collect_logs
  log "[$name] SETUP SDLDBG.LOG=$(wc -l <"$SDLDBG_LOG" 2>/dev/null || echo 0) lines"
}

# ---- T11 per-backend: OPL3 FM (real RMS, headless-verifiable) -------------
scenario_BKOPL3() {
  local name="BKOPL3"; SCN_RC=0
  log "=== T11 per-backend: OPL3 FM (RMS-verifiable headless under oplmode=opl3) ==="
  run_setup_audio "$name" opl3 "$CONF_OPL3"
  assert_banner "setup-sb16-open" 'SB16 mixer balance:' || note_fail
  # OPL3 register writes (port 0x388) reach DOSBox-X's emulated YMF262 -> real,
  # genuinely non-silent FM output (~ -43 dB). STRICT assert.
  assert_wav_nonsilent "$WAV_OUT" strict || note_fail
  return $SCN_RC
}

# ---- T11 per-backend: WaveBlaster MIDI (real-HW only) --------------------
scenario_BKWB() {
  local name="BKWB"; SCN_RC=0
  log "=== T11 per-backend: WaveBlaster MIDI (real-HW only; no MPU-401 in DOSBox) ==="
  run_setup_audio "$name" wb "$CONF_FAST"
  assert_banner "setup-sb16-open" 'SB16 mixer balance:' || note_fail
  # The WB music path engaged SETUP's MPU-401 direct-port init (blind init).
  # The WB MPU init banner proves the WaveBlaster path reached MPU programming.
  # ANCHOR on the unique "mpu401: ... port_base=0x" line FORMAT only -- the init
  # verb has drifted across SDL revisions (SDL/0093 "blind init" / "direct-port
  # init", then SDL/0098 default-ON "cold-init DEFAULT-ON at" + "entry-ack drain
  # at"); pinning the old "init at" verb stale-failed once cold-init went
  # default-ON, so match any mpu401 line that programs a port_base
  # (grep_anchor_confound discipline: anchor the stable FORMAT, not the verb).
  assert_banner "setup-mpu401" 'mpu401:.*port_base=0x' || note_fail
  # DOSBox-X does not emulate the MPU-401/wavetable, so the MIDI arpeggio is
  # inaudible here -> documented skip; non-silence is the operator's real-HW check.
  assert_wav_nonsilent "$WAV_OUT"
  return $SCN_RC
}

# ---- T11 per-backend: Organya / PCM (SB16 DMA) -- cache-ABSENT fallback --
scenario_BKORG() {
  local name="BKORG"; SCN_RC=0
  log "=== T11 per-backend: Organya/PCM cache-ABSENT (tone fallback) ==="
  org_cache_absent                          # no CURLY.PCM -> T36 tone fallback path
  run_setup_audio "$name" organya "$CONF_FAST"
  assert_banner "setup-sb16-open" 'SB16 mixer balance:' || note_fail
  # T36: with no pre-rendered cache, the organya music test logs the fallback +
  # plays the test tone. The "org:" trace is the deterministic witness; SB16 DMA
  # PCM audibility itself is a documented headless skip (real-HW ear check).
  assert_banner "org-fallback" 'org:.*no pre-rendered cache' || note_fail
  assert_wav_nonsilent "$WAV_OUT"
  return $SCN_RC
}

# ---- T36 Organya cache-PRESENT: real pre-rendered snippet ----------------
# Stage a deterministic OPC1 fixture (CACHE/11025_1/CURLY.PCM via the host
# generator) so the organya music test takes the pre-rendered-snippet path
# instead of the tone fallback. Witnesses: the "org: loaded ... snippet" trace
# (deterministic) + non-silence (the snippet is real PCM on the SB16 path).
scenario_BKORGCACHE() {
  local name="BKORGCACHE"; SCN_RC=0
  log "=== T36 per-backend: Organya cache-PRESENT (pre-rendered title snippet) ==="
  org_cache_present                         # stage CACHE/11025_1/CURLY.PCM (OPC1 fixture)
  run_setup_audio "$name" organya "$CONF_FAST"
  assert_banner "setup-sb16-open" 'SB16 mixer balance:' || note_fail
  # The music test loaded the pre-rendered snippet (not the tone fallback).
  assert_banner "org-snippet-loaded" 'org: loaded .* snippet' || note_fail
  assert_banner_absent "org-no-fallback" 'org:.*no pre-rendered cache' || note_fail
  assert_wav_nonsilent "$WAV_OUT"
  org_cache_absent                          # leave the stage clean for later runs
  return $SCN_RC
}

# ---- T11 Sound Hardware: authoritative BLASTER --------------------------
scenario_HWBLASTER() {
  local name="HWBLASTER"; SCN_RC=0
  log "=== T11 Sound Hardware: authoritative BLASTER (config P300 over ambient P330) ==="
  # Seed a known BLASTER (MPU port P330) so the Sound Hardware screen starts there.
  { printf '; T11 sound hardware\r\n'
    printf 'AUDIO_BACKEND=wb\r\n'
    printf 'BLASTER=A220 I5 D1 H5 P330 T6\r\n'
  } > "$STAGE_DIR/DOSKUTSU.CFG"
  # Sound(idx0)->submenu; Custom setup(submenu row1): Home,Down,Enter ->
  #   screen_hardware (override row0 ON since a BLASTER is seeded, so the
  #   A/I/D/H/P/T fields are active).
  # In-screen (R-M: no override row): rows 0 A, 1 I, 2 DMA, 3 P, 4 T. Home->row0,
  #   Down x3 -> P field. The R-A expanded MPU list is ascending (...0x300,
  #   0x320, 0x330...), so from the seeded P330 a Left x2 lands on P300 (0x330 ->
  #   0x320 -> 0x300). The edit writes the composed BLASTER LIVE; ESC backs out
  #   SILENTLY (no prompt) -> submenu -> main; main-menu F10 writes P300. (Custom's
  #   Enter OPENS a pick-list, so we edit the P field with Left/Right, not Enter.)
  SCN_KEYS=( Home Return   Home Down Return
             Home $(rep 3 Down) $(rep 2 Left) Escape   Escape   F10 )
  run_setup_phase "$name" keep_cfg
  assert_cfg_line BLASTER 'A220 I5 D1 H5 P300 T6' || note_fail
  if [[ "$DOSKUTSU_PRESENT" == "1" ]]; then
    # Launch DOSKUTSU with a DIFFERENT ambient BLASTER (P330). The config's P300
    # must WIN -- BLASTER is authoritative (loader setenv overwrite=1).
    clear_logs
    launch_dosbox "$CONF_FAST" -c 'SET BLASTER=A220 I5 D1 H5 P330 T6' -c 'DOSKUTSU.EXE' || { note_fail; return 1; }
    sleep 8
    shoot "${name}-03-doskutsu"
    kill_dosbox
    collect_logs
    assert_banner "config-load" 'config: loaded DOSKUTSU\.CFG \([0-9]+ keys\)' || note_fail
    # Authoritative override proven: the engine inits the MPU port on the CONFIG's
    # P300, not the ambient P330. ANCHOR on the 0x0300 port discriminator only --
    # a wrong P330 would read 0x0330 (grep_anchor_confound: anchor to the unique
    # value, not the verb). The init-verb prefix has drifted across SDL revisions
    # ("blind init" / "direct-port init" / SDL/0098 "cold-init DEFAULT-ON at" /
    # "entry-ack drain at"); pinning the old "init at" verb stale-failed once the
    # WB cold-init went default-ON (SDL/0095/0098), so match any mpu401 line that
    # programs port_base=0x0300.
    assert_banner "authoritative-mpu-port" 'mpu401:.*port_base=0x0300' || note_fail
    assert_banner "wb-backend" 'MidiScheduler armed with MidiBackendWaveBlaster' || note_fail
  else
    log "  [SKIP] engine half (no build/doskutsu.exe)"
  fi
  return $SCN_RC
}

# ---- Scenario EXPRESS: DF-UX Phase 2 one-key detect ----------------------
# Sound submenu -> Express setup: a red warning modal, re-run the hardware
# probes, an evidence modal (DSP version), then write the DETECTED BLASTER +
# recommended backend to the session, offer the inline music test. The ambient
# launch env sets BLASTER=A220 I5 D1 H5 T6 (launch_dosbox), so profile_detect
# parses that exact card and Express composes it back into the session; the
# recommended backend for a detected SB16 is opl3. We answer NO to the test
# (the AUDIOTEST build's device bring-up is exercised by scenario_SETUPAUDIO;
# here we assert the CFG write deterministically). The baseline CFG seeds a
# DIFFERENT BLASTER so the assertion proves Express OVERWROTE it with detection.
scenario_EXPRESS() {
  local name="EXPRESS"; SCN_RC=0
  log "=== Scenario $name: DF-UX Phase 2 Express one-key detect ==="
  # Seed a deliberately-wrong baseline so the post-Express value proves detection.
  { printf '; EXPRESS baseline (intentionally stale -- Express must overwrite)\r\n'
    printf 'AUDIO_BACKEND=organya\r\n'
    printf 'BLASTER=A240 I7 D3 H7 T1\r\n'
  } > "$STAGE_DIR/DOSKUTSU.CFG"
  # Sound(idx0)->submenu; Express(submenu row0): Home,Return.
  #   screen_express: Return (dismiss red warning) -> profile_detect ->
  #   Return (dismiss evidence) -> 'n' (decline the Test-it-now prompt).
  #   -> submenu; Escape -> main; F10 = Save and Exit (writes CFG).
  SCN_KEYS=( Home Return   Home Return   Return Return n   Escape   F10 )
  run_setup_phase "$name" keep_cfg
  # Express composed the DETECTED card (ambient A220 I5 D1 H5 T6) over the stale
  # A240 baseline, and set the recommended SB16 backend opl3.
  assert_cfg_line BLASTER 'A220 I5 D1 H5 T6' || note_fail
  assert_cfg_line AUDIO_BACKEND opl3          || note_fail
  # (assert_profile_video_speed already ran inside run_setup_phase.)
  return $SCN_RC
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

require_tools
ensure_stage
start_xvfb

ALL=(A B C EXPRESS AUDIO SETUPAUDIO HWBLASTER BKOPL3 BKWB BKORG BKORGCACHE)
if [[ -n "$ONLY" ]]; then ALL=("$ONLY"); fi

OVERALL_RC=0
for s in "${ALL[@]}"; do
  case "$s" in
    A)          scenario_A ;;
    B)          scenario_B ;;
    C)          scenario_C ;;
    EXPRESS)    scenario_EXPRESS ;;
    AUDIO)      scenario_AUDIO ;;
    SETUPAUDIO) scenario_SETUPAUDIO ;;
    HWBLASTER)  scenario_HWBLASTER ;;
    BKOPL3)     scenario_BKOPL3 ;;
    BKWB)       scenario_BKWB ;;
    BKORG)      scenario_BKORG ;;
    BKORGCACHE) scenario_BKORGCACHE ;;
    *) echo "unknown scenario: $s" >&2; exit 2 ;;
  esac
  rc=$?
  if [[ "$rc" != "0" ]]; then
    OVERALL_RC=1
    FAILED_SCENARIOS+=("$s")
    log "scenario $s: FAILED"
    [[ "$KEEP_GOING" == "1" ]] || { log "stopping at first failure (use --keep-going to continue)"; break; }
  else
    log "scenario $s: PASSED"
  fi
done

echo
if [[ "$OVERALL_RC" == "0" ]]; then
  log "ALL SCENARIOS PASSED"
else
  log "FAILED scenarios: ${FAILED_SCENARIOS[*]}"
fi
log "artifacts in $OUT_DIR (screenshots, *.cfg, debug.log, sdldbg.log, dosbox.log)"
exit $OVERALL_RC
