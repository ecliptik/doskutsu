#!/usr/bin/env bash
#
# tests/run-gameplay-smoke.sh -- visible DOSBox-X gameplay smoke test
#
# Drives DOSKUTSU.EXE through a scripted sequence of keystrokes via xdotool,
# capturing screenshots at named milestones. Designed for repeatable smoke
# verification without a human at the keyboard. Distinct from the headless
# library smokes (run-smoke.sh, run-sdl3-smoke.sh, etc.) -- those exercise
# isolated SDL/SDL_mixer/SDL_image; this one exercises the full game binary
# end-to-end through the visible-DOSBox-X path used for screenshots.
#
# What this CAN verify:
#   - Binary boots without DPMI/CWSDPMI failure.
#   - Title screen renders (paint pipeline works).
#   - Engine accepts keyboard input (Z key advances "New game").
#   - Post-title content renders (intro scene, first cave, etc.).
#   - debug.log + sdldbg.log are captured for offline inspection.
#
# What this CANNOT verify (still requires human eyes):
#   - Whether the rendered content is *correct* (sprite alignment, palette
#     fidelity, text legibility, scrolling smoothness).
#   - Whether audio plays (no audio-capture path on the headless test bot).
#   - Long-running stability (heap fragmentation, memory leaks) -- that's
#     the Phase 7 gate's 30-min run and it remains human-in-the-loop.
#   - Save/load round-trip integrity.
#
# So: this script is the floor, not the ceiling. Pass = the binary
# *can* be driven; humans review screenshots to judge whether it
# *should* be shipped.
#
# Usage:
#   tests/run-gameplay-smoke.sh                   # default: --fast, ./build/stage, /tmp/gameplay-smoke
#   tests/run-gameplay-smoke.sh --parity          # parity DOSBox-X config (cycles=fixed 40000)
#   tests/run-gameplay-smoke.sh --out /tmp/foo    # custom artifact dir
#   tests/run-gameplay-smoke.sh --keep-running    # don't kill DOSBox-X at the end
#   tests/run-gameplay-smoke.sh --skip-gate       # capture logs but skip the banner-emit gate
#                                                 # (intended for wave-39 ablation builds where
#                                                 # reverted patches make required banners
#                                                 # unreachable by design -- gate would fail
#                                                 # spuriously; flush-instr decomp verifies the
#                                                 # expected absences from the captured logs)

set -euo pipefail

# Contention-robust retry bookkeeping (task #10). Capture the original argv BEFORE
# the parse loop below consumes it, so a clean-slate re-run preserves --parity /
# --out / etc. SMOKE_ATTEMPT is incremented across the (at most one) self-re-exec;
# see the REQUIRED-banner retry guard near the gate decision for the rationale.
SMOKE_ORIG_ARGS=("$@")
SMOKE_ATTEMPT="${SMOKE_ATTEMPT:-1}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCHER="$REPO_ROOT/tools/dosbox-launch.sh"
# shellcheck source=../tools/dosbox-teardown.sh
source "$REPO_ROOT/tools/dosbox-teardown.sh"   # dbx_kill_conf -- conf-scoped teardown
LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE --fast)
# CONF must mirror the conf that tools/dosbox-launch.sh selects for these flags so the
# scoped teardown targets exactly the instance this smoke launched (--fast -> fast conf,
# --parity drops --fast -> parity conf). Kept in lockstep with the LAUNCHER_FLAGS arg loop.
CONF="$REPO_ROOT/tools/dosbox-x-fast.conf"
OUT_DIR="/tmp/gameplay-smoke"
KEEP_RUNNING=0
SKIP_GATE=0
DISPLAY="${DOSBOX_DISPLAY:-:0}"

while (($#)); do
  case "$1" in
    --parity)         LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE); CONF="$REPO_ROOT/tools/dosbox-x.conf" ;;
    --out)            shift; OUT_DIR="$1" ;;
    --keep-running)   KEEP_RUNNING=1 ;;
    --skip-gate)      SKIP_GATE=1 ;;
    -h|--help)        sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //' ; exit 0 ;;
    *)                echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$OUT_DIR"
RESULTS="$OUT_DIR/results.txt"
: > "$RESULTS"

log() {
  echo "[gameplay-smoke] $*" | tee -a "$RESULTS"
}

shoot() {
  # shoot <name> -- capture a screenshot, store under OUT_DIR/<name>.png
  local name="$1"
  DISPLAY=$DISPLAY scrot -u "$OUT_DIR/$name.png" 2>/dev/null || true
  local size
  size=$(stat -c%s "$OUT_DIR/$name.png" 2>/dev/null || echo 0)
  log "screenshot $name.png ($size bytes)"
}

key() {
  # key <name> -- send a key via xdotool; we do NOT focus the window each
  # time (focus once at the start) because re-focusing introduces a
  # window-manager round-trip that desynchronizes the keystroke timing.
  local name="$1"
  DISPLAY=$DISPLAY xdotool key --delay 60 "$name"
  log "sent key: $name"
}

# Refuse to run if DOSBox-X is already up -- don't fight the existing lock.
if pgrep -x dosbox-x >/dev/null; then
  echo "[gameplay-smoke] error: dosbox-x already running. Kill it first or use --kill-first via the launcher." >&2
  exit 3
fi

# Refresh stage so we test the current build.
log "make stage..."
make -C "$REPO_ROOT" stage >>"$RESULTS" 2>&1

# SETUP-stub gate (v1.6.2 shipped-stub regression, [[build_cache_hygiene]] two-witness
# gap). The RELEASE configurator build/setup/setup-release.exe (AUDIOTEST=1) must be the
# real SDL3 audio-test backend, NOT the AUDIOTEST=0 scaffold stub. We check the RELEASE
# binary, NOT the dev-staged SETUP.EXE -- `make stage` deliberately stages the stub (no
# SDL link needed to drive the TUI in a smoke), so asserting no-stub on the staged file
# would false-fail every dev run. If setup-release.exe is absent (pure dev smoke that
# never built the release backend) this is skipped with a note. When present it fails
# HARD on any scaffold/stub marker -- the missing SETUP half of the game-binary two-witness.
SETUP_RELEASE_EXE="$REPO_ROOT/build/setup/setup-release.exe"
if [[ -f "$SETUP_RELEASE_EXE" ]]; then
  if strings "$SETUP_RELEASE_EXE" | grep -qiE 'scaffold|not yet linked|audiotest_stub'; then
    log "FAIL: $SETUP_RELEASE_EXE is the AUDIOTEST=0 SCAFFOLD STUB -- shipped-stub regression."
    log "      Fix: rm build/setup/setup-release.exe build/setup/.audiotest-* && make setup-release"
    exit 6
  fi
  log "setup-stub gate: OK -- setup-release.exe is the AUDIOTEST=1 backend (no scaffold markers)."
else
  log "setup-stub gate: SKIP -- no build/setup/setup-release.exe (dev smoke; release backend not built)."
fi

# PLAY.TAS sanity check (catches the wave-44 stub regression). When a
# PLAY.TAS is present at the stage root, it must be a STRUCTURALLY VALID
# DTASv1 recording, not a header-only stub that auto-exits immediately the
# moment TAS replay engages. Absence is OK -- existing gameplay smoke does
# not require TAS replay (it drives input via xdotool).
#
# This is a DTASv1 STRUCTURAL check, NOT a byte floor. The original wave-44
# guard used ">1000 bytes", which predated the segmented-TAS era (patch 0191)
# and FALSE-REJECTED valid segmented recordings (the canonical Mimiga organya
# calib segment is only ~220 bytes = 20-byte header + 25 events). That false
# reject caused a real landmine: make stage's stage-tas.sh fell back to a
# WRONG 1932-byte TAS that happened to clear the floor. Per docs/TAS.md the
# format is: 20-byte header (magic "DTASv1\n"(7) + version(1) + prng_seed(4)
# + flags(4) + n_events(4); n_events=0xFFFFFFFF = stream mode, NOT a literal
# count -- so validate by SIZE, not the field) then 8-byte events (tick u32 +
# mask u32) to EOF. VALID = magic present AND size >= 28 (header + >=1 event)
# AND (size-20) is a whole number of 8-byte events. The stub to catch = a
# header-only file (20 bytes, 0 events) -> fails size>=28.
STAGED_TAS="$REPO_ROOT/build/stage/PLAY.TAS"
if [[ -f "$STAGED_TAS" ]]; then
  TAS_BYTES=$(stat -c%s "$STAGED_TAS")
  # Hex-compare the 7-byte magic "DTASv1\n" (44 54 41 53 76 31 0a). Do NOT use
  # $(head -c7) string compare -- command substitution strips the trailing \n,
  # so the magic's final 0x0a byte would be lost and the compare always fails.
  TAS_MAGIC_HEX=$(head -c 7 "$STAGED_TAS" 2>/dev/null | od -An -tx1 | tr -d ' \n')
  TAS_EVENT_BYTES=$(( TAS_BYTES - 20 ))
  if [[ "$TAS_MAGIC_HEX" != "4454415376310a" ]]; then
    log "FAIL: $STAGED_TAS bad DTASv1 magic (hex '$TAS_MAGIC_HEX', want 4454415376310a) -- not a TAS recording."
    log "      Fix: run ./scripts/stage-tas.sh or set DOSKUTSU_TAS_SRC."
    exit 5
  fi
  if (( TAS_BYTES < 28 || TAS_EVENT_BYTES % 8 != 0 )); then
    log "FAIL: $STAGED_TAS is $TAS_BYTES bytes -- DTASv1 header-only/truncated stub"
    log "      (need magic + >=1 whole 8-byte event = >=28 bytes, (size-20)%8==0)."
    log "      This is the wave-44 stub shape. Fix: ./scripts/stage-tas.sh or DOSKUTSU_TAS_SRC."
    exit 5
  fi
  TAS_EVENTS=$(( TAS_EVENT_BYTES / 8 ))
  TAS_SHA=$(sha256sum "$STAGED_TAS" | awk '{print $1}')
  log "staged PLAY.TAS: $TAS_BYTES bytes, $TAS_EVENTS events, sha256 ${TAS_SHA:0:12}"
else
  log "note: no PLAY.TAS at $STAGED_TAS (TAS replay unavailable; xdotool drive only)"
fi

# Clear prior logs so debug.log/sdldbg.log only contain this run's output.
# wave-53 (patch 0148 + SDL/0062) relocated both logs into a LOGS/ subdir;
# clear the old root locations too so a stale pre-wave-53 file at the root
# cannot be mistaken for fresh output if the runtime mkdir ever falls back.
rm -f "$REPO_ROOT/build/stage/LOGS/DEBUG.LOG" \
      "$REPO_ROOT/build/stage/DOSKUTSU/LOGS/SDLDBG.LOG" \
      "$REPO_ROOT/build/stage/DEBUG.LOG" \
      "$REPO_ROOT/build/stage/DOSKUTSU/SDLDBG.LOG" \
      "$REPO_ROOT/build/stage/debug.log" "$REPO_ROOT/build/stage/sdldbg.log"

# Clear any stray DOSKUTSU.CFG left in the stage by a prior ad-hoc run (a tempo
# capture, a per-backend audio probe, etc). The DEFAULT gameplay gate expects NO
# CFG -- patch 0216's config shim then emits the "no DOSKUTSU.CFG, using built-in
# defaults" variant and the compiled-default levers fire (e.g. SDL/0074 mixer
# balance master=31 voice=31 fm=28, a REQUIRED banner the gate pins by value). A
# leftover CFG (e.g. SB16_FM_VOL=31) silently shifts those values -> the required
# banner reads a different value -> false GATE FAIL that looks like a code
# regression but is pure stage contamination. make stage does NOT write/clear a
# CFG, so clear it here. (A future CFG-driven smoke variant must write its CFG
# AFTER this point.)
rm -f "$REPO_ROOT/build/stage/DOSKUTSU.CFG"

log "launching DOSBox-X (flags: ${LAUNCHER_FLAGS[*]})"
"$LAUNCHER" "${LAUNCHER_FLAGS[@]}" >"$OUT_DIR/launcher.log" 2>&1 &
LAUNCH_PID=$!

# Wait for the dosbox-x process to spawn (the launcher backgrounds it).
for _ in $(seq 1 20); do
  if pgrep -x dosbox-x >/dev/null; then break; fi
  sleep 0.5
done
if ! pgrep -x dosbox-x >/dev/null; then
  log "FAIL: dosbox-x did not start within 10s"
  exit 4
fi
log "dosbox-x started, PID $(pgrep -x dosbox-x)"

# Focus the DOSBox-X window once (xdotool's search is racy if many X clients
# claim "DOSBox" in the title -- we wait + retry).
for _ in $(seq 1 10); do
  if DISPLAY=$DISPLAY xdotool search --name DOSBox windowactivate --sync 2>/dev/null; then
    log "DOSBox window focused"
    break
  fi
  sleep 0.5
done

# Precache-window hardening: wait until boot actually REACHES the title menu
# before the fixed milestone sequence. On a fresh-sha build the org PCM cache
# (keyed to DOSKUTSU_BUILD_SHA12) is cold, so the first boot runs the one-time
# 0266 auto-precache (~41 songs) which can outlast the fixed window -- boot
# stays mid-render BEFORE the menu, so the REQUIRED 0224 menu-slide banner
# (post-textbox.Init eval) is never REACHED and the gate false-fails "REQUIRED
# banner absent / code path dead" even though 0224 is embedded + healthy. The
# gate's contention retry clears LOGS but not the CACHE, so both attempts burn
# their window precaching and the false-fail reproduces. This is
# [[smoke_gate_discipline]]'s SECOND 0224-false-blame mechanism (precache-
# window-starvation, distinct from the first, shared-log contention).
#
# We watch the RUNTIME DEBUG.LOG (per-line fsync from patch 0036) for the
# menu-reached signal rather than a pre-launch cache heuristic: a stale READY
# sentinel from a PRIOR binary can be present while THIS binary still cold-
# renders (rc4 exposed exactly this -- a prior-build sentinel made a naive
# presence check skip the wait). Polling what boot ACTUALLY did is robust to
# every cache state (cold / warm / content-hit / stale-sentinel). Breaks as
# soon as the menu is reached (fast on a warm boot); bounded fall-through so a
# real precache HANG or a genuinely dead 0224 still surfaces honestly.
DBG_LOG="$REPO_ROOT/build/stage/LOGS/DEBUG.LOG"
MENU_SIGNAL='menu slide-in fixed-timestep'   # 0224 emits at the post-textbox.Init menu eval
log "waiting for boot to reach the title menu (up to 240s; covers a cold-cache 0266 precache)..."
menu_reached=0
for _ in $(seq 1 240); do
  if [[ -f "$DBG_LOG" ]] && grep -q "$MENU_SIGNAL" "$DBG_LOG" 2>/dev/null; then
    menu_reached=1
    break
  fi
  sleep 1
done
if [[ "$menu_reached" == 1 ]]; then
  log "boot reached the title menu; proceeding to milestone sequence."
  sleep 2   # let the menu settle / first frame render
else
  log "WARN: title menu not reached within 240s -- proceeding; gate will report honestly."
fi

# Milestone sequence. Timing comes from observation: under --fast the engine
# init-to-title takes ~3-4s; the title-screen menu accepts input immediately.
# Adjust if the parity config (--parity) is in use.

sleep 5
shoot "01-title"

# Press Z to confirm "New game" (Cave Story default action key).
key "z"
sleep 4
shoot "02-post-title"

# The post-title state for NXEngine-evo can be either an intro scene or
# a direct cut to first stage; either way, hold a few seconds and capture
# the steady-state render.
sleep 6
shoot "03-mid-scene"

# Try advancing dialogue / skipping intro with another Z press.
key "z"
sleep 3
shoot "04-after-z2"

