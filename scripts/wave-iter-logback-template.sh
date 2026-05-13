#!/usr/bin/env bash
# Wave-iter logback template -- pulls PLAY + probe logs off the CF card to
# claude side, with graceful missing-file handling + auto-unmount on exit.
#
# USAGE
# -----
#   1. Copy this file to /tmp/logback-wave<N>-main.sh for each iter.
#   2. Edit the CONFIG block below: WAVE_TAG + PLAY_REQUIRED_CELLS +
#      PLAY_OPTIONAL_CELLS + PROBE_LOGS arrays + WAVE_DIR (where on claude side
#      the logs land).
#   3. Operator runs: scp claude:/tmp/logback-wave<N>-main.sh /tmp/ && bash /tmp/logback-wave<N>-main.sh
#
# Conventions:
#   - Engine writes per-PLAY logs as <TAG>.LOG via DOSKUTSU_LOG_TAG env-var
#     (per patch nxengine/0069). SDL writes <TAG>SDL.LOG (per patch SDL/0024).
#     LOG_TAG is set inside each _PLAY<n>.BAT child shell to W<wave>A<n>.
#   - No D<n>.LOG intermediates; no operator REN required.
#   - PLAY cells in PLAY_REQUIRED_CELLS use scp_one_required (reports MISSING
#     if not on CF; logback continues; tally counter increments). Cells in
#     PLAY_OPTIONAL_CELLS use scp_one_optional (silent skip if not on CF).
#   - Sparse-PLAY waves (per `single_lever_per_binary_or_else_attribution_impossible.md`)
#     can have non-consecutive cell indices in either array.
#   - Final step: sync + auto-unmount CF, same canonical block as install side.
#
# References (banked in memory tree):
#   - install_logback_wrappers_must_auto_unmount.md
#   - realhw_iter_scp_handoff.md
#   - logback_optional_artifacts_conditional_scp.md
#   - iter_handoff_oneliner_style.md

# NOT -e: we want to continue on missing individual logs (operator may have
# skipped a PLAY, or a PROBE may have legitimately produced no output).
set -u

# ============================================================================
# CONFIG -- EDIT PER WAVE
# ============================================================================

WAVE_TAG="wave-N"              # used in echo lines + WAVE_DIR default
WAVE_DIR="/tmp/wave<N>"        # claude-side landing dir

# PLAY cell indices to pull logs for. REQUIRED -> scp_one_required (reports
# MISSING if not on CF; tally counter increments). OPTIONAL -> scp_one_optional
# (silent skip if not on CF). Sparse-PLAY waves can have non-consecutive indices
# in either array. Common shapes:
#   wave-38/wave-39: REQUIRED=( 1 2 3 4 ); OPTIONAL=( 5 ) for quit-cycle
#   wave-40 sparse: REQUIRED=( 1 );        OPTIONAL=( 5 )
PLAY_REQUIRED_CELLS=( 1 2 3 4 )
PLAY_OPTIONAL_CELLS=( 5 )

# Probe logs to pull from CF. Each entry maps a CF filename to a label.
# Mark as optional if the probe may legitimately not have completed (e.g.
# AUDRQ v2 deadman-switch path).
declare -A PROBE_LOGS_REQUIRED=(
  [BLTPAT.LOG]="BLTPAT_V2 hail-mary sanity-anchor"
)
declare -A PROBE_LOGS_OPTIONAL=(
  [AUDRQ.LOG]="AUDRQ v2 (may be partial if v2 deadman fired)"
  # [MIXBENCH.LOG]="MIXBENCH audio Tier-1"
  # [ORGSYNTH.LOG]="ORGSYNTH audio Tier-2"
)

# Canonical CF mount path. Stable across waves.
CF_MOUNT="/media/micheal/DOS"
CF_GAME_DIR="${CF_MOUNT}/doskutsu"

# ============================================================================
# IMPLEMENTATION -- typically untouched per wave
# ============================================================================

echo "=== ${WAVE_TAG} logback ==="

if [ ! -d "${CF_GAME_DIR}" ]; then
  echo "FAIL: ${CF_GAME_DIR} missing -- CF not mounted at ${CF_MOUNT}"
  exit 1
fi

ssh claude "mkdir -p ${WAVE_DIR}"

pulled=0
skipped=0

