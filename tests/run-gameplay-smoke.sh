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

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCHER="$REPO_ROOT/tools/dosbox-launch.sh"
LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE --fast)
OUT_DIR="/tmp/gameplay-smoke"
KEEP_RUNNING=0
SKIP_GATE=0
DISPLAY="${DOSBOX_DISPLAY:-:0}"

while (($#)); do
  case "$1" in
    --parity)         LAUNCHER_FLAGS=(--stage --exe DOSKUTSU.EXE) ;;
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

# PLAY.TAS sanity check (catches the wave-44 stub regression). When a
# PLAY.TAS is present at the stage root, it must be a real recording, not
# the 28-byte DTASv1-header-only stub that auto-exits at tick 1 the moment
# TAS replay engages. Absence is OK -- existing gameplay smoke does not
# require TAS replay (it drives input via xdotool). But a sub-1000-byte
# PLAY.TAS is the wave-44 bug shape and must be loud, not silent.
# scripts/stage-tas.sh (called from `make stage`) is responsible for
# staging the canonical 1932-byte recording.
STAGED_TAS="$REPO_ROOT/build/stage/PLAY.TAS"
if [[ -f "$STAGED_TAS" ]]; then
  TAS_BYTES=$(stat -c%s "$STAGED_TAS")
  if (( TAS_BYTES < 1000 )); then
    log "FAIL: $STAGED_TAS is $TAS_BYTES bytes (< 1000 = stub regression)."
    log "      Fix: run ./scripts/stage-tas.sh or set DOSKUTSU_TAS_SRC."
    log "      See scripts/stage-tas.sh header for the wave-44 rationale."
    exit 5
  fi
  TAS_SHA=$(sha256sum "$STAGED_TAS" | awk '{print $1}')
  log "staged PLAY.TAS: $TAS_BYTES bytes, sha ${TAS_SHA:0:12}"
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
  pkill -x dosbox-x || true
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
  "opl3 backend: opl3-patches\.dat (loaded|not found|invalid|truncated|version mismatch|too many programs)"
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
  "audio mid-gap pump: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "data cache: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "audio tick-boundary pump: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "pixtone multi-source probe: (ENABLED \(opt-in\)|DISABLED \(default\))"
  "pixtone IRQ-mix: (ENABLED \(default; SDL backend engaged;.*\)|DISABLED \(L3 requested but SDL backend refused;.*\)|DISABLED \(killswitch;.*\))"
  "midi ISR tick: engine (registered tick_isr callback \(L2b active.*\)|skipped registration.*)"
  "audio: SDL/0073 rate-diag: req_freq="
  "audio: SDL/0074 RATEDIV auto: .*master_rate=11025"
  "audio: SDL/0074 SB16 mixer balance: master=31 voice=31 fm=28"
  "audio: SDL/0071 pixtone IRQ-mix: ENABLED"
  "audio: SDL/0072 midi tick from ISR: ENABLED"
  "audio: SDL/0074 RATEDIV auto: dev_freq="
  "audio: SDL/0074 SB16 mixer balance: (master=|DISABLED)"
  "midi ISR tick: engine registered tick_isr callback \(L2b active.*\)"
  "opl3 backend: PATCH_ORGAN \+ PATCH_MALLET RR=8"
  "audio: SDL/0079 TL=0x3F sweep applied \(18 voices"
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
)
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
  "wave-46 OPL3 patch-bank loader (patch 0141; optional -- emits when MidiBackendOpl3 ctor runs; one of: loaded / not found / invalid / truncated / version mismatch / too many programs; default ship state is 'not found' since data/opl3-patches.dat is a ride-along for a follow-on iter)"
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
log ""
log "=== Banner-emit gate (proves runtime invocation, not just embed) ==="

for i in "${!BANNER_REGEX[@]}"; do
  regex="${BANNER_REGEX[$i]}"
  severity="${BANNER_SEVERITY[$i]}"
  label="${BANNER_LABEL[$i]}"

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

if [[ "$GATE_FAIL" -gt 0 ]]; then
  log ""
  log "GATE FAIL: one or more REQUIRED banners absent (or FORBIDDEN banners present)."
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