# Probe player input: send a Right arrow press, see if the screen changes.
# Held key -- release shortly after.
DISPLAY=$DISPLAY xdotool keydown Right
sleep 1.5
DISPLAY=$DISPLAY xdotool keyup Right
sleep 1
shoot "05-moved-right"

DISPLAY=$DISPLAY xdotool keydown Left
sleep 1.5
DISPLAY=$DISPLAY xdotool keyup Left
sleep 1
shoot "06-moved-left"

# Try jump.
key "z"
sleep 0.5
shoot "07-jumped"

sleep 2
shoot "08-final"

# Capture engine-side logs before killing DOSBox-X. DJGPP fopen writes uppercase
# 8.3 names; the staged Linux tree is case-sensitive. wave-53 patch 0148
# relocates the engine log to build/stage/LOGS/DEBUG.LOG and SDL/0062 relocates
# the SDL log to build/stage/DOSKUTSU/LOGS/SDLDBG.LOG. Both sides mkdir LOGS/ at
# runtime and fall back to the pre-wave-53 root path if mkdir fails, so the cp
# below tries LOGS/ first then the legacy root location. Patch 0036 (nxengine)
# + SDL/0024 fsync-per-line make these readable without waiting for DOSBox-X to
# exit, but we still kill the process first so the gate runs on a fully-flushed
# log without a race window.
if [[ "$KEEP_RUNNING" == "0" ]]; then
  log "killing DOSBox-X..."
  dbx_kill_conf "$CONF" || true
  sleep 2
  if ! cp "$REPO_ROOT/build/stage/LOGS/DEBUG.LOG" "$OUT_DIR/debug.log" 2>/dev/null; then
    cp "$REPO_ROOT/build/stage/DEBUG.LOG" "$OUT_DIR/debug.log" 2>/dev/null \
      && log "note: engine log found at legacy root path (LOGS/ mkdir fell back)" \
      || log "no debug.log captured (looked at build/stage/LOGS/DEBUG.LOG + root)"
  fi
  if ! cp "$REPO_ROOT/build/stage/DOSKUTSU/LOGS/SDLDBG.LOG" "$OUT_DIR/sdldbg.log" 2>/dev/null; then
    cp "$REPO_ROOT/build/stage/DOSKUTSU/SDLDBG.LOG" "$OUT_DIR/sdldbg.log" 2>/dev/null \
      && log "note: SDL log found at legacy root path (LOGS/ mkdir fell back)" \
      || log "no sdldbg.log captured (looked at build/stage/DOSKUTSU/LOGS/SDLDBG.LOG + root)"
  fi
fi

# Quick error-count summary so a human reviewer can spot regressions fast.
DEBUG_LOG="$OUT_DIR/debug.log"
SDLDBG_LOG="$OUT_DIR/sdldbg.log"
if [[ -f "$DEBUG_LOG" ]]; then
  # grep -c outputs "0" + exit 1 on zero matches; `|| true` suppresses the
  # non-zero exit without appending a second "0" the way `|| echo 0` would.
  ERR_COUNT=$(grep -c '\[error\]' "$DEBUG_LOG" 2>/dev/null || true)
  CRIT_COUNT=$(grep -c '\[critical\]' "$DEBUG_LOG" 2>/dev/null || true)
  DRAWSURF_COUNT=$(grep -c "drawSurface.*invalid" "$DEBUG_LOG" 2>/dev/null || true)
  log "debug.log: ${ERR_COUNT:-0} errors, ${CRIT_COUNT:-0} criticals, ${DRAWSURF_COUNT:-0} drawSurface-invalid"
fi
if [[ -f "$SDLDBG_LOG" ]]; then
  log "sdldbg.log: $(wc -l <"$SDLDBG_LOG") lines"
fi

# Banner-emit gate. `strings | grep` of the binary proves the literal lives in
# .rodata but NOT that the surrounding LOG_* call site is reachable from
# runtime -- dead code paths keep their string literals. This gate captures the
# logs after the smoke run and requires each regex below to match >=1 line in
# DEBUG.LOG  U  SDLDBG.LOG. Regex alternations cover both default-ON and
# default-OFF variants so the gate stays correct across killswitch flips and
# only fails when the call site itself is dead. Add a new entry whenever a
# lever ships a boot/init banner. See CLAUDE.md sec. Critical Rules sec. Build
# verification.
#
# Parallel arrays (indexed by position). REGEX uses `|` for alternations so
# fields are kept separate rather than packed-and-split.