scp_one_required() {
  local src="$1" dst="$2" label="$3"
  if [ ! -f "${src}" ]; then
    echo "  MISSING: ${src} (${label}) -- PLAY may have been skipped or crashed"
    skipped=$((skipped+1))
    return 0
  fi
  if scp "${src}" "claude:${WAVE_DIR}/${dst}" >/dev/null; then
    echo "  OK: ${label} -> ${dst}"
    pulled=$((pulled+1))
  else
    echo "  SCP-FAIL: ${src} (${label})"
    skipped=$((skipped+1))
  fi
}

scp_one_optional() {
  local src="$1" dst="$2" label="$3"
  if [ ! -f "${src}" ]; then
    echo "  skip: ${label} (optional; not present)"
    return 0
  fi
  if scp "${src}" "claude:${WAVE_DIR}/${dst}" >/dev/null; then
    echo "  OK: ${label} -> ${dst}"
    pulled=$((pulled+1))
  fi
}

# Compute the wave tag prefix from WAVE_TAG (e.g. "wave-39" -> "W39A").
# Operator can override LOG_PREFIX env-var if this guess is wrong.
wave_num=$(echo "${WAVE_TAG}" | sed -E 's/[^0-9]+//g')
LOG_PREFIX="${LOG_PREFIX:-W${wave_num}A}"

req_list=$(IFS=' '; echo "${PLAY_REQUIRED_CELLS[*]:-}")
opt_list=$(IFS=' '; echo "${PLAY_OPTIONAL_CELLS[*]:-}")
echo
echo "--- PLAY engine + SDL logs (required cells: ${req_list:-none}; optional: ${opt_list:-none}) ---"
for n in "${PLAY_REQUIRED_CELLS[@]}"; do
  scp_one_required "${CF_GAME_DIR}/${LOG_PREFIX}${n}.LOG"    "${LOG_PREFIX}${n}.LOG"    "engine ${LOG_PREFIX}${n}"
  scp_one_required "${CF_GAME_DIR}/${LOG_PREFIX}${n}SDL.LOG" "${LOG_PREFIX}${n}SDL.LOG" "SDL ${LOG_PREFIX}${n}"
done
for n in "${PLAY_OPTIONAL_CELLS[@]}"; do
  scp_one_optional "${CF_GAME_DIR}/${LOG_PREFIX}${n}.LOG"    "${LOG_PREFIX}${n}.LOG"    "engine ${LOG_PREFIX}${n} (optional)"
  scp_one_optional "${CF_GAME_DIR}/${LOG_PREFIX}${n}SDL.LOG" "${LOG_PREFIX}${n}SDL.LOG" "SDL ${LOG_PREFIX}${n} (optional)"
done

echo
echo "--- PROBE logs ---"
for fname in "${!PROBE_LOGS_REQUIRED[@]}"; do
  scp_one_required "${CF_GAME_DIR}/${fname}" "${fname}" "${PROBE_LOGS_REQUIRED[${fname}]}"
done
for fname in "${!PROBE_LOGS_OPTIONAL[@]}"; do
  scp_one_optional "${CF_GAME_DIR}/${fname}" "${fname}" "${PROBE_LOGS_OPTIONAL[${fname}]}"
done

echo
echo "=== logback summary ==="
echo "  pulled: ${pulled} files"
echo "  skipped/missing: ${skipped} files"
echo "  destination: claude:${WAVE_DIR}/"

echo
echo "[final] sync + unmount CF"
sync
echo "  sync done"
if mountpoint -q "${CF_MOUNT}"; then
  cf_device=$(findmnt -no SOURCE "${CF_MOUNT}" 2>/dev/null || true)
  echo "  CF device: ${cf_device:-(none found)}"
  unmounted=0
  if [ -n "${cf_device}" ] && udisksctl unmount -b "${cf_device}"; then
    echo "  PASS: unmounted via udisksctl"
    unmounted=1
  elif sudo -n umount "${CF_MOUNT}" 2>&1; then
    echo "  PASS: unmounted via sudo umount"
    unmounted=1
  elif umount "${CF_MOUNT}" 2>&1; then
    echo "  PASS: unmounted via umount"
    unmounted=1
  fi
  if [ ${unmounted} -eq 0 ]; then
    echo "  WARN: auto-unmount failed; CF still mounted at ${CF_MOUNT}"
    echo "  Run manually:"
    echo "    udisksctl unmount -b ${cf_device:-${CF_MOUNT}}"
    echo "    OR: sudo umount ${CF_MOUNT}"
  fi
else
  echo "  CF already unmounted"
fi
echo
echo "Next: flush-instr decomp at docs/PHASE11-${WAVE_TAG^^}-FINDINGS.md"
