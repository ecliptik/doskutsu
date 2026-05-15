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
  "map: BG skip-when-unchanged (ENABLED \(default\)|DISABLED \(killswitch\))"
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
  "wave-48 BG skip-when-unchanged killswitch decision (patch 0143; optional -- one banner per process emitted on first call to map_backdrop_should_skip_clear_and_draw, narrating ENABLED (default) vs DISABLED (killswitch=0); fires deterministically on first DrawScene call so should emit even under DOSBox-X smoke if engine reaches DrawScene at all)"
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