BANNER_REGEX=(
  "Renderer::initVideo: opaque-tile fastpath (ENABLED|DISABLED)"
  "Renderer::initVideo: Cirrus BLT solid-fill consumer (ENABLED|DISABLED)"
  "sdl: SDL/0059 Cirrus BLT solid-fill (ACTIVE|DISABLED|N/A)"
  "cirrus-blt-async: (enabled|disabled)"
  "gameloop: (legacy combined-tick path|Mechanism A.2 tick split ACTIVE)"
  "\[0142 abl_cache_test n="
  "sdl: SDL/0060 Cirrus BLT pattern-copy (ACTIVE|DISABLED|N/A)"
  "Sound system: MidiScheduler armed with MidiBackendWaveBlaster|Sound system: WB probe failed; falling back to organya"
  "Sound system: MidiScheduler armed with MidiBackendOpl3|Sound system: OPL3 probe failed; falling back to organya"
  "tas: (record opened|replay opened)"
  "tas: (auto-exit at tick|end-of-replay auto-exit at tick)"
  "snd-shutdown\[0/6\]: pre-MidiScheduler::silence_for_shutdown"
  "wb backend: silence_for_shutdown"
  "MidiScheduler: MIDI source = 'orgmid' variant=v[12]"
  "\[RUNMANIFEST-BEGIN\]"
  "\[RUNMANIFEST-END\]"
  "\[runmanifest-emit\] schema=v"
  "audio backend: opl3 \(default since wave 46 patch 0139"
  "audio close: (16-bit|8-bit) DSP halt-then-exit \(0x(D5/D9|D0/DA)/D3/RESET\)"
  "opl3 backend: opl3bank\.dat (loaded|not found|invalid|truncated|version mismatch|too many programs)"
  "opl3 backend: cleared reg 0xBD chip-wide latch \(wub-wub mitigation per flush-instr WAVE-46 H6\)"
  "map: BG skip-when-unchanged (ENABLED \(opt-in\)|DISABLED \(default\))"
  "bg-skip: (first event|backdrop redraw skipped)"
  "sdl: wave-49 Cirrus BLTCopy: (ENABLED|DISABLED)"
  "sdl: wave-49 Cirrus BLTCopy: first BLT success"
  "map: mds_clear LUT (ENABLED \(default\)|DISABLED \(killswitch\))"
  "mds-clear-lut: rebuilt N=[0-9]+ entries \(k_skip=[0-9]+% k_keep=[0-9]+%"
  "sdl: wave-50 VRAM-resident page-flip (ENABLED|DISABLED)"
  "sdl: wave-50 VRAM-resident page-flip first swap"
  "Renderer::initVideo: asm opaque-span blit lever (ENABLED|DISABLED)"
  "Renderer::initVideo: blit-decomp .*instrumentation (ENABLED|DISABLED)"
  "Renderer::initVideo: mds-scroll-decomp instrumentation (ENABLED|DISABLED)"
  "map: skip-redundant-clear lever (ENABLED|DISABLED)"
  "Logger::init: wave-53 log level=(INFO|WARN)"
  "perf-mode: level=[0-2]"
  "perf-mode B2: decorative FG detail dropped"
  "fixed-timestep: (ENABLED|DISABLED)"
  "audio: SDL/0063 WaitDevice ring-drain timeout active"
  "audio: SDL/0066 audio-thread hard-park active"
  "audio: SDL/0067 SDL_DOSAudioPump export available"
  "audio: SDL/0068 SDL_DOSAudioPump TryLock\\+timeout backstop active"
  "audio: SDL/0069 cumulative silent-IRQ counter export: ENABLED"
  "audio: SDL/0070 v2 Pixtone probe pix_active histogram \\+ irq_delta fold-in"
  "audio: SDL/0071 pixtone IRQ-mix: (ENABLED|DISABLED|REQUESTED)"
  "audio: SDL/0072 midi tick from ISR: (ENABLED|DISABLED)"
  "audio mid-gap pump: (ENABLED|DISABLED)"
  "data cache: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "audio tick-boundary pump: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "pixtone multi-source probe: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "pixtone IRQ-mix: (ENABLED \(default; SDL backend engaged;.*\)|DISABLED \(L3 requested but SDL backend refused;.*\)|DISABLED \(killswitch;.*\))"
  "midi ISR tick: engine (registered tick_isr callback \(L2b active.*\)|skipped registration.*)"
  "audio: SDL/0073 rate-diag: req_freq="
  "audio: SDL/0074 RATEDIV auto: .*master_rate=11025"
  "audio: SDL/0074 SB16 mixer balance: master=31 voice=28 fm=28"
  "audio: SDL/0071 pixtone IRQ-mix: ENABLED"
  "audio: SDL/0072 midi tick from ISR: ENABLED"
  "audio: SDL/0074 RATEDIV auto: dev_freq="
  "audio: SDL/0074 SB16 mixer balance: (master=|DISABLED)"
  "midi ISR tick: engine registered tick_isr callback \(L2b active.*\)"
  "opl3 backend: PATCH_ORGAN \+ PATCH_MALLET RR=8"
  "audio: SDL/0079 TL=0x3F sweep applied \(18 voices"
  "mpu401: direct-port init at port_base=0x[0-9a-fA-F]+ \(default since patch 0080"
  "load-stage trace: (ENABLED \(opt-in.*\)|DISABLED)"
  "\\[load-stage-phase\\] cave=[0-9]+ phase=(sheets_flush|tileset_load|load_map|load_tileattr|load_entities|tsc_load|backdrop_set|load_meta|TOTAL) wall_ms=[0-9]+( phases=[0-9]+)?"
  "frame-spike detect: (ENABLED \(opt-in.*\)|DISABLED)"
  "\\[frame-spike\\] inter_ms=[0-9]+"
  "firepath trace: (ENABLED \(opt-in.*\)|DISABLED)"
  "\\[firepath\\] func=(FireWeapon|CreateBullet|effect|ai_polar_shot) wall_us=[0-9]+"
  "sheet-load trace: (ENABLED \(opt-in.*\)|DISABLED)"
  "\\[sheet-load\\] cave=[0-9]+ sheetno=[0-9]+ wall_us=[0-9]+"
  "eager sheet reload: (ENABLED \(opt-in.*\)|DISABLED)"
  "\\[eager-sheet-reload\\] cave=[0-9]+ sheets=[0-9]+ wall_ms=[0-9]+"
  "IO audit: (ENABLED \(opt-in.*\)|DISABLED)"
  "\\[io-audit\\] phase=(boot|load_stage|gameplay) op=[A-Za-z_]+ file=.* bytes=-?[0-9]+ wall_us=[0-9]+"
  "skip sheet flush: (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "\\[skip-sheet-flush\\] cave=[0-9]+"
  "load_stage: entry-music preload (ENABLED \(default\)|DISABLED \(killswitch\))"
  "title: eager-title-IO phase tag (ENABLED \(default\)|DISABLED \(killswitch\))"
  "blitPatternAcross src-clamp (ENABLED \(default\)|DISABLED \(killswitch\))"
  "backdrop-before-music reorder (ENABLED|disabled \(default\))"
  "exit-(pre-return|atexit-first|atexit-last)"
  "DEV WARP: new game -> stage [0-9]+"
  "starve-diag: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "title backdrop pre-decode: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "title audio pump \(run_tick\): (ENABLED \(opt-in\)|DISABLED \(default\))"
  "audio: SDL/0082 STARVE_DIAG=1 -- Bug-5 ring/silent-IRQ starve markers ENABLED \(snd: layer\)"
  "audio: SDL/0086 in-band COOP_YIELD ENABLED \(default-ON\) -- Bug-5 organya cooperative-scheduler monopoly fix"
  "audio: SDL/0090 device-rate setter default: [0-9]+ -> [0-9]+ Hz"
  "org-stream-diag: ORG_STREAM_DIAG=1 -- Bug-5 organya stream production metrics ENABLED"
  "org-lookahead: ORG_LOOKAHEAD=1 -- Bug-5 organya stream look-ahead buffer ENABLED"
  "audio tick-boundary pump CAP: [0-9]+ "
  "Sound system: device frame-rate: requesting [0-9]+ Hz match to master"
  "eager action sheets: (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "organya render quiesce: (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "\[org-precache\] TOTAL rate=[0-9]+ channels=[0-9]+ rendered=[0-9]+ skipped=[0-9]+ failed=[0-9]+ bytes=[0-9]+"
  "config: (loaded DOSKUTSU.CFG \([0-9]+ keys\)|no DOSKUTSU.CFG, using built-in defaults)"
  "organya auto-precache: (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "Renderer::initVideo: DOS resolution-label lock (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "menu slide-in fixed-timestep: (ENABLED \(default;.*\)|DISABLED \(killswitch;.*\))"
  "MidiScheduler: MIDI source = custom drop-in dir"
  "\[input-bind\] applied=[0-9]+"
  "\[joycal\] applied stored cal"
  "\[org-hq\] stereo prerender rate=22050 ch=2"
  "audio: SDL/0106 SB DMA-path decision: is_sb16=[01] force_8bit=[01] dsp_ver=-?[0-9]+ highdma=-?[0-9]+ -> (16-bit-high-DMA|8-bit-low-DMA) path"
  "mpu401: SDL/0106 WB init result=(OK|FAILED) port_base=0x[0-9a-fA-F]+ drr_poll_enabled=[01] drr_cap=[0-9]+ drr_cap_hits=[0-9]+"
  "audio: SDL/0107 8-bit channel mode: ch=[12] \((MONO|STEREO)\) tc=-?[0-9]+ effective=[0-9]+ Hz stereo_bit=(yes|no) ring_silence_primed=yes"
  "audio: SDL/0108 pixtone IRQ-mix format: (S16-stereo|U8-stereo|U8-mono) \(device is_16bit=[01] channels=[12]\)"
  "audio: SDL/0109 DSP [0-9]+\.[0-9]+ reports SB16 but BLASTER has no VALID 16-bit DMA channel \(highdma=-?[0-9]+; valid = 5/6/7\)"
  "audio backend: adlib \(Campaign 2 -- native OPL2 FM synthesis"
  "Sound system: AdLib \(OPL2\) path -- AUDIO_BACKEND=adlib"
  "(adlib|gus): music pump STARTED at [0-9]+ Hz"
  "audio backend: gus \(Campaign 3 -- native Gravis Ultrasound GF1"
  "Sound system: GUS \(GF1\) path -- AUDIO_BACKEND=gus"
  "gus backend ready: GF1 detected"
  "audio: SDL/0112 GUS GF1 detect: present=[01]"
  "gus backend: on_song_start -- uploaded [0-9]+ instruments"
  "gus SFX: uploaded [0-9]+ of [0-9]+ .pxt to GF1 DRAM"
  "Sound system: GUS path -- Pixtone SFX uploaded to GF1 voices"
  "Sound system: 4-state audio enable \(#31\) -- music=(on|OFF) sfx=(on|OFF)"
  "audio backend: none \(No Music -- #31\)"
  "Sound system: GUS path -- SFX DISABLED .*MUSIC-ONLY GUS"
  "audio: SDL/0112 GUS device up: .*upload=pio"
  "audio: SDL/0112 GUS TEST-TONE16: voice=[0-9]+ addr=0x[0-9A-Fa-f]+ end=0x[0-9A-Fa-f]+ playing"
  "gus backend: release-ramp (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "gus backend: multisample (ENABLED \(default\)|DISABLED \(killswitch =0\))"
  "audio: SDL/0113 GUS SFX bank-align: ENGAGED \(8-bit no-straddle 256K guard\)"
  "gus SFX DRAM-straddle diag \[nx0256\]"
  "gus SFX rendersafe \[nx0257\]: (ON|OFF \(default\))"
  "gus SFX voice routing \[nx0259\]:"
  "gus SFX gain \[nx0259\]: [0-9]+%"
  "sdl: SDL/0115 BANK-GRAN-FIX (ENABLED|DISABLED)"
  "sdl: SDL/0116 VBLANK-BOUND (ENABLED|DISABLED)"
  "Sound system: AdLib PC-speaker SFX beeps -- engine SFX->beep mapping WIRED"
  "audio: SDL/0118 \[pcspk\] square-wave SFX (ENABLED|DISABLED)"
  "load_stage ring\+DMA silence flush: (ENABLED|DISABLED)"
  "ring\+DMA silence quiesce"
  "\[loadband-stat cave=[0-9]+"
)
BANNER_SEVERITY=(
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "forbidden"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "required"
  "required"
  "required"
  "required"
  "optional"
  "optional"
  "required"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "required"
  "optional"
  "required"
  "required"
  "optional"
  "required"
  "optional"
  "optional"
  "required"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
  "required"
  "required"
  "optional"
  "optional"
  "optional"
  "optional"
  "optional"
)
# BANNER_LABEL is parallel to BANNER_REGEX/BANNER_SEVERITY -- ALL THREE are 137 entries
# each and MUST stay 1:1 (re-aligned in the v1.6.2 rc5 window: the GUS Campaign-3
# regexes idx 111-129 had no labels, so SDL/0115..0268 labels displayed against the
# wrong regexes and the real scores printed with an empty "[]" label -- purely cosmetic,
# but it misled a reader chasing a 0120 "emits=0" that was actually the mislabel). The
# gate loop indexes label[$i] alongside regex[$i]. KEEP THEM IN LOCKSTEP: when you add a
# BANNER_REGEX + BANNER_SEVERITY entry, add a matching BANNER_LABEL line at the SAME index
# (the `${BANNER_LABEL[$i]:-}` guard below still tolerates a gap, but keep them equal).
# Labels are DISPLAY-ONLY -- pass/fail keys on regex+severity -- but an aligned label makes the
# gate output readable.
BANNER_LABEL=(
  "lever-1 opaque-tile fastpath (patch 0137)"
  "lever-2b nx-engine consumer (patch 0138)"
  "lever-2a SDL primitive (patch SDL/0059)"
  "lever-3 BULK_COPY async (patch 0139)"
  "A.2 gameloop tick (patch 0141)"
  "abl-cache disambiguation bench (patch 0142)"
  "BLTPAT primitive (patch SDL/0060)"
  "wave-41 WB-engaged path (AUDIO_BACKEND=wb -> probe success OR fallback narration; optional so default smoke does not gate-fail when env unset)"
  "wave-44 OPL3-engaged path (AUDIO_BACKEND=opl3 -> probe success OR fallback narration; optional so default smoke does not gate-fail when env unset; PLAY1-PLAY4 in wave-44 matrix exercise this banner)"
  "wave-41 TAS record/replay engaged (patch 0135; optional -- emits only when DOSKUTSU_TAS_RECORD or _REPLAY env var is set; default smoke runs with neither so banner absent is correct)"
  "wave-41 TAS clean termination (patch 0135; optional -- emits either end-of-replay-auto-exit (default; recording exhausted) or auto-exit-at-tick (AUTO_EXIT_TICK reached); EOF-auto-exit is the default-ON behavior since patch 0135 v1.1)"
  "wave-42 #18 MidiScheduler shutdown narration (patch 0136; optional -- fires on every clean engine exit through SoundManager::shutdown; emits regardless of backend choice; absence under smoke means pkill-on-timeout kill-path, not a bug)"
  "wave-42 #18 WB silence-for-shutdown override (patch 0136; optional -- WB-only; emits only under AUDIO_BACKEND=wb on clean exit; fixes the wave-41 hanging-MIDI-note-on-quit bug)"
  "wave-42 #19 GM_VARIANT engaged (patch 0137; optional -- emits when SDL_HINT_DOSKUTSU_AUDIO_MIDI_GM_VARIANT=v1 or v2 selects data/orgmid1// or data/orgmid2/; default smoke leaves env unset so default-orgmid path runs and banner is absent)"
  "wave-43 RUNMANIFEST block begin sentinel (patch 0138; optional -- fires on every clean engine exit; absence under smoke means pkill-on-timeout kill-path, not a bug; severity matches snd-shutdown[0/6] precedent)"
  "wave-43 RUNMANIFEST block end sentinel (patch 0138; optional -- pairs with begin sentinel; the smoke gate can extract the schema-v1 block via awk '/RUNMANIFEST-BEGIN/,/RUNMANIFEST-END/' for post-process consumption per WAVE-41-TRI-ENV-CORRELATION-PLAN sec. 4.4)"
  "wave-43 RUNMANIFEST engagement banner (patch 0138; optional -- the runtime-witness side of the two-witness pattern; complements strings|grep RUNMANIFEST-BEGIN which only proves embed)"
  "wave-46 default OPL3 backend (patch 0139; optional -- emits when SDL_HINT_DOSKUTSU_AUDIO_BACKEND is unset, which is the wave-46 production default; absent under explicit AUDIO_BACKEND=organya/wb/auto override)"
  "wave-46 DSP RESET in audio close (patch SDL/0059; optional -- fires on every clean engine exit through the SDL_dosaudio_sb teardown path; emits in both 16-bit and 8-bit DSP variants; absence under smoke means pkill-on-timeout kill-path, not a bug)"
  "wave-46 OPL3 patch-bank loader (patch 0141; renamed bank file data/opl3bank.dat per patch 0232 8.3 fix; optional -- emits when the MidiBackendOpl3 ctor runs, i.e. when OPL3 is detected; one of: loaded / not found / invalid / truncated / version mismatch / too many programs. As of patches 0232+0233 the full DMXOPL-derived 128-program bank SHIPS as data/opl3bank.dat, so the expected state is 'loaded 128 programs' whenever OPL3 is present. ABSENT under the default oplmode=none smoke config -- the OPL3 probe fails there and the ctor never runs, which is expected, not a failure; the runtime witness is an oplmode=opl3 config. Embed witness = strings|grep opl3bank.dat.)"
  "wave-47 OPL3 reg 0xBD clear (patch 0142; optional -- emits in MidiBackendOpl3 dtor on clean exit; pkill-on-timeout DOSBox-X smoke won't fire this banner; absent under explicit AUDIO_BACKEND=organya/wb/auto override or under OPL3 probe failure path; wub-wub mitigation per flush-instr WAVE-46 H6 60-70% probability)"
  "wave-48 BG skip-when-unchanged decision (patch 0143; default flipped to OFF by patch 0157/wave-61 -- bg-skip abandoned, it shipped the backdrop-black flicker; optional -- one banner per process emitted on first call to map_backdrop_should_skip_clear_and_draw, narrating ENABLED (opt-in via SDL_HINT_DOSKUTSU_BG_SKIP_WHEN_UNCHANGED=1) vs DISABLED (default); fires deterministically on first DrawScene call so should emit even under DOSBox-X smoke if engine reaches DrawScene at all)"
  "wave-48 BG skip engagement (patch 0143; optional -- fires the first time scroll-state key matches last-drawn frame, then again every 100 skip events; absence under smoke may indicate either (a) zero-skip-eligible frames in the test scene (e.g. attract reel scrolls every frame) or (b) the killswitch path is engaged; check the killswitch decision banner to disambiguate)"
  "wave-49 Cirrus BLTCopy chip-detect (patch SDL/0060; optional -- one banner per process emitted on first call to SDL_DOSVesaDirect_IsCirrusBLTAvailable() or SDL_DOSVesaDirect_BLTCopy(), narrating ENABLED (Cirrus 5434 detected) vs DISABLED (chip not 5434, or killswitch SDL_HINT_DOSKUTSU_CIRRUS_BLT=0); DOSBox-X smoke typically emits DISABLED because emulated chip isn't Cirrus 5434; real-HW g2k smoke emits ENABLED; absence on real-HW indicates the engine never wired up the helper at all)"
  "wave-49 Cirrus BLTCopy first-success (patch SDL/0060; optional -- one banner per process emitted on the FIRST successful chip-driven BLT, narrating src/dst offsets + rect size; the runtime-witness side of the two-witness pattern complementing strings|grep \"wave-49 Cirrus BLTCopy\"; absence (with ENABLED banner present) means engine code wired up IsCirrusBLTAvailable() but never called BLTCopy() in the smoke window; absence (with DISABLED banner) is expected -- no BLT issued)"
  "wave-49 mds_clear LUT killswitch decision (patch 0144; optional -- one banner per process emitted on first call to _mds_clear_lut_killswitch_engaged inside DrawScene MdsClearTimer scope, narrating ENABLED (default) vs DISABLED (killswitch=0); fires deterministically on first DrawScene call when not skipped by wave-48 patch 0143; absence under smoke may mean wave-48 bg-skip path always engaged in test scene)"
  "wave-49 mds_clear LUT rebuild (patch 0144; optional -- emits at each LUT rebuild: map load OR tile mutation (TSC <CMP, map_ChangeTileWithSmoke); narrates k_skip% and k_keep% per the design's sec. 5 outcome thresholds; multiple emits per session expected as the player crosses maps; absence means either (a) wave-48 bg-skip always engages so the clearScreen never runs, or (b) killswitch is on; the killswitch decision banner disambiguates)"
  "wave-50 VRAM-resident page-flip chip-detect (patch SDL/0061; optional -- one banner per process emitted on first call to _vram_resident_check_enabled inside SDL_DOSVesaDirectPresentFull's divert OR via SDL_DOSVesaPagePresent, narrating ENABLED (Cirrus 5434 + sanity-check pass) vs DISABLED (killswitch off / chip not 5434 / sanity fail); DOSBox-X smoke typically emits DISABLED (emulated chip != Cirrus 5434) UNLESS SDL_HINT_DOSKUTSU_VRAM_RESIDENT=1 is set; real-HW g2k cycle-1 iter sets the env var so ENABLED is expected; absence on real-HW with env set indicates the killswitch was unset OR the present path never engaged the divert -- check SDL_HINT_DOSKUTSU_CIRRUS_BLT not =0 either since the chip-detect is shared with SDL/0060)"
  "wave-50 VRAM-resident page-flip first swap (patch SDL/0061; optional -- one banner per process emitted on the FIRST successful divert + CRTC swap, narrating back_page_byte_offset + new visible_page index; the runtime-witness side of the two-witness pattern complementing strings|grep \"wave-50 VRAM-resident\"; absence (with ENABLED banner present) means the divert engaged but SDL_DOSVesaDirectPresentFull was never called in the smoke window (engine on legacy SDL_UpdateWindowSurface path or smoke killed before first present); absence (with DISABLED banner) is expected -- no swap issued)"
  "wave-51 asm opaque-span blit lever (patch 0145; optional -- one banner per process emitted in Renderer::initVideo, narrating ENABLED via SDL_HINT_DOSKUTSU_ASM_BLIT=1 vs DISABLED (default-OFF for wave-51); fires deterministically at video init so emits under DOSBox-X smoke; default smoke leaves the hint unset so DISABLED is the expected smoke verdict)"
  "wave-51 blit-decomp instrumentation (patch 0146; optional -- one banner per process emitted in Renderer::initVideo, narrating ENABLED via SDL_HINT_DOSKUTSU_BLIT_DECOMP=1 vs DISABLED (default-OFF); the .* in the regex spans the ENABLED variant's extra '_blit_indexed sub-decomp' words; default smoke leaves the hint unset so DISABLED is expected; the two INSTR PLAY cells in the wave-51 matrix exercise the ENABLED path)"
  "wave-52 mds-scroll-decomp instrumentation (patch 0147; optional -- one banner per process emitted in Renderer::initVideo, narrating ENABLED via SDL_HINT_DOSKUTSU_MDS_DECOMP=1 vs DISABLED (default-OFF); fires deterministically at video init; default smoke leaves the hint unset so DISABLED is expected; the wave-52 PLAY1 cell exercises the ENABLED path -- per-100-scene-draws [mds-decomp-stat]/[mds-decomp-route]/[mds-decomp-pass]/[mds-decomp-frames] emit lines)"
  "wave-53 skip-redundant-clear lever (patch 0149; optional -- LOG_INFO banner narrating ENABLED via SDL_HINT_DOSKUTSU_SKIP_REDUNDANT_CLEAR=1 vs DISABLED (default-OFF); INFO-level so it only appears when the run logs at INFO -- tagged runs, or untagged with DOSKUTSU_LOG_VERBOSE=1; absent on a plain untagged WARN-level run, which is expected, not a failure)"
  "wave-53 Logger log-level banner (patch 0148; optional -- emitted directly by Logger::init regardless of level, narrating INFO vs WARN; untagged production runs default to WARN, tagged runs and DOSKUTSU_LOG_VERBOSE=1 force INFO; the runtime witness that the wave-53 WARN-when-untagged default engaged)"
  "wave-54 PERF_MODE level banner (patch 0150; optional -- LOG_INFO emitted on the first perf_mode_level() call, which is the first map_draw_backdrop call, narrating level=0 faithful (default) / 1 smooth / 2 fast; INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1, absent on a plain untagged WARN-level run which is expected not a failure; the runtime witness that the SDL_HINT_DOSKUTSU_PERF_MODE infra was reached)"
  "wave-55 cut B2 decorative-FG-detail first-fire (patch 0151; optional -- LOG_INFO emitted once on the first frame B2 actually filters a purely-decorative foreground tile (attribute word == TA_FOREGROUND exactly); the two-witness runtime side complementing strings|grep \"perf-mode B2\" which proves embed only; fires ONLY when the smoke run boots with SDL_HINT_DOSKUTSU_PERF_MODE>=1 AND reaches a map that has at least one decorative FG tile -- a level-0 boot, or a scene whose FG layer is all collision/slope tiles, never fires it, which is expected not a failure. NOTE the wave-54 A2 banner was removed: A2 gain-collapsed on real HW and its hunk was reverted by patch 0151)"
  "wave-57 FIXED_TIMESTEP killswitch decision (patch 0153; default flipped ON by patch 0159 wave-63, the 1.0 release; optional -- LOG_INFO emitted on the first fixed_timestep_active() call (the first gameloop iteration), narrating ENABLED (default; logic 50 Hz decoupled from render) vs DISABLED (SDL_HINT_DOSKUTSU_FIXED_TIMESTEP=0 killswitch; 1:1 logic/render); INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1, absent on a plain untagged WARN-level run which is expected not a failure; the runtime witness that the Track B B2 accumulator engaged -- on a default boot the ENABLED variant confirms the 1.0 default 50 Hz fixed-timestep mode)"
   "SDL/0063 WaitDevice ring-drain timeout (486-campaign Bug 2 quit-to-DOS hang fix; optional -- emitted at audio OpenDevice (early boot) on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG -- the smoke gate greps both), default-ON; the disabled variant emits only when SDL_HINT_DOSKUTSU_DOS_AUDIO_WAITDEVICE_TIMEOUT=0; the runtime witness that the SDL/0063 ring-drain teardown-deadlock backstop is live -- the quit-hang itself reproduces only on real 486DX2 hardware, so this banner is the build-qa runtime-invocation confirmation, not a hang test)"
   "SDL/0066 audio-thread hard-park (486-campaign Bug 2 quit-to-DOS hang fix proper -- the targeted fix that SDL/0063 did not deliver; optional -- emitted at audio OpenDevice (early boot) on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG), default-ON; the disabled variant emits only when SDL_HINT_DOSKUTSU_DOS_AUDIO_SHUTDOWN_PARK=0; the runtime witness that the SDL/0066 audio-thread teardown-park is live -- the engagement banner audio: SDL/0066 hard-park ENGAGED is emitted only at quit when SDL_DOSAudioBeginShutdown fires, so DOSBox-X smoke confirms the active-at-init banner but not the engagement; the actual hang reproduces only on real 486DX2 hardware, so this active-at-init banner is the build-qa runtime-invocation confirmation, not a hang test)"
   "SDL/0067 SDL_DOSAudioPump export available (audio mid-gap-service export for 486-campaign Bug 1 / gap-driven SFX stutter; optional -- emitted at audio OpenDevice (early boot) on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG); unconditional -- the SDL-side export is always available; what gates the engine call-sites is the engine-side SDL_HINT_DOSKUTSU_AUDIO_MIDGAP_PUMP env var read by nxengine-evo/0162 (Leg A pump-call patch). The active-at-init banner is the runtime witness that the SDL_DOSAudioPump symbol is wired and the audio backend's OpenDevice ran; nx-engine emits its own engine-side engagement banner by default (default-ON; the killswitch SDL_HINT_DOSKUTSU_AUDIO_MIDGAP_PUMP=0 emits the DISABLED variant instead). The actual SFX-stutter mitigation reproduces only on real DX2-66 hardware (the cooperative-scheduler gap-driven underrun); DOSBox-X smoke confirms the export embed + boot path, not the perf mitigation)"
   "SDL/0068 SDL_DOSAudioPump TryLock+timeout backstop (cooperative-scheduler-starvation guard for the SDL_DOSAudioPump path; optional -- emitted at audio OpenDevice (early boot) on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG), default-ON with 100 ms wall-clock cap; the disabled variant emits only when SDL_HINT_DOSKUTSU_DOS_AUDIO_PUMP_TIMEOUT=0; the runtime witness that the SDL/0068 backstop is live -- the wedge the backstop guards against (build-qa #84 bisect, wave-84 combined-iter: SDL_DOSAudioPump's implicit-blocking-lock starves under DOSBox-X SB16 emulation when the playback thread's WaitDevice never DOS_Yields) reproduces only under that DOSBox-X cadence regime, so on emulation the backstop's presence is what makes the gate-pass possible; on real DX2-66 the natural ~43 Hz SB16 IRQ-5 makes WaitDevice yield per cycle and the backstop is unused defensive insurance. SDL/0068 active means SDL_DOSAudioPump can be called from main without the wave-84 wedge regardless of which side the regime falls)"
   "SDL/0069 cumulative silent-IRQ counter export (Pixtone multi-source probe hypothesis-C signal; optional -- emitted at audio OpenDevice (early boot) on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG), unconditional -- no killswitch, no env var; the SDL-side counter doskutsu_audio_silent_irq_count is always live as a single 32-bit volatile increment in the IRQ silent-branch alongside the existing isr_consecutive_silent_irqs (which resets on any non-silent IRQ AND on DSP-resume service, losing intra-block peaks). The sibling counter is monotonic; the engine-side Pixtone probe reads it via extern at audit-flush boundary and computes block-over-block deltas to detect ring-empty bursts during rapid-fire SFX moments. The active-at-init banner is the runtime witness that the counter symbol is wired and the audio backend's OpenDevice ran; nx-engine's Pixtone probe (engine-side, opt-in via SDL_HINT_DOSKUTSU_PIXTONE_PROBE=1) emits its own engagement banner separately. See docs/internal/PIXTONE-MULTISOURCE-DESIGN.md sec.2.1 #2 + sec.4 #4)"
   "SDL/0070 v2 Pixtone probe pix_active histogram + irq_delta fold-in (resolves v1 hypothesis-E instrumentation gap + sec.21.4.1 silent_irq numeric anomaly; optional -- emitted at audio OpenDevice (early boot) on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG). The active-at-init banner is the runtime witness that the v2 probe machinery is wired. The pix_active histogram + irq_delta fields extend the existing [sdl-audiocb] block emit; their engagement is gated on FINE_INSTR=1 (rides on g_c_sfx_audiocb_instr_active from SDL/0064; no new env var). When FINE_INSTR=1, every 100 PlayDevice invocations the [sdl-audiocb] block emits with pix_active min/med/p90/max samples=N + irq_delta=M. v1's engine-side histogram (nxengine 0165's SamplePixtoneActiveBucket hooked to organyaStreamCallback) fired only under the Organya backend; OPL3 (wave-46 default since 2026-05-14) bypassed it -> samples=0 in v1 iter -> hypothesis-E verdict. v2's SDL-side sampler at the top of DOSSOUNDBLASTER_PlayDevice is the unified-cadence point that fires regardless of music backend (OPL3 / Organya / WaveBlaster all funnel through SDL3_mixer -> PlayDevice). The irq_delta fold-in (snapshot of doskutsu_audio_irq_count between block emits) is the IRQ-rate corroborator that resolves the sec.21.4.1 anomaly (v1 silent_irq ~750/block vs design-assumed 43 Hz IRQ predicting ~190 IRQs/block over 4.4 s). See docs/internal/PIXTONE-PROBE-V2-DESIGN.md sec.2 + sec.5)"
   "SDL/0071 pixtone IRQ-mix killswitch decision (Lever 3 structural Bug-2 fix; optional -- LOG_INFO banner emitted at audio OpenDevice on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG), one of three variants: ENABLED (DEFAULT-ON since v1.0.1, patch SDL/0075; set SDL_HINT_DOSKUTSU_PIXTONE_IRQ_MIX=0 to disable), DISABLED (killswitch =0), or REQUESTED (default-on but backend-gate refused because SDL_HINT_DOSKUTSU_AUDIO_BACKEND=organya forces L3 off per LEVER3 sec.5.4). Lever 3 moves Pixtone PCM mix from the cooperative-thread SDL3_mixer ring path INTO the SoundBlasterIRQHandler IRQ-5 ISR -- the IRQ that owns the DMA buffer also does the SFX mix, bypassing the cooperative scheduler entirely for the SFX path. Structural answer to wave-21 / sec.21 / #120 hypothesis-C verdict (cooperative-scheduler audio-thread starvation; up to 1701 ms max single-callback latency on real DX2-66). Default-OFF first iter per [[author_flagged_caveat_overrides_let_data_decide]] + [[single_lever_per_binary_or_else_attribution_impossible]] discipline; SDL3_mixer fallback path stays alive when off so a real-HW DPMI-fault inside the ISR mitigates to disabled rather than freezing. The active-at-init banner is the runtime witness that the Lever 3 wiring engaged or was correctly refused. The actual stutter-elimination effect reproduces only on real DX2-66 hardware; DOSBox-X smoke confirms the banner / boot path but not the cooperative-scheduler bypass behavior per [[dosbox_not_proxy]]. Engine-side routing of Pixtone::play through SDL_DOSPixtoneStart is in nxengine-evo/0167. See docs/internal/LEVER3-PIXTONE-IRQ-MIX-DESIGN.md sec.1-7 + sec.10 contract lock + 486-CLASS-CAMPAIGN-FINDINGS.md sec.21 + #120 sec.22)"
   "SDL/0072 midi tick from ISR killswitch decision (Lever 2b structural Bug-4 fix; optional -- LOG_INFO banner emitted at audio OpenDevice on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT the engine DEBUG.LOG), one of two variants: ENABLED (DEFAULT-ON since v1.0.1, patch SDL/0075; set SDL_HINT_DOSKUTSU_MIDI_ISR_TICK=0 to disable) or DISABLED (killswitch =0). Lever 2b moves MidiScheduler::tick from the cooperative main-loop path INTO the SoundBlasterIRQHandler IRQ-5 ISR -- the IRQ fires at ~43 Hz Tier-2 (about 2x rendered fps on heavy-music DX2-66 scenes), decoupling MIDI tempo accuracy from render-rate. Structural answer to 486-campaign Bug 4 (music-tempo wobble under sub-25 fps gameplay). No backend-gate -- Lever 2b is orthogonal to the music backend choice (the engine simply doesn't register a callback when running under Organya, which doesn't use MidiScheduler; under OPL3 / WB / midi-stub, the engine registers an extern \"C\" shim that invokes MidiScheduler::tick_isr(now_ms) per LEVER2B sec.1.2). The engine registers an ISR-safe callback via SDL_DOSMidiTickRegister (declared in vendor/SDL/include/SDL3/SDL_dosmidi_tick.h); the ISR computes monotonic cumulative now_ms via inter-IRQ delta accumulation through the existing SDL_DOSAudioReadTimer + SDL_DOSAudioTimerDeltaUs primitives. Default-OFF first iter per [[author_flagged_caveat_overrides_let_data_decide]] + [[single_lever_per_binary_or_else_attribution_impossible]] discipline; main-loop MidiScheduler::tick fallback path stays alive when off so a real-HW DPMI-fault inside the registered callback mitigates to disabled rather than freezing. The active-at-init banner is the runtime witness that the Lever 2b wiring engaged. The actual tempo-perception effect reproduces only on real DX2-66 hardware; DOSBox-X smoke confirms the banner / boot path but not the render-rate decoupling behavior per [[dosbox_not_proxy]]. Engine-side routing + tick_isr() body + game-pause-coherent stop/start at pause transitions are in nxengine-evo/0168. See docs/internal/LEVER2B-MIDI-ISR-TICK-DESIGN.md sec.1-7 + sec.8 contract lock + 486-CLASS-CAMPAIGN-FINDINGS.md sec.17 + sec.20)"
  "0162-v2 Leg A audio mid-gap pump (486-campaign Bug 1 stage-load ~1-second-freeze fix, v2 -- the v1 iter showed PLAY1 missed Shack from accumulator-desync, v2 adds FIXED_TIMESTEP freeze/thaw around load_stage and flips default to OFF per flush-instr's safer-landing recommendation; optional -- LOG_INFO emitted on the first MIDGAP_PUMP call (first stage entry), narrating ENABLED (opt-in via SDL_HINT_DOSKUTSU_AUDIO_MIDGAP_PUMP=1) vs DISABLED (default; the lever is off until v3 promotes it back to default-ON after v2 iter validation); INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1, absent on a plain untagged WARN-level run which is expected not a failure; this is the engine-side banner -- complements the SDL-side 'audio: SDL/0067 SDL_DOSAudioPump export available' banner above (the SDL banner confirms the export is wired and reachable; this banner confirms nxengine-evo/0162-v2's killswitch parser engaged and the engine call-sites are live). On a default boot the DISABLED variant is expected (lever off); cells that SET the hint to 1 emit ENABLED and exercise the 6 inter-loader pumps in load_stage + the freeze/thaw RAII guard. The v2 pumps use 'while (SDL_DOSAudioPump()) { }' to top up the ring to full per seam, and the FtAccumulatorGuard at load_stage entry snapshots accum_ns + restores it at every return path so wall-clock spent in load_stage is invisible to the catch-up tick_state count (closes the v1 TAS desync). See docs/internal/AUDIO-MID-GAP-SERVICE-DESIGN.md + 486-CLASS-CAMPAIGN-FINDINGS.md sec.17)"
  "0163 engine data cache (486-campaign Bug 1 stage-load freeze fix -- the structural replacement for Leg A; optional -- LOG_INFO emitted on the first nxe::cache::enabled() call (engine init, right after SDL_Init), narrating ENABLED (opt-in via SDL_HINT_DOSKUTSU_DATA_CACHE=1) vs DISABLED (default; the v1 lever is off until iter validation); INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1, absent on a plain untagged WARN-level run which is expected not a failure; the runtime witness that the cache module is wired and the killswitch parser engaged. On the ENABLED path, additional per-dir progress lines emit -- 'data cache: preloading Stage/', 'data cache: preloading Npc/', 'data cache: preloading top-level *.pbm', 'data cache: preloading pxt/', and the final 'data cache: preload complete -- N files / M bytes resident / S skipped (X bytes malloc-failed)' -- which name the access-pattern preload order and the partial-cache resilience. The cache replaces audio mid-gap pumping (0162-v2) as the structural fix: a single uninterruptible .pbm fread on real DX2-66 CF exceeds the audio ring depth, so caching the .pbm bytes in RAM is the only way to make stage transitions fast enough for the audio ring not to underrun. See docs/internal/DATA-CACHE-DESIGN.md + 486-CLASS-CAMPAIGN-FINDINGS.md sec.18)"
  "0164 Leg B audio tick-boundary pump (486-campaign Bug 2 continuous-SFX-stutter fix; optional -- LOG_INFO emitted on the first tickpump_active() call (the first run_tick_fixed iteration), narrating ENABLED (opt-in via SDL_HINT_DOSKUTSU_AUDIO_TICKPUMP=1) vs DISABLED (default; the v1 lever is off until iter validation); INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1, absent on a plain untagged WARN-level run which is expected not a failure; the runtime witness that the tick-boundary TICKPUMP macro is wired and the killswitch parser engaged. On the ENABLED path, a single TICKPUMP() call site fires in run_tick_fixed immediately before game.tick_render_submit() once per displayed frame (both 1:1 and catch-up paths converge here), running while(SDL_DOSAudioPump()){} to top up the SB16 ring to full -- bounded by ring depth (~16 iterations worst-case from empty, 1-2 in steady-state at 30 fps); in the silent case (no SFX/Organya/MIDI) the loop body executes zero times so the cost is just the cached int compare. Complements 0162-v2 Leg A (stage-load freads) by covering the steady-state gameplay path where heavy render frames defer the cooperative-scheduler audio thread past the ring depth (wave-38 cooperative-scheduler starvation pattern). Wave-36 trap clear: pump invokes SDL_DOSAudioPump only -- no game.tick_state, no drawcall submit, no catch-up re-entry. On a default boot the DISABLED variant is expected (lever off); the PLAY1 cell of the audio-pipeline iter SETs the hint to 1 to engage. See docs/internal/AUDIO-PIPELINE-CAMPAIGN-DESIGN.md sec.1.2)"
  "0165 Pixtone multi-source probe (486-campaign Bug 2 continuous-SFX-stutter probe; optional -- LOG_INFO emitted on the first pixtone_probe_active() call (the first audit-flush boundary at flip 100), narrating ENABLED (opt-in via SDL_HINT_DOSKUTSU_PIXTONE_PROBE=1) vs DISABLED (default; the lever is off until the operator engages it for the probe iter); INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1, absent on a plain untagged WARN-level run which is expected not a failure; the runtime witness that the probe killswitch parser engaged and the [pixtone-multi] block emit is wired. On the ENABLED path, one [pixtone-multi block=N samples=N active min=X med=X p90=X max=X live=N stoptrack=N polarstar=N silent_irq=N] line per 100-flip block from _maybe_flush_pixtone_multi_audit -- the active-count histogram (6 buckets [0]/[1]/[2]/[3-5]/[6-10]/[11+]; sampled at audio-buffer-fill ~43 Hz Tier-2 via NXE::Diag::SamplePixtoneActiveBucket called from organyaStreamCallback) + block-over-block deltas for g_pixtone_stoptrack_count (MIX_StopTrack-on-preempt-and-replay events, channel-pressure indicator) + g_pixtone_polar_star_count (combined SND_POLAR_STAR_L1_2 + L3 fires, SFX-storm-rate indicator) + doskutsu_audio_silent_irq_count (SDL/0069 monotonic; scheduler-starvation indicator). Discriminates 5 hypotheses A-E (per-callback mix CPU / track-state transition / scheduler starvation / IRQ jitter / re-diagnose) for the contingent v2 fix author. Counter increments are unconditional; emit + histogram bucket-inc gated on probe_active() -- cache-OFF byte-identical-at-rendered-frame to pre-0165. On a default boot the DISABLED variant is expected (lever off); the PLAY1 cell of the probe iter SETs the hint to 1 to engage. See docs/internal/PIXTONE-MULTISOURCE-DESIGN.md)"
  "0167 Lever 3 engine-side routing (Pixtone IRQ-mix; engine companion to patches/SDL/0071; optional -- LOG_INFO banner emitted on the first pixtone_irq_mix_active() call (the first Pixtone::play / Pixtone::stop / setVolume hit after engine init), narrating ENABLED (DEFAULT-ON since v1.0.1, patches SDL/0075 + nxengine-evo/0170; set SDL_HINT_DOSKUTSU_PIXTONE_IRQ_MIX=0 to disable) vs DISABLED (killswitch =0, or backend-gate refused under Organya). INFO-level; appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1; absent on a plain untagged WARN-level run which is expected not a failure. The runtime witness that the engine-side routing branches engaged. On the ENABLED path, Pixtone::play routes non-resampled SFX through SDL_DOSPixtoneStart (SDL/0071) instead of MIX_PlayTrack, bypassing the cooperative-scheduler audio thread for the SFX path (hypothesis-C mitigation per #120 sec.22). Pixtone::stop routes to SDL_DOSPixtoneStop. Pixtone::playResampled stays on SDL3_mixer (v1 scope; v2 candidate). Pixtone::setVolume mirrors gain into _sound_fx[].gain_x256 for SDL_DOSPixtoneStart consumption; mid-play L3 volume updates are no-op (v2 candidate: SDL_DOSPixtoneSetGain), with a one-shot caveat banner at first L3-active setVolume so operator sees the named cause if a scripted fadeout sounds wrong. Per-slot raw_master_buf is DPMI-locked at Pixtone::_prepareToPlay so the SB16 IRQ-5 ISR mix loop can safely read across IRQ context. Probe-compat: [pixtone-multi] live field + SamplePixtoneActiveBucket histogram both SUM g_pixtone_active_count + g_dos_pixtone_active_count so discriminator pillar 2 (max_active_count) holds across L3 OFF/ON paths. Pixtone::stop disambiguates the routing via on_irq_pool flag set at play-time (one bit per stPXSound; 0 = SDL3_mixer path; 1 = L3 ISR-pool path). On a default boot the ENABLED variant is expected (default-ON since v1.0.1); the killswitch SDL_HINT_DOSKUTSU_PIXTONE_IRQ_MIX=0 disables, and a non-default Organya AUDIO_BACKEND forces the DISABLED-refused variant. See docs/internal/LEVER3-PIXTONE-IRQ-MIX-DESIGN.md sec.4 (engine routing) + sec.10 contract lock)"
  "0168 Lever 2b engine-side routing + DPMI locks + pause-coherent stop/start (MidiScheduler tick from IRQ-5 ISR; engine companion to patches/SDL/0072; optional -- LOG_INFO banner emitted once at SoundManager::init narrating ENABLED (engine registered tick_isr callback (L2b active)) vs DISABLED (engine skipped registration; SDL_HINT_DOSKUTSU_MIDI_ISR_TICK unset or =0). INFO-level; appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1. The runtime witness that the engine-side ISR-callback registration engaged. On the ENABLED path, MidiScheduler::tick_isr is registered via SDL_DOSMidiTickRegister + invoked from the SB16 IRQ-5 ISR at ~43 Hz Tier-2 with monotonic now_ms; the engine main-loop MidiScheduler::tick call at SoundManager::runFade is SKIPPED to avoid double-dispatch. tick_isr uses fixed-point _us_per_tick_x256 (no FPU per ISR discipline) instead of the engine-thread tick()'s double _us_per_tick. Reads from a DPMI-locked 16K-entry MidiEvent[] buffer (g_midi_isr_events) refreshed from _events at every load/load_from_preload_cache mutation under cli/sti (MidiIsrCriticalSection RAII; gated on SDL_DOSMidiIsrTickActive for L2b-OFF byte-identity). MidiBackendOpl3 instance DPMI-locked at ctor; runtime patch bank (.bss-resident) DPMI-locked at _try_load_opl3_patches_dat success. Pause-behavior choice (b) per operator: SoundManager::pause explicitly calls MidiScheduler::stop under L2b ON + SoundManager::resume re-arms via start -- preserves today's 'music halts when paused' semantic; one-shot banner narrates the explicit-stop engagement on first pause transition. On a default boot the DISABLED variant is expected (lever off); engaging requires SDL_HINT_DOSKUTSU_MIDI_ISR_TICK=1 + a non-Organya backend (Organya doesn't use MidiScheduler). See docs/internal/LEVER2B-MIDI-ISR-TICK-DESIGN.md sec.1-7 + sec.8 contract lock)"
  "SDL/0073 Bug 6 Tier-2 rate-diag (patch SDL/0073; always-on -- emitted at audio OpenDevice on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT engine DEBUG.LOG); unconditional, no killswitch; mirrors the resolved SB16 device rate (req_freq/req_ch vs dev_freq/dev_ch, sample_frames, buffer_size, dsp0x41_hz, block_size, isr_chunk) into the iter log -- the SDL_Log at sb.c:~1916 only reaches stdout, invisible in iter logs. The decisive Bug-6 device-rate diagnostic; the companion rate-meas line (irq_hz x sample_frames = hw_frame_rate, emitted every 100 PlayDevice calls) confirms whether the true SB16 frame-drain rate is ~2x the resolved dev_freq -- the leading Tier-2 octave-high root cause. See docs/internal/BUG6-RATE-CONFIRMED-INSTR-AND-FIX-DESIGN.md)"
  "SDL/0074 Bug 6 RATEDIV auto-default (patch SDL/0074, promoted to REQUIRED-on-default-boot by SDL/0075 -- the regex pins master_rate=11025 (engine-computed SAMPLE_RATE*AUDIO_CHANNELS, set before the device opens, device-rate-INDEPENDENT so DOSBox-X-safe per [[dosbox_not_proxy]], unlike shift_div which tracks DOSBox-X's emulated SB16 rate); doubles as a cross-unit witness that nxengine-evo/0169 SDL_DOSPixtoneSetMasterRate ran -- master_rate=0 would mean the setter is missing/mis-ordered. Smoke must run Tier-2 default (no AUDIO_TIER2 override) so master_rate=11025. Emitted at audio OpenDevice when Lever 3 is engaged, after the rate-diag line; supersedes the 0073 operator-supplied rate-divider banner. The engine supplies the Pixtone master content rate via SDL_DOSPixtoneSetMasterRate (SAMPLE_RATE*AUDIO_CHANNELS) before the device opens; SDL computes divider = round(device->spec.freq / master_rate) -> shift_div (measured dev_freq=44100: Tier-2 master 11025 -> divider 4 -> shift 2; Tier-1 master 44100 -> divider 1 -> shift 0), baking the operator-validated RATEDIV=4 as a tier-aware default. SDL_HINT_DOSKUTSU_PIXTONE_IRQ_RATEDIV=1|2|4 is now a DEBUG OVERRIDE that wins when set. Default smoke leaves Lever 3 off so this banner is ABSENT, which is expected not a failure; the ship/confirm-iter L3 cells emit it. See docs/internal/BUG6-BALANCE-AND-SHIP-FIX-DESIGN.md)"
  "SDL/0074 SB16 CT1745 mixer balance (patch SDL/0074, promoted to REQUIRED-on-default-boot by SDL/0075 -- the regex pins master=31 voice=31 fm=28, the v1.0.1 compiled-default balance (env-independent + device-independent, so DOSBox-X-safe; catches a fat-fingered FM default; is_sb16 holds under DOSBox-X sbtype=sb16). Emitted at audio OpenDevice on the SB16 (is_sb16) path; programs the CT1745 analog mixer: Master 0x30/0x31=max(31), Voice(PCM/SFX) 0x32/0x33, FM(music) 0x34/0x35, to fix the Bug-6 SFX-too-quiet-vs-OPL3-music balance (the SFX are near digital full-scale so the boost must be ANALOG, not digital). Two variants: 'master=31 voice=N fm=N' (enabled; defaults voice=31 fm=24; tune SDL_HINT_DOSKUTSU_SB16_VOICE_VOL / SDL_HINT_DOSKUTSU_SB16_FM_VOL 0..31) vs DISABLED (SDL_HINT_DOSKUTSU_SB16_MIXER_PROGRAM=0 -> mixer untouched, byte-identical to pre-0074). Default = program (the fix is ON). DOSBox-X may not model CT1745 balance so smoke confirms boot/no-crash only; the audible effect is real-HW-only (the SB Pro 0x0E write already proves the mixer-port mechanics, low risk). See docs/internal/BUG6-BALANCE-AND-SHIP-FIX-DESIGN.md)"
  "SDL/0071 L3 default-ON ASSERT (patch SDL/0075; REQUIRED on default boot -- the v1.0.1 ship config has Lever 3 default-ON, so a plain ZERO-env boot MUST emit the SDL/0071 ENABLED banner at audio OpenDevice. Asserts the default-flip took, complementing the optional alternation entry above which just logs whichever state emitted. FAILS the gate if a default boot shows DISABLED (flip regressed / half-flip) or the audio device never opened. Killswitch SDL_HINT_DOSKUTSU_PIXTONE_IRQ_MIX=0 emits DISABLED -- that is a deliberate non-default run, not the gated default config. See docs/internal/V1_0_1-DEFAULT-FLIP-AND-FINAL-ITER-DESIGN.md)"
  "SDL/0072 L2b default-ON ASSERT (patch SDL/0075; REQUIRED on default boot -- v1.0.1 has Lever 2b default-ON, so a ZERO-env boot MUST emit the SDL/0072 ENABLED banner at audio OpenDevice. L2b is entirely SDL-driven (the engine follows SDL_DOSMidiIsrTickActive at all sites), so this single SDL-side flip is the whole L2b default-ON. FAILS the gate if a default boot shows DISABLED. Killswitch SDL_HINT_DOSKUTSU_MIDI_ISR_TICK=0 emits DISABLED -- deliberate non-default. See docs/internal/V1_0_1-DEFAULT-FLIP-AND-FINAL-ITER-DESIGN.md)"
  "SDL/0074 RATEDIV-auto presence (informational disambiguator, OPTIONAL; complements the pinned-required master_rate=11025 entry above -- if that required entry FAILS, this optional one logs whether the RATEDIV-auto banner emitted AT ALL: emits=1 means present-but-wrong-value (dropped nxengine-evo/0169 setter or non-Tier-2), emits=0 means absent (L3 off / audio device did not open). No gate effect. patch SDL/0075)"
  "SDL/0074 SB16-balance presence (informational disambiguator, OPTIONAL; complements the pinned-required master=31 voice=31 fm=28 entry above -- if that required entry FAILS, this logs whether the SB16-balance banner emitted AT ALL: present-but-wrong-value vs absent (not is_sb16 / MIXER_PROGRAM=0). No gate effect. patch SDL/0075)"
  "engine L2b tick_isr registration ASSERT (REQUIRED on default boot via SDL/0075 + nxengine-evo/0170 -- the engine 0168 banner emits at SoundManager::init when SDL_DOSMidiIsrTickActive() is true (default-ON since v1.0.1). Init-time DETERMINISTIC (NOT SFX-gated), and the smoke is verbose (DOSKUTSU_LOG_VERBOSE=1 -> INFO) so the engine LOG_INFO line emits. Distinct from L302 (which asserts SDL RESOLVED L2b ON): this asserts the ENGINE actually REGISTERED the tick_isr callback (followed the SDL flag) -- a regression where SDL says on but the engine registration path breaks would PASS L302 and FAIL this. The matching skipped-registration variant only emits under the =0 killswitch (deliberate non-default). Per flush-instr. patch SDL/0075)"
  "nxengine-evo/0171 OPL3 wub-wub fix banner (REQUIRED in builds carrying patch 0171 -- the 4-byte PATCH_ORGAN/PATCH_MALLET RR=0->RR=8 fix emits this LOG_INFO at MidiBackendOpl3 init confirming the fixed-bytes constexpr table linked. Default-ON, no killswitch; banner is unconditional in the new binary. Absence on a post-0171 build = build linked pre-fix bytes (regression / stale cache). Pairs with the SDL/0079 shutdown-sweep banner for the wub-wub residual fix bundle.)"
  "SDL/0079 OPL3 shutdown TL=0x3F sweep banner (REQUIRED on clean-quit in builds carrying SDL/0079 -- the SDL_DOSOpl3Shutdown TL=0x3F sweep zeroes all 18 voices x 2 ops before KEY-OFF-all, killing the post-quit envelope output. Default-ON, no killswitch; emits when engine destructor runs at clean quit (smoke's TAS-EOF auto-exit path triggers it). Absence = SDL/0079 not linked OR smoke didn't reach clean quit. Pairs with the nx-0171 engine-init banner.)"
  "SDL/0080 mpu401 direct-port default banner (optional -- emits at SDL_DOSMpu401Init when the WB MIDI dispatch path engages, which is gated on either the auto-detect chain selecting WB or AUDIO_BACKEND=wb explicitly. Default smoke runs without AUDIO_BACKEND set and the auto-detect chain falls through WB -> OPL3 on DOSBox-X (no DreamBlaster S2 daughterboard emulated) so this banner is typically ABSENT under DOSBox-X. The runtime-witness side of the two-witness pattern for the patch-0080 default-flip: the strings|grep on the binary proves embed (\"mpu401: direct-port init at port_base=\"), and this banner regex proves runtime invocation if/when WB is selected. Real-HW iters with AUDIO_BACKEND=wb on g2k Vibra16S + S2 are the path where the banner actually emits and where the WBTEST-006 H20 confirmation lands. Killswitch path (SDL_HINT_DOSKUTSU_AUDIO_WB_DIRECT_PORT=0) emits the \"DSP-mediated fallback init at port_base=\" banner instead -- not covered by this regex by design, since the default-config gate cares about the default-path banner. See patches/SDL/0080 commit message + docs/internal/WBTEST-001-FINDINGS.md rev-8.)"
  "0178 SFXPAUSE-007 load-stage trace decision banner (optional -- LOG_INFO emitted on the first loadstage_trace_active() call when the probe gate is set, narrating ENABLED variant only; DISABLED state has no emit. INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1. The runtime witness that the patch 0178 probe is wired and the cached gate parser engaged. Default smoke leaves SDL_HINT_DOSKUTSU_LOADSTAGE_TRACE unset so this banner is ABSENT, which is expected not a failure; SFXPAUSE-007 iter cells SET it to 1 to engage. Pairs with the per-emit [load-stage-phase] regex below. This probe is the diagnostic for the CORRECTED Bug 1 mechanism per SFXPAUSE-006 (audio-thread catch-up hypothesis invalidated; main-thread load_stage cost on POD83 + CF card is the remaining suspect).)"
  "0178 SFXPAUSE-007 load-stage per-phase emit (optional -- one [load-stage-phase] line per sub-loader inside load_stage (8 lines: sheets_flush, tileset_load, load_map, load_tileattr, load_entities, tsc_load, backdrop_set, load_meta) + one TOTAL summary line at success exit. Default smoke leaves SDL_HINT_DOSKUTSU_LOADSTAGE_TRACE unset so this is ABSENT, which is expected. SFXPAUSE-007 iter cells will accumulate 9 lines per cave entry; the dominant sub-phase wall_ms tells flush-instr which load_stage path to drill into next. Error-path returns emit per-phase lines up to the failure point but NO TOTAL -- parser detects error path by absence-of-TOTAL for a given cave=N. See docs/internal/BOOT.md for the full field semantics + patches/nxengine-evo/0178 commit message.)"
  "0182 SFXPAUSE-010 frame-spike detect decision banner (optional -- LOG_INFO emitted on the first frame_spike_detect_active() call when SDL_HINT_DOSKUTSU_FRAME_SPIKE_DETECT=1; DISABLED state has no emit. Default smoke leaves the hint unset so this banner is ABSENT, which is expected not a failure. SFXPAUSE-010 iter cells SET it to 1 to capture session-wide inter-frame > 50 ms events. Pairs with [frame-spike] regex below.)"
  "0182 SFXPAUSE-010 frame-spike per-event emit (optional -- one [frame-spike] inter_ms=M line per frame where the inter-frame wall-clock delta exceeds 50 ms; expected ~10-20 lines per session on a default smoke run that exercises cave entries. Default smoke leaves SDL_HINT_DOSKUTSU_FRAME_SPIKE_DETECT unset so this is ABSENT, which is expected. SFXPAUSE-010 iter accumulates the spike events to identify when in the session the user-perceptible stall occurs (Mechanism #2 fire-frame stall per [[sfx_pause_mechanism_identified_audiocb_catchup_burst]]). See docs/internal/BOOT.md + patches/nxengine-evo/0182 commit message.)"
  "0182 SFXPAUSE-010 firepath trace decision banner (optional -- LOG_INFO emitted on the first firepath_trace_active() call from any of the 4 bracketed functions when SDL_HINT_DOSKUTSU_FIREPATH_TRACE=1; DISABLED state has no emit. Default smoke leaves the hint unset so ABSENT, expected. SFXPAUSE-010 iter cells SET it to 1. Pairs with [firepath] regex below.)"
  "0182 SFXPAUSE-010 firepath per-function first-call emit (optional -- one [firepath] func=NAME wall_us=N line per bracketed function on its FIRST CALL per session. Four functions bracketed: FireWeapon, CreateBullet, effect, ai_polar_shot. Default smoke leaves the hint unset so this is ABSENT, expected. SFXPAUSE-010 iter accumulates 4 lines per session showing the cold-ICache + cold-heap cost of each function's first invocation; whichever is dominant is the candidate target for SFXPAUSE-011's fix attempt. See docs/internal/BOOT.md + patches/nxengine-evo/0182 commit message.)"
  "0185 SFXPAUSE-012 sheet-load trace decision banner (optional -- LOG_INFO emitted on the first sheetload_trace_active() call when SDL_HINT_DOSKUTSU_SHEETLOAD_TRACE=1; DISABLED state has no emit. Default smoke leaves the hint unset so ABSENT, expected not a failure. SFXPAUSE-012 DOSBox-X structural-confirm run + g2k baseline cell SET it to 1. Pairs with [sheet-load] regex below.)"
  "0185 SFXPAUSE-012 sheet-load per-decode emit (optional -- one [sheet-load] cave=C sheetno=N wall_us=N line per actual PNG decode in Sprites::_loadSheetIfNeeded when SDL_HINT_DOSKUTSU_SHEETLOAD_TRACE=1. The root-cause smoking gun: a sheet load whose cave=C matches a cave whose load_stage completed many frames earlier proves the decode happened at fire-time (the ~450 ms fire-frame pause). Default smoke leaves the hint unset so ABSENT, expected. build-qa's DOSBox-X structural-confirm run SETs the hint + correlates sheet-load timeline to fire-frame vs load-frame -- the mechanism is pure CPU/filesystem so DOSBox-X runs it faithfully. See docs/internal/BOOT.md + patches/nxengine-evo/0185 commit message.)"
  "0186 SFXPAUSE-012 eager-sheet-reload decision banner (optional -- LOG_INFO emitted on the first eager_sheet_reload_active() call when SDL_HINT_DOSKUTSU_EAGER_SHEET_RELOAD=1; DISABLED state has no emit. Default smoke leaves the hint unset so ABSENT, expected; the fix ships default-OFF for the A/B test. SFXPAUSE-012 fix cell SETs it to 1. Pairs with [eager-sheet-reload] regex below.)"
  "0186 SFXPAUSE-012 eager-sheet-reload per-cave emit (optional -- one [eager-sheet-reload] cave=C sheets=K wall_ms=M line per cave entry when SDL_HINT_DOSKUTSU_EAGER_SHEET_RELOAD=1. sheets=K is the count of sheets that needed a fresh decode this cave; wall_ms is the eager-decode cost relocated into the load band. With this ON, the companion [sheet-load] emits cluster at load_stage time (the A/B proof that the fix moved the decode out of the fire frame). Default smoke leaves the hint unset so ABSENT, expected. build-qa confirms structurally in DOSBox-X. See docs/internal/BOOT.md + patches/nxengine-evo/0186 commit message.)"
  "0187 SFXPAUSE-013 IO-audit decision banner (optional -- LOG_INFO emitted on the first io_audit_active_main() call when SDL_HINT_DOSKUTSU_IO_AUDIT=1; DISABLED state has no emit. Default smoke leaves the hint unset so ABSENT, expected. SFXPAUSE-013 build-qa run (task #38) SETs it to 1 to produce the lazy-IO landmine list. Pairs with the [io-audit] regex below.)"
  "0187 SFXPAUSE-013 IO-audit per-op emit (optional -- one [io-audit] phase=NAME op=OPTYPE file=BASENAME bytes=N wall_us=N line per instrumented filesystem-IO op when SDL_HINT_DOSKUTSU_IO_AUDIT=1. Phases: boot / load_stage / gameplay. Op types: loadImage / tsc_load / music_org_load / music_ogg_load / save_read / save_write. The actionable subset is phase=gameplay (a latent real-HW lazy-IO stall); phase=boot + phase=load_stage are expected. Validates the 0186 fix (sprite loadImage ops should appear at phase=load_stage, not phase=gameplay) AND surfaces any other phase=gameplay IO landmines. Default smoke leaves the hint unset so this is ABSENT, expected. build-qa task #38 runs the audit + produces the landmine list + authors the permanent no-gameplay-IO dev gate. Mechanism is pure CPU/filesystem so DOSBox-X runs it faithfully. See docs/internal/BOOT.md + patches/nxengine-evo/0187 commit message.)"
  "0188 SFXPAUSE-012 skip-sheet-flush decision banner (optional -- the SHIPPED FIX, DEFAULT-ON. LOG_INFO emitted on the first skip_sheet_flush_active() call (first cave entry), narrating ENABLED (default) vs DISABLED (killswitch =0). INFO-level so it appears only on tagged runs or DOSKUTSU_LOG_VERBOSE=1; absent on a plain untagged WARN-level production boot which is expected not a failure. On a default boot the ENABLED (default) variant confirms the per-cave sprite-sheet flush is skipped (the SFXPAUSE-012 fire-frame-pause fix). SDL_HINT_DOSKUTSU_SKIP_SHEET_FLUSH=0 emits DISABLED (killswitch) + restores the old per-cave flush. Pairs with the [skip-sheet-flush] regex below.)"
  "0188 SFXPAUSE-012 skip-sheet-flush per-cave emit (optional -- one [skip-sheet-flush] cave=C line per cave entry on the default-ON path, confirming load_stage skipped the sprite-sheet flush so sheets stay resident. cave 1 lazy-loads sheets one-time, cave 2+ finds them resident (no re-decode, no pause, no added load) -- the SHIPPED fix. INFO-level so absent on an untagged WARN-level boot (expected). The companion [io-audit] op=loadImage lines (under SDL_HINT_DOSKUTSU_IO_AUDIT=1) should appear ONLY in cave 1, never recurring cave 2+ -- the gate-validation signature build-qa's IO-audit dev gate keys off. See docs/internal/BOOT.md + patches/nxengine-evo/0188 commit message.)"
  "0185 P2 FIX-2 entry-music preload (PRELOAD_STAGE_MUSIC default-ON; optional -- BANNER_REGEX idx 76)"
  "0184 P2 FIX-1 eager-title-IO phase tag (EAGER_TITLE_IO default-ON; optional -- BANNER_REGEX idx 77)"
  "0186 P1b BLITPATTERN_CLAMP src-clamp decision banner (default-ON v1.0.6; optional -- LOG_INFO via _blitpattern_src_clamp() narrating ENABLED (default) vs DISABLED (killswitch). INFO-level so present only on tagged/verbose runs, absent on a plain untagged WARN boot which is expected not a failure. ENABLED (default) confirms the layered-pattern-blit source-height clamp is active -- the v1.0.5 cache-OFF title cloud OOB-read fix, flipped default-ON for v1.0.6 since the OFF state was production-byte-identical and PLAY1 confirmed it clean on g2k. SDL_HINT_DOSKUTSU_BLITPATTERN_CLAMP=0 emits DISABLED (killswitch) + restores the unclamped read. See docs/internal/BOOT.md + patches/nxengine-evo/0186 commit message. BANNER_REGEX idx 78.)"
  "0188 #31 backdrop-before-music reorder banner (env-gated SDL_HINT_DOSKUTSU_BACKDROP_BEFORE_MUSIC, DEFAULT-OFF; optional -- LOG_INFO at first title_init narrating 'disabled (default)' vs 'ENABLED'. Rides the #37 DX2-66 iter (PLAY0=disabled/repro, PLAY1==1/fix) + a v1.0.7 ship candidate if g2k confirms #31 Bug-5. OPTIONAL not required: the permanent gate must also pass against shipped v1.0.6 + cf80c15-swept release binaries which LACK 0188 -- a required entry would regression-trap them. Promote to required at the v1.0.7 ship-gate when 0188 goes default-ON (the BLITPATTERN_CLAMP precedent). BANNER_REGEX idx 79.)"
  "0187 #32 exit-bracket diagnostic chain (DEFAULT-ON shutdown diag, no env; optional -- doskutsu_dbg_log markers at clean main()-return: exit-pre-return -> exit-atexit-first -> exit-atexit-last (+ raw co-witness exit-atexit-last-raw, fsync'd). Emits ONLY on a clean quit (boot-only/force-killed smoke won't show them, expected). A log that ends before these on quit = the #32(b) post-SDL_Quit DOS-exit hang caught. OPTIONAL: a pure diagnostic, never ships in a release; absent on release binaries which lack 0187. BANNER_REGEX idx 80.)"
  "0190 #38 DEV-WARP dev/QA warp tool (CONDITIONAL on DOSKUTSU_WARP_STAGE>0; optional -- emits 'DEV WARP: new game -> stage N spawn (x,y) ...' at InitNewGame when the warp is requested. Default smoke leaves WARP_STAGE unset so this is ABSENT (the warp is a runtime no-op, behavior-identical to production) -- expected, not a failure. The 0190 warp-smoke cell (DOSKUTSU_WARP_STAGE=13) SETs it + is where this is witnessed (the 2-witness runtime half for 0190; strings-embed is the other). OPTIONAL: dev-only tool, relocates to _disabled/ before ship; WARP_STAGE-absent in ship binaries stays the absent-gate. Supersedes the crude 0189 (curated D1 spawn + map_focus camera + forced FADE_IN, vs 0189's drowned 10,8). BANNER_REGEX idx 81.)"
  "0192 #31 Bug-5 STARVE_DIAG eng-side decision banner (env-gated SDL_HINT_DOSKUTSU_STARVE_DIAG, DEFAULT-OFF; optional -- LOG_INFO 'starve-diag: ENABLED (opt-in)' vs 'DISABLED (default)' at the engine instrumentation init. Gates the eng: title run_tick load-bracket breadcrumbs. Default smoke leaves the hint unset so the DISABLED variant emits on a verbose/tagged run (ABSENT on a plain untagged WARN boot, expected). OPTIONAL: diag-only lever for the DX2-66 Bug-5 localize iter, never ships in a release; absent on release binaries which lack 0192. BANNER_REGEX idx 82.)"
  "0192 #31 Bug-5 TITLE_PREDECODE fix-lever decision banner (env-gated SDL_HINT_DOSKUTSU_TITLE_PREDECODE, DEFAULT-OFF; optional -- LOG_INFO 'title backdrop pre-decode: ENABLED (opt-in)' vs 'DISABLED (default)'. Cell C candidate fix: pre-decode title backdrops at boot. Default smoke leaves the hint unset so DISABLED emits (verbose/tagged only); the PLAY1 cell SETs it to 1 -> ENABLED. OPTIONAL: diag/fix lever for the Bug-5 iter, absent on release binaries which lack 0192. BANNER_REGEX idx 83.)"
  "0192 #31 Bug-5 TITLE_AUDIO_PUMP fix-lever decision banner (env-gated SDL_HINT_DOSKUTSU_TITLE_AUDIO_PUMP, DEFAULT-OFF; optional -- LOG_INFO 'title audio pump (run_tick): ENABLED (opt-in)' vs 'DISABLED (default)'. Cell B candidate fix: drive SDL_DOSAudioPump from the title run_tick (which otherwise has no audio pump -- the Bug-5 root cause). Default smoke leaves the hint unset so DISABLED emits (verbose/tagged only); the PLAY2 cell SETs it to 1 -> ENABLED. OPTIONAL: diag/fix lever for the Bug-5 iter, absent on release binaries which lack 0192. BANNER_REGEX idx 84.)"
  "SDL/0082 #31 Bug-5 snd-side starve-diag engage banner (env-gated SDL_HINT_DOSKUTSU_STARVE_DIAG -- same hint as eng idx 82, the SDL-audio-side witness; optional -- 'audio: SDL/0082 STARVE_DIAG=1 ... ENABLED (snd: layer)' fires on the first PlayDevice call (entry-placement, before the SDL/0054 silence-skip early-return) and gates the per-100-call + LOW-edge 'starve-diag/snd:' ring-state markers. Default smoke leaves the hint unset so this is ABSENT (expected). In DOSBox the PlayDevice ring path IS exercised on the organya title so the markers witness (64-marker confirm, build ce2164c0); on g2k the real SB16 PlayDevice fires them. OPTIONAL: diag-only, absent on release binaries which lack SDL/0082. BANNER_REGEX idx 85.)"
  "SDL/0086 #31 Bug-5 in-band COOP_YIELD fix banner (the confirmed-root-cause fix; default-ON, killswitch SDL_HINT_DOSKUTSU_DOS_AUDIO_COOP_YIELD=0; optional -- 'audio: SDL/0086 in-band COOP_YIELD ENABLED (default-ON) ...' emits unconditionally at SB16 OpenDevice on the SDL-log channel (<TAG>SDL.LOG / SDLDBG.LOG, NOT engine DEBUG.LOG). OPTIONAL like the sibling 0069 OpenDevice banner: present only when the SB16 audio device opens + the SDL-log channel is active (typically present under DOSBox-X with SB16 emulated; ABSENT is not a failure -- the embed side of the two-witness pattern is strings|grep 'in-band COOP_YIELD' on the binary). Killswitch=0 emits the DISABLED variant instead (not matched by this regex by design). Unlike SDL/0082 (diag-only, env-gated) this lever SHIPS in v1.0.7 default-ON; promote this entry to required once g2k+DOSBox confirm reliable emit at ship-prep. BANNER_REGEX idx 86.)"
  "SDL/0090 #31 v1.0.8 device-rate setter default banner (always-on in DEFAULT config when the setter applies: SoundManager calls SDL_DOSAudioSetDeviceFrameRate at SB16 OpenDevice -> 'audio: SDL/0090 device-rate setter default: 44100 -> 11025 Hz'. OPTIONAL by the SDL/0086 precedent -- emits on the SDL-log channel only when the SB16 device opens + the SDL-log channel is active (typically present under DOSBox-X w/ SB16 emulated; ABSENT is not a failure -- embed witness = nm/strings SDL_DOSAudioSetDeviceFrameRate). Suppressed by killswitch SDL_HINT_DOSKUTSU_DOS_AUDIO_DEVICE_RATE_DEFAULT=0 or any DEVICE_RATE env hint (the SDL/0087 hint line emits instead) -- so gate it in DEFAULT-config smoke only. Promote to required at ship-prep once DOSBox+g2k confirm reliable emit. BANNER_REGEX idx 87.)"
  "0196 #31 Bug-5 bug5pmp ORG_STREAM_DIAG organya stream-production probe (env-gated SDL_HINT_DOSKUTSU_ORG_STREAM_DIAG, DEFAULT-OFF; optional -- one-shot 'org-stream-diag: ORG_STREAM_DIAG=1 ... ENABLED' on the FIRST organyaStreamCallback when the hint=1 AND the Organya backend is active, then a per-100-callback [org-stream-stat] line. Default smoke runs the OPL3 backend (wave-46 default) with the hint unset, so the organya callback never runs and this is ABSENT -- expected, not a failure (embed witness = strings|grep org-stream-stat). The realhw PMP0/1/2 cells (organya + ORG_STREAM_DIAG=1) are the runtime witness. OPTIONAL: diag-only probe for the Bug-5 gameplay-audio residual iter, absent on release binaries which lack 0196. BANNER_REGEX idx 88.)"
  "0197 #31 Bug-5 bug5pmp ORG_LOOKAHEAD organya stream look-ahead buffer (env-gated SDL_HINT_DOSKUTSU_ORG_LOOKAHEAD, DEFAULT-OFF; optional -- one-shot 'org-lookahead: ORG_LOOKAHEAD=1 ... ENABLED (target=4096 bytes cap=16 iters)' on the FIRST organyaStreamCallback when the hint=1 AND the Organya backend is active. The candidate-fix lever for the gameplay residual: the callback over-produces to a target stream-queue depth so a flip-park drains the buffer instead of starving. Default smoke runs OPL3 + hint-unset so the callback never runs and this is ABSENT -- expected, not a failure (embed witness = strings|grep org-lookahead). The realhw PMP2 cell (organya + ORG_LOOKAHEAD=1 + ORG_STREAM_DIAG=1) is the runtime witness. OPTIONAL: candidate-fix lever for the Bug-5 gameplay-audio iter, default-OFF; promote to required only if it ships default-ON in v1.0.7. BANNER_REGEX idx 89.)"
  "0198 #31 Bug-5 bug5pmp TICKPUMP_CAP bound-the-pump lever (env-gated SDL_HINT_DOSKUTSU_TICKPUMP_CAP=N, DEFAULT 0/unbounded; optional -- one-shot 'audio tick-boundary pump CAP: N (...)' on the first capped TICKPUMP, which fires ONLY when SDL_HINT_DOSKUTSU_AUDIO_TICKPUMP=1 (the cap reader sits inside the tickpump_active() gate). Default smoke leaves AUDIO_TICKPUMP unset so the pump never runs and this is ABSENT -- expected, not a failure (embed witness = strings|grep 'tick-boundary pump CAP'). The realhw CAP=1/CAP=2 cells (TICKPUMP=1 + TICKPUMP_CAP=N) are the runtime witness. Bounds the unbounded SDL_DOSAudioPump while-loop to N frames/tick so the proven pump feeds organya without stealing main. OPTIONAL: candidate-fix lever for the Bug-5 gameplay-audio iter, default-unbounded preserves pre-0198 behavior. BANNER_REGEX idx 90.)"
  "0203 #31 v1.0.8 device-rate ENGINE wire-in banner (always-on every boot: SoundManager unconditionally calls SDL_DOSAudioSetDeviceFrameRate(SAMPLE_RATE) -> 'Sound system: device frame-rate: requesting <hz> Hz match to master ...'. OPTIONAL -- banner-emit proves the engine CALL fired, NOT that the rate took effect (the SDL-side killswitch DEVICE_RATE_DEFAULT=0 makes the backend ignore the call while the engine still emits this banner); rate-took-effect witness = SDL/0090 banner + SDL/0073 dev_freq line. Redundant device-rate witness alongside SDL/0090 (optional per nx handoff; embed witness = strings _SDL_DOSAudioSetDeviceFrameRate). BANNER_REGEX idx 91.)"
  "0213 v1.0.8.1 eager action-sheet load decision (default-ON; LOG_INFO at first load_stage; pre-loads weapon bullet + hit/muzzle/trail caret sheets so the first fire/impact does not lazy-decode mid-gameplay; killswitch SDL_HINT_DOSKUTSU_EAGER_ACTION_SHEETS=0. OPTIONAL: INFO-level, present on verbose/tagged runs. BANNER_REGEX idx 92.)"
  "0214 v1.0.8.1 organya render-window quiesce decision (default-ON; emits on an organya COLD-RENDER; zeroes the SB16 ring so the DMA loops silence not stale music during the blocking synth pass; killswitch SDL_HINT_DOSKUTSU_ORG_RENDER_QUIESCE=0. OPTIONAL: cold-render-only, absent in a warm-cache smoke. BANNER_REGEX idx 93.)"
  "0215 v1.0.8.1 batch org-precache total (OPTIONAL -- emits ONLY under DOSKUTSU_ORG_PRECACHE_ALL=1, the deployable-cache one-shot; reports rate/channels/rendered/skipped/failed/bytes for CF sizing; absent in a normal gameplay smoke. BANNER_REGEX idx 94.)"
  "0216 DOSKUTSU.CFG config-file setenv shim (REQUIRED -- emits every boot after Logger::init: 'config: loaded DOSKUTSU.CFG (N keys)' when DOSKUTSU.CFG is present in the program dir, else 'config: no DOSKUTSU.CFG, using built-in defaults'. SETUP.EXE writes the file; the shim setenv's each user-facing key BEFORE any getenv/SDL_GetHint read, overwrite=0 so precedence is env > file > built-in default. Two-witness with the consuming lever's own banner -- e.g. a CFG with PERF_MODE=1 yields 'perf-mode: level=1'; a BAT 'SET SDL_HINT_DOSKUTSU_PERF_MODE=0' over the same CFG yields 'perf-mode: level=0' (env wins). Default smoke stages NO CFG so the 'no DOSKUTSU.CFG' variant matches. BANNER_REGEX idx 95.)"
  "0221 organya auto-precache decision (OPTIONAL -- organya-gated, emits ONLY on an organya run, one of 'ENABLED (default)' / 'DISABLED (killswitch =0)' via SDL_HINT_DOSKUTSU_ORG_AUTOCACHE; mirrors the 0214 render-quiesce banner. First-launch renders all songs behind a progress overlay + writes the per-tier READY.<sha> sentinel; subsequent boots skip on the sentinel; in-game cold-render overlay armed for stray misses. Absent under the default OPL3/WB backend (no cold-render) which is correct not a failure; the headless DOSKUTSU_ORG_PRECACHE_ALL=1 path returns before registration so it is also absent there -- byte-identical to 0215.)"
  "0223 small-polish item-4c DOS resolution-label lock (default-ON; killswitch SDL_HINT_DOSKUTSU_RES_LABEL_LOCK=0. Emits every DOS boot from Renderer::initVideo: 'Renderer::initVideo: DOS resolution-label lock ENABLED (default)' (or DISABLED (killswitch =0)). The actual behavior -- Video menu shows the locked 320x240 + Resolution scroll is a no-op -- lives in options.cpp and only runs when the menu is opened, so the boot banner is the two-witness runtime side; embed witness = strings|grep 'DOS resolution-label lock'. REQUIRED at the v1.1.2 ship-gate (default-ON, initVideo emits at every boot; g2k-validated). BANNER_REGEX idx 97.)"
  "0224 small-polish item-3 menu slide-in fixed-timestep fix (default-ON; killswitch SDL_HINT_DOSKUTSU_MENU_SLIDE_FT=0. Forced boot eval in main.cpp after textbox.Init emits 'menu slide-in fixed-timestep: ENABLED (default; ...)' (or DISABLED (killswitch; ...)). Moves the StageSelect WARP-banner (fWarpY) + SaveSelect char-pic (fPicXOffset) slide STEP into the sub-prompt TickState() so it advances at 50 Hz under FT GM_NORMAL instead of render rate; non-FT paths (FT=0/GM_TITLE/inventory) keep stepping in Draw byte-identically. Embed witness = strings|grep 'menu slide-in fixed-timestep'. REQUIRED at the v1.1.2 ship-gate (default-ON, boot-forced eval emits at every boot; g2k-validated). BANNER_REGEX idx 98.)"
  "0226 #39b custom MIDI drop-in dir resolution (OPTIONAL -- emits ONLY when MIDI_SET / SDL_HINT_DOSKUTSU_AUDIO_MIDI_SOURCE names a path-safe data/<dir>/ holding >=1 .mid that is NOT a known set (wiimidi/orgmid), AND the killswitch SDL_HINT_DOSKUTSU_AUDIO_MIDI_CUSTOM_DIRS is not '0'. Default smoke uses MIDI_SET=wiimidi so this is ABSENT -- correct, not a failure; embed witness = strings|grep 'custom drop-in dir'. The #39b DOSBox validation cell (MIDI_SET=mycustom + staged data/mycustom/*.mid) is the runtime witness; killswitch=0 falls back to wiimidi with no banner. BANNER_REGEX idx 99.)"
  "0227/0230 Phase-3 input bindings + gameport axis maps (REQUIRED -- '[input-bind] applied=N invert_y=M dos_axes=1' emits every boot from input_apply_cfg_bindings; applied=count of BIND_* overlaid, dos_axes=1 = the DOS axis->action maps re-asserted post settings_load. Embed witness = strings|grep DOSKUTSU_BIND_. BANNER_REGEX idx 100.)"
  "SDL/0102 gameport persisted-calibration applied (OPTIONAL -- SDL-log '[joycal] applied stored cal x=[..] y=[..]'; fires ONLY when SDL_HINT_DOSKUTSU_JOY_CAL is set + an emulated/real gameport is present, so ABSENT in the default no-joystick smoke is correct. Embed witness = strings|grep joycal. BANNER_REGEX idx 101.)"
  "0231 v1.0.9 HQ Organya tier (optional -- emits ONLY when SDL_HINT_DOSKUTSU_AUDIO_TIER2=0 resolves the 22050 stereo HQ tier; default smoke runs Tier-2 LQ so the line is ABSENT, expected not a failure; the HQ smoke cell sets AUDIO_TIER2=0 to witness it. Embed witness = strings|grep org-hq. BANNER_REGEX idx 102.)"
  "SDL/0106 SB DMA-path decision (REQUIRED -- 'audio: SDL/0106 SB DMA-path decision: is_sb16=.. force_8bit=.. dsp_ver=.. highdma=.. -> (16-bit-high-DMA|8-bit-low-DMA) path' emits every boot at SB detection; the PicoGUS-in-SB-mode 8-bit-DMA unblock + DMA-path witness (patches/SDL/0106). Embed witness = strings|grep 'SDL/0106'. BANNER_REGEX idx 103.)"
  "SDL/0106 WB MPU-401 init-result witness (OPTIONAL -- 'mpu401: SDL/0106 WB init result=(OK|FAILED) port_base=.. drr_poll_enabled=.. drr_cap=.. drr_cap_hits=..' emits at SDL_DOSMpu401Init, ONLY when the WB MIDI path engages (AUDIO_BACKEND=wb or auto-detect picks WB). Default DOSBox-X smoke falls through WB -> OPL3 (no DreamBlaster S2 emulated) so typically ABSENT -- expected, not a failure (same gating as the patch-0080 mpu401 direct-port banner). The init-time companion of the sb.c CloseDevice WB/MPU run-end witness; together they bracket the wave-40 REFUTE_MPU_TIMEOUT field (DreamBlaster S2 output-buffer-busy stuck after byte 1) for the PicoGUS case. Embed witness = strings|grep 'WB init result'. BANNER_REGEX idx 104.)"
  "SDL/0107 8-bit MONO + ring silence-prime witness (OPTIONAL -- 'audio: SDL/0107 8-bit channel mode: ch=1 (MONO) tc=.. effective=.. Hz stereo_bit=no ring_silence_primed=yes ...' emits at OpenDevice ONLY on the pre-SB16 8-bit DMA path. DOSBox-X emulates an SB16 (DSP 4.x + high DMA) so is_sb16=true and the 8-bit branch never runs -> ABSENT under the default smoke, expected, not a failure (same g2k-only gating as the wave-49 Cirrus-BLT banner). The g2k PicoGUS-in-SB-mode iter is the runtime witness: ch=1/MONO/no-stereo-bit confirms the SDL/0107 chipmunk-pitch fix; ring_silence_primed=yes confirms the U8 0x80 ring prime (loud-pop fix). Killswitch SDL_HINT_DOSKUTSU_AUDIO_SB_8BIT_STEREO=1 restores prior SB-Pro 8-bit STEREO (would emit ch=2/STEREO/yes). Embed witness = strings|grep 'SDL/0107'. BANNER_REGEX idx 105.)"
  "SDL/0108 8-bit U8 Pixtone IRQ-mix format (OPTIONAL -- 'audio: SDL/0108 pixtone IRQ-mix format: (S16-stereo|U8-stereo|U8-mono) (device is_16bit=.. channels=..)' emits at OpenDevice ONLY when Lever-3 Pixtone IRQ-mix is active (default-ON; absent under AUDIO_BACKEND=organya or PIXTONE_IRQ_MIX=0). DOSBox-X emulates an SB16 so it emits the S16-stereo variant (byte-identical 16-bit path); g2k PicoGUS-in-SB-mode emits U8-mono -- the decisive witness that the SFX-distortion fix engaged (the ISR now writes U8 centered on 0x80 with the correct mono stride instead of S16-stereo into the U8 ring). No new env var (rides the existing PIXTONE_IRQ_MIX killswitch). Embed witness = strings|grep 'SDL/0108'. BANNER_REGEX idx 106.)"
  "SDL/0109 SB16 valid-16bit-DMA-channel guard (OPTIONAL -- 'audio: SDL/0109 DSP X.Y reports SB16 but BLASTER has no VALID 16-bit DMA channel (highdma=N; valid = 5/6/7) -- GRACEFUL fall back to the 8-bit low-DMA D-channel path ...' emits at SB detection ONLY when a DSP-4.x card resolves highdma NOT in {5,6,7} (the g2k 'H0' channel-0 init-hang shape, or a PicoGUS-in-SB-mode bogus/absent H) AND force-8bit is off. Default DOSBox-X smoke has a real H5 -> is_sb16=true so this is ABSENT, expected, not a failure. The dedicated DOSBox cell (sb16 + no-valid-H + no-force, e.g. BLASTER with H0/no-H) is the witness: it must now log the SDL/0106 decision banner as 8-bit-low-DMA + this SDL/0109 fall-back line, NEVER a 16-bit ch-0 program -- fixes the v1.4.0 SDL/0106 (highdma>=0) latent hang. Embed witness = strings|grep 'SDL/0109'. BANNER_REGEX idx 107.)"
  "0234 Campaign 2 AdLib backend selection (OPTIONAL -- 'audio backend: adlib (Campaign 2 -- native OPL2 FM synthesis ...)' emits at selectBackendFromEnv ONLY under SDL_HINT_DOSKUTSU_AUDIO_BACKEND=adlib. The default smoke leaves AUDIO_BACKEND unset (-> opl3 default) so this is ABSENT, expected, not a failure (same OPTIONAL gating as the wave-44 AUDIO_BACKEND=opl3/wb selection banners). The witness is the dedicated AdLib cell: a no-Sound-Blaster DOSBox config (sbtype=none + oplmode=opl2) with SET SDL_HINT_DOSKUTSU_AUDIO_BACKEND=adlib. Embed witness = strings|grep 'audio backend: adlib'. BANNER_REGEX idx 108.)"
  "0234 Campaign 2 AdLib no-mixer init path (OPTIONAL -- 'Sound system: AdLib (OPL2) path -- AUDIO_BACKEND=adlib. Skipping SB16 + SDL_mixer bring-up ...' emits at SoundManager::init ONLY under AUDIO_BACKEND=adlib. ABSENT in the default smoke (no adlib), expected. The runtime witness that the THIRD boot mode engaged: main.cpp omitted SDL_INIT_AUDIO yet kept SoundManager::init, which then skipped MIX_Init/MIX_CreateMixerDevice and took the OPL2 path. MUSIC ONLY -- a DAC-less AdLib card has no PCM SFX. Embed witness = strings|grep 'AdLib (OPL2) path'. BANNER_REGEX idx 109.)"
  "0235 Campaign 2 AdLib PIT/IRQ-0 OPL music pump started (OPTIONAL -- 'adlib: OPL music pump STARTED at N Hz (PIT ch0 / IRQ-0 drives MidiScheduler::tick_isr; BIOS 18.2 Hz tick chained; restored on exit) ...' emits at SoundManager::init ONLY under AUDIO_BACKEND=adlib AND when SDL_DOSOplTimerPumpStart succeeded (OPL2 detected, SB not hot, hz in [19,1000]). This is THE decisive runtime witness that the no-SB music clock engaged -- on a real AdLib/OPL2 card or PicoGUS /mode adlib the 8253 PIT ch0 is reprogrammed to N Hz (default 120; override SDL_HINT_DOSKUTSU_OPL_TIMER_HZ) and IRQ-0 drives the SAME tick_isr the SB path drives. ABSENT in the default smoke (no adlib), expected. NB per [[dosbox_not_proxy]] DOSBox-X confirms the banner/boot path but NOT real PIT/IRQ-0 timing -- the g2k AdLib iter is the perf/correctness witness (incl. PIT-restore-on-quit + BIOS-tick correctness). Embed witness = strings|grep 'OPL music pump STARTED'. BANNER_REGEX idx 110.)"
  "Campaign 3 GUS backend selection (OPTIONAL -- 'audio backend: gus (Campaign 3 ...)' at selectBackendFromEnv ONLY under AUDIO_BACKEND=gus; native Gravis UltraSound GF1 path; ABSENT in the default opl3 smoke, expected. Witnessed via the dedicated GUS cell / g2k /mode gus.)"
  "GUS (GF1) init path (OPTIONAL -- 'Sound system: GUS (GF1) path -- AUDIO_BACKEND=gus'; SoundManager GF1 bring-up; ABSENT in the default smoke, GUS-cell/g2k only.)"
  "GUS backend ready (OPTIONAL -- 'gus backend ready: GF1 detected'; GF1 wavetable detected + armed; GUS-cell/g2k only.)"
  "SDL/0112 GUS GF1 detect (OPTIONAL -- 'audio: SDL/0112 GUS GF1 detect: present=[01] ...'; GF1 probe result (base/irq/dma/dram/voices/rate); present=0 when no ULTRASND hint (DOSBox default). GUS-cell/g2k only.)"
  "GUS on_song_start instrument upload (OPTIONAL -- 'gus backend: on_song_start -- uploaded N instruments'; per-song .pat instrument DRAM upload count; GUS-cell/g2k only.)"
  "GUS SFX .pxt upload (OPTIONAL -- 'gus SFX: uploaded N of N .pxt to GF1 DRAM'; Pixtone SFX -> GF1 DRAM upload count; GUS-cell/g2k only.)"
  "GUS path Pixtone SFX uploaded (OPTIONAL -- 'Sound system: GUS path -- Pixtone SFX uploaded to GF1 voices'; GUS SFX voice residency; GUS-cell/g2k only.)"
  "#31 4-state audio enable (OPTIONAL -- 'Sound system: 4-state audio enable (#31) -- music=(on|OFF) sfx=(on|OFF)'; the #31 music/sfx enable matrix banner; emits per the configured state.)"
  "#31 No-Music backend (OPTIONAL -- 'audio backend: none (No Music -- #31)'; emits under AUDIO_BACKEND=none; ABSENT in the default smoke.)"
  "GUS MUSIC-ONLY path (OPTIONAL -- 'Sound system: GUS path -- SFX DISABLED ...MUSIC-ONLY GUS'; emits when GUS SFX is disabled (SFX_DEVICE=none frees full 1MB DRAM for music); GUS-cell/g2k only.)"
  "SDL/0112 GUS device up (OPTIONAL -- 'audio: SDL/0112 GUS device up: base=.. irq=.. dma=.. voices=.. rate=..Hz dram=..KB upload=pio'; GF1 device bring-up summary; GUS-cell/g2k only.)"
  "SDL/0112 GUS 16-bit TEST-TONE16 (OPTIONAL -- 'audio: SDL/0112 GUS TEST-TONE16: voice=.. addr=.. end=.. playing'; the 16-bit GF1 playback probe (#39 silence isolation); GUS-cell/g2k only.)"
  "GUS release-ramp lever (OPTIONAL -- 'gus backend: release-ramp (ENABLED (default)|DISABLED (killswitch =0))'; nx0254 software note-off release ramp; killswitch. GUS-cell/g2k only.)"
  "GUS multisample lever (OPTIONAL -- 'gus backend: multisample (ENABLED (default)|DISABLED (killswitch =0))'; nx0255 per-note multisample residency; killswitch. GUS-cell/g2k only.)"
  "SDL/0113 GUS SFX bank-align (OPTIONAL -- 'audio: SDL/0113 GUS SFX bank-align: ENGAGED (8-bit no-straddle 256K guard)'; DRAM 256K bank-straddle guard for 8-bit SFX; GUS-cell/g2k only.)"
  "GUS SFX DRAM-straddle diag (OPTIONAL -- 'gus SFX DRAM-straddle diag [nx0256]'; #38 DRAM bank-straddle observability in initGusSfx; GUS-cell/g2k only.)"
  "GUS SFX rendersafe guard (OPTIONAL -- 'gus SFX rendersafe [nx0257]: (ON|OFF (default))'; #38 pre-render OOM guard; GUS-cell/g2k only.)"
  "GUS SFX voice routing (OPTIONAL -- 'gus SFX voice routing [nx0259]:'; #38 SFX->reserved GF1 voice slice; GUS-cell/g2k only.)"
  "GUS SFX gain (OPTIONAL -- 'gus SFX gain [nx0259]: N%'; #38 SFX/music gain balance; GUS-cell/g2k only.)"
  "SDL/0115 banked-blit granularity fix (REQUIRED -- 'sdl: SDL/0115 BANK-GRAN-FIX (ENABLED|DISABLED) ...' emits once at DOSVESA_CreateWindowFramebuffer on EVERY boot, before the first flush. Proves the WinGranularity<WinSize multi-bank-walk correction (patches/SDL/0115) is in the binary AND ran. Default-ON; strict-'0' killswitch SDL_HINT_DOSKUTSU_BANK_GRAN_FIX=0 flips the text to DISABLED (still matches). On g2k gran==size==64KB the corrected walk is byte-identical to the legacy bank++ sequence. Embed witness = strings|grep 'BANK-GRAN-FIX'.)"
  "SDL/0116 bounded WaitForVBlank (REQUIRED -- 'sdl: SDL/0116 VBLANK-BOUND (ENABLED|DISABLED) ...' emits once at DOSVESA_CreateWindowFramebuffer on EVERY boot. Proves the mainline vblank-spin HW-IO-hang guard (patches/SDL/0116) is in the binary AND ran. Default-ON; strict-'0' killswitch SDL_HINT_DOSKUTSU_VBLANK_BOUND=0 flips the text to DISABLED (still matches). Embed witness = strings|grep 'VBLANK-BOUND'.)"
  "0264 AdLib PC-speaker SFX->beep mapping WIRED (OPTIONAL -- engine SFX->beep map; emits at SoundManager::init ONLY under AUDIO_BACKEND=adlib, the MUSIC-ONLY AdLib path; ABSENT in the default opl3 smoke, expected; witnessed via the dedicated AdLib DOSBox cell (sbtype=none + oplmode=opl2 + SDL_HINT_DOSKUTSU_AUDIO_BACKEND=adlib). SDL owns the default-ON killswitch SDL_HINT_DOSKUTSU_PCSPK_SFX=0. Embed witness = strings|grep 'AdLib PC-speaker SFX beeps'. BANNER_REGEX idx 108.)"
  "SDL/0118 PC-speaker square-wave SFX decision (OPTIONAL -- 'audio: SDL/0118 [pcspk] square-wave SFX (ENABLED|DISABLED)' on the SDL-log channel (SDLDBG.LOG); emits at the PC-speaker beep-path bring-up on the AdLib path only; ABSENT in the default opl3 smoke, expected; killswitch SDL_HINT_DOSKUTSU_PCSPK_SFX=0 -> v1.5.0 music-only byte-identical. Embed witness = strings|grep 'SDL/0118'. BANNER_REGEX idx 109.)"
  "0267 load_stage ring+DMA silence flush (OPTIONAL -- 'load_stage ring+DMA silence flush: (ENABLED|DISABLED)'; nx 0267 PicoGUS-SB cave-transition screech fix; LOG_INFO on first load_stage (first cave entry) narrating default-ON (calls the shipped SDL/0092 ring+DMA-zero at stage-load entry) vs DISABLED via killswitch SDL_HINT_DOSKUTSU_LOADSTAGE_SILENCE=0. Emits when the smoke drives into a stage; INFO-level so present on the verbose smoke. The screech-elimination effect is g2k-only (PicoGUS-in-SB-mode); DOSBox witnesses the banner/decision, not the audible screech per [[dosbox_not_proxy]]. Embed witness = strings|grep 'load_stage ring+DMA silence flush'.)"
  "SDL/0120 flush-quiesce Pixtone-IRQ-mix screech fix (OPTIONAL -- '...pixtone_quiesced=N) ring+DMA silence quiesce' on the SDL-log channel; SDL_DOSAudioFlushRingSilence now also quiesces the Lever-3 Pixtone IRQ-mix source (task #17 screech). Emits at every FlushRingSilence call (organya cold-render 0206/0209 AND load_stage entry via 0267), so witnessable in the DEFAULT smoke -- distinct from the AdLib-only [pcspk] banner. Per-cave g2k witness = pixtone_quiesced=N in the same line. Embed witness = strings|grep 'ring+DMA silence quiesce'; new export SDL_DOSAudioIsrRingWrite. Kept OPTIONAL (emit is cold-render/load_stage-path dependent).)"
  "0268 load-band audio-underrun probe (OPTIONAL -- '[loadband-stat cave=N dur_ms=N pix_active_entry=N irq_delta=N ...]' one line per cave; nx 0268 #17 diag, DEFAULT-OFF (opt-in SDL_HINT_DOSKUTSU_LOADBAND_STAT=1). Gated independently of LOADSTAGE_SILENCE so both screech A/B cells can carry it. ABSENT in the default smoke (hint unset), expected. The rc5 SCRA/SCRB g2k cells set it to snapshot the SB across the load_stage band. Embed witness = strings|grep 'loadband-stat'.)"
)

if [[ "$SKIP_GATE" == "1" ]]; then
  log ""
  log "=== Banner-emit gate SKIPPED (--skip-gate) ==="
  log "Logs still captured at $OUT_DIR/{debug,sdldbg}.log for flush-instr decomp."
  log "Caller must verify expected banner absences match the ablation contract."
  log "done. Artifacts in: $OUT_DIR"
  exit 0
fi

GATE_FAIL=0
GATE_FAIL_REQUIRED_ABSENT=0   # set only when a REQUIRED banner is absent (vs a FORBIDDEN one present); gates the contention retry below
log ""
log "=== Banner-emit gate (proves runtime invocation, not just embed) ==="

for i in "${!BANNER_REGEX[@]}"; do
  regex="${BANNER_REGEX[$i]}"
  severity="${BANNER_SEVERITY[$i]}"
  label="${BANNER_LABEL[$i]:-}"   # set-u-safe guard (belt-and-suspenders; arrays aligned 100=100=100 as of #17 realign -- removed an orphan SDL/0098 label that had no REGEX entry and drifted the tail off by one); display-only, gate keys on regex+severity

  hit_total=0
  hit_source=""
  for src in "$DEBUG_LOG" "$SDLDBG_LOG"; do
    [[ -f "$src" ]] || continue
    c=$(grep -cE "$regex" "$src" 2>/dev/null || true)
    c="${c:-0}"
    if [[ "$c" -gt 0 ]]; then
      hit_total=$((hit_total + c))
      hit_source="${hit_source:+$hit_source,}$(basename "$src")=$c"
    fi
  done

  case "$severity" in
    required)
      if [[ "$hit_total" -gt 0 ]]; then
        log "  PASS [$label] emits=$hit_total ($hit_source)"
      else
        log "  FAIL [$label] emits=0 -- REQUIRED banner absent; runtime code path is dead"
        log "        regex: $regex"
        GATE_FAIL=1
        GATE_FAIL_REQUIRED_ABSENT=1
      fi
      ;;
    forbidden)
      if [[ "$hit_total" -gt 0 ]]; then
        log "  FAIL [$label] emits=$hit_total ($hit_source) -- FORBIDDEN banner present; incomplete revert (banner literal in .rodata + runtime emit means the patch's code is still live)"
        log "        regex: $regex"
        GATE_FAIL=1
      else
        log "  PASS [$label] emits=0 (correctly absent)"
      fi
      ;;
    optional)
      log "  INFO [$label] emits=$hit_total ($hit_source) -- informational; no gate effect"
      ;;
  esac
done

# Contention-robust single retry (task #10). A REQUIRED default-on boot banner that
# is absent here is far more often a DISTURBED CAPTURE than a real regression: these
# banners (e.g. patch 0224's menu-slide forced eval at main.cpp boot) emit
# DETERMINISTICALLY on every clean boot -- verified 20/20 in a contention-free window
# -- so an absence almost always means the captured build/stage/LOGS/DEBUG.LOG was
# clobbered by a concurrent DOSBox sharing build/stage, or this run was killed mid-
# capture by a cross-workstream global pkill ([[dosbox_global_pkill_collides_concurrent_workstreams]]).
# A single clean-slate re-run distinguishes the two cases WITHOUT weakening the gate:
# a genuine dead-code regression is deterministic and fails the retry too (still
# caught + reported), while a transient contention/capture flake passes it. We retry
# at most ONCE, and ONLY for a REQUIRED-absent failure -- a FORBIDDEN-present failure
# is a deterministic incomplete-revert that a retry cannot clear, so it falls straight
# through to the hard FAIL below.
if [[ "$GATE_FAIL" -gt 0 && "$GATE_FAIL_REQUIRED_ABSENT" -gt 0 && "$SMOKE_ATTEMPT" -lt 2 ]]; then
  log ""
  log "GATE FAIL on attempt $SMOKE_ATTEMPT was a REQUIRED-banner absence. Those banners emit"
  log "deterministically every clean boot, so this is most likely a capture disturbed by"
  log "concurrent-DOSBox contention (shared build/stage log / cross-workstream pkill), not a"
  log "regression. Re-running ONCE on a clean slate -- a real regression fails the retry too."
  dbx_kill_conf "$CONF" KILL || true
  sleep 2
  exec env SMOKE_ATTEMPT=2 "$0" ${SMOKE_ORIG_ARGS[@]+"${SMOKE_ORIG_ARGS[@]}"}
fi

if [[ "$GATE_FAIL" -gt 0 ]]; then
  log ""
  log "GATE FAIL (attempt $SMOKE_ATTEMPT): one or more REQUIRED banners absent (or FORBIDDEN banners present)."
  if [[ "$SMOKE_ATTEMPT" -ge 2 ]]; then
    log "This failure REPRODUCED on a clean-slate retry -- it is NOT a transient contention"
    log "flake but a real regression (or a FORBIDDEN incomplete-revert)."
  fi
  log "This is the wave-38 failure mode: code that strings|grep finds in the binary"
  log "is not actually reached at runtime. Investigate the failing patch(es) before"
  log "shipping. See CLAUDE.md sec. Critical Rules -- Build verification."
  log "done. Artifacts in: $OUT_DIR"
  exit 5
fi

log ""
log "GATE PASS: all required banners emit at runtime."
log "done. Artifacts in: $OUT_DIR"
log "Review screenshots 01..08, debug.log, sdldbg.log."
