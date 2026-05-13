#!/usr/bin/env bash
# Wave-iter install template -- copies binary + BATs to a mounted CF card, with
# sha verification + BAT CRLF/ASCII gate + auto-unmount on success.
#
# USAGE
# -----
#   1. Copy this file to /tmp/install-wave<N>-main.sh for each iter.
#   2. Edit the CONFIG block below: TARBALL filename + EXPECTED_*_SHA per
#      binary + WAVE_TAG (used only in echo lines) + add/remove `verify_sha`
#      calls if the wave bundles a different binary count.
#   3. Update the on-g2k HERE-doc at the end to match the wave's PROBE leg +
#      PLAY matrix.
#   4. Operator runs: scp claude:/tmp/install-wave<N>-main.sh /tmp/ && bash /tmp/install-wave<N>-main.sh
#
# The script:
#   [1/6] verifies CF mounted at the operator's canonical mount point
#   [2/6] scp's the tarball from claude to laptop staging
#   [3/6] extracts tarball to CF
#   [4/6] sha-verifies every binary on CF against the EXPECTED_*_SHA constants
#   [5/6] CRLF + ASCII-verifies every *.BAT on CF (no LF-only, no UTF-8)
#   [6/6] sync + auto-unmount CF (udisksctl first, then umount fallback)
#
# BUNDLE-SIDE PREREQUISITE (run on claude before scp'ing the tarball):
#   Both the tarball MUST have CRLF + ASCII BATs at pack time -- the install
#   gate is the backstop, not the primary defense. See memory entry
#   `tarball_bundle_must_crlf_normalize_carry_forward_bats.md`. Recipe:
#
#     # In the staging dir before final `tar czf ...`:
#     for bat in DOSKUTSU/*.BAT; do
#       iconv -f UTF-8 -t ASCII//TRANSLIT < "$bat" \
#         | awk '{ printf "%s\r\n", $0 }' > "${bat}.tmp"
#       mv "${bat}.tmp" "$bat"
#     done
#     # Verify before tar:
#     file DOSKUTSU/*.BAT | grep -v "CRLF line terminators" \
#       && { echo "BUG: BAT file missing CRLF"; exit 1; }
#
# References (all banked in memory tree):
#   - tarball_bundle_must_crlf_normalize_carry_forward_bats.md
#   - install_logback_wrappers_must_auto_unmount.md
#   - verify_binary_before_tarball.md
#   - smoke_gate_must_verify_banner_emit_via_real_log.md
#   - build_orchestration_silently_drops_patches.md
#   - dos_batch_files_crlf.md  (base discipline)
#   - no_em_dashes.md          (no Unicode chars in BATs)
#   - realhw_iter_scp_handoff.md  (mount path convention)

set -e

# ============================================================================
# CONFIG -- EDIT PER WAVE
# ============================================================================

# Wave tag used in echo lines (cosmetic; doesn't drive filenames).
WAVE_TAG="wave-N"

# Tarball filename on claude side (under /tmp/).
TARBALL="doskutsu-cf-YYYY-MM-DD-wave-N-bin-XXXXXXXXXXXX.tar.gz"

# Expected full sha256 of every binary on CF after extraction.
# Add / remove EXPECTED_*_SHA constants + `verify_sha` calls to match the
# binaries this wave actually ships.
EXPECTED_DOSKUTSU_SHA="<full 64-char sha256 of DOSKUTSU.EXE>"
EXPECTED_BLTPAT_SHA="<full 64-char sha256 of BLTPAT.EXE; remove if probe not bundled>"
EXPECTED_AUDRQ_SHA="<full 64-char sha256 of AUDRQ.EXE; remove if probe not bundled>"
# EXPECTED_MIXBENCH_SHA="..."   # add if MIXBENCH bundled
# EXPECTED_ORGSYNTH_SHA="..."   # add if ORGSYNTH bundled

# PLAY cell indices for this wave. Drives the [5/6] BAT CRLF + ASCII check loop.
# Outer PLAY<n>.BAT + inner _PLAY<n>.BAT pair-checked per cell.
#
# REQUIRED cells: must exist on CF + must CRLF/ASCII-verify. Failure aborts install.
# OPTIONAL cells: may be absent (no fail). If present, must still CRLF/ASCII-verify.
#
# Sparse-PLAY waves (per `single_lever_per_binary_or_else_attribution_impossible.md`)
# can have non-consecutive cell indices -- e.g. wave-40: REQUIRED=( 1 ); OPTIONAL=( 5 ).
# Common wave-38/wave-39 shape: REQUIRED=( 1 2 3 4 ); OPTIONAL=( 5 ) for quit-cycle.
PLAY_REQUIRED_CELLS=( 1 2 3 4 )
PLAY_OPTIONAL_CELLS=( 5 )

# Names of any non-PLAY BATs (probe BATs typically). Empty if none.
PROBE_BATS=( BLTPAT AUDRQ )    # adjust per wave
# PROBE_BATS=( BLTPAT AUDRQ MIXBENCH ORGSYNTH )   # wave-38-style

# Canonical CF mount path + staging dir on operator's laptop.
# These are stable across waves; do not edit unless the operator's setup
# changed.
CF_MOUNT="/media/micheal/DOS"
CF_GAME_DIR="${CF_MOUNT}/doskutsu"
STAGING="/home/micheal/Projects/gateway2000/doskutsu"

# ============================================================================
# IMPLEMENTATION -- typically untouched per wave
# ============================================================================

echo "=== ${WAVE_TAG} install ==="

echo "[1/6] verify CF mounted"
if [ ! -d "${CF_GAME_DIR}" ]; then
  echo "  FAIL: ${CF_GAME_DIR} missing -- CF not mounted at ${CF_MOUNT}"
  exit 1
fi
echo "  PASS: ${CF_GAME_DIR} present"

echo "[2/6] scp tarball claude -> laptop"
mkdir -p "${STAGING}"
scp "claude:/tmp/${TARBALL}" "${STAGING}/"

echo "[3/6] extract tarball to CF"
tar -xzf "${STAGING}/${TARBALL}" -C "${CF_MOUNT}/"
echo "  PASS: extracted to ${CF_GAME_DIR}"

echo "[4/6] verify binary shas on CF"
verify_sha() {
  local name="$1" expected="$2"
  if [ ! -f "${CF_GAME_DIR}/${name}" ]; then
    echo "  FAIL: ${name} missing on CF"
    exit 1
  fi
  local actual
  actual=$(sha256sum "${CF_GAME_DIR}/${name}" | awk '{print $1}')
  if [ "${actual}" != "${expected}" ]; then
    echo "  FAIL: ${name} sha mismatch"
    echo "    expected: ${expected}"
    echo "    actual:   ${actual}"
    exit 1
  fi
  echo "  PASS: ${name} ${actual:0:12}"
}
verify_sha DOSKUTSU.EXE "${EXPECTED_DOSKUTSU_SHA}"
[ -n "${EXPECTED_BLTPAT_SHA:-}"   ] && verify_sha BLTPAT.EXE   "${EXPECTED_BLTPAT_SHA}"
[ -n "${EXPECTED_AUDRQ_SHA:-}"    ] && verify_sha AUDRQ.EXE    "${EXPECTED_AUDRQ_SHA}"
[ -n "${EXPECTED_MIXBENCH_SHA:-}" ] && verify_sha MIXBENCH.EXE "${EXPECTED_MIXBENCH_SHA}"
[ -n "${EXPECTED_ORGSYNTH_SHA:-}" ] && verify_sha ORGSYNTH.EXE "${EXPECTED_ORGSYNTH_SHA}"

echo "[5/6] verify BAT files are CRLF + ASCII"
check_bat() {
  local name="$1"
  if [ ! -f "${CF_GAME_DIR}/${name}" ]; then
    echo "  FAIL: ${name} missing on CF"
    exit 1
  fi
  if ! file "${CF_GAME_DIR}/${name}" | grep -q "CRLF line terminators"; then
    echo "  FAIL: ${name} missing CRLF -- bundler did not normalize at pack time"
    exit 1
  fi
  if file "${CF_GAME_DIR}/${name}" | grep -q "UTF-8"; then
    echo "  FAIL: ${name} has UTF-8 multi-byte chars -- DOS COMMAND.COM will mis-parse"
    exit 1
  fi
}
check_bat_optional() {
  # Absence allowed (optional cell may not be bundled this wave); presence still
  # requires CRLF + ASCII.
  local name="$1"
  [ -f "${CF_GAME_DIR}/${name}" ] || return 0
  if ! file "${CF_GAME_DIR}/${name}" | grep -q "CRLF line terminators"; then
    echo "  FAIL: ${name} present but missing CRLF -- bundler did not normalize at pack time"
    exit 1
  fi
  if file "${CF_GAME_DIR}/${name}" | grep -q "UTF-8"; then
    echo "  FAIL: ${name} present but has UTF-8 multi-byte chars -- DOS COMMAND.COM will mis-parse"
    exit 1
  fi
}
for probe in "${PROBE_BATS[@]}"; do
  check_bat "${probe}.BAT"
done
for n in "${PLAY_REQUIRED_CELLS[@]}"; do
  check_bat "PLAY${n}.BAT"
  check_bat "_PLAY${n}.BAT"
done
for n in "${PLAY_OPTIONAL_CELLS[@]}"; do
  check_bat_optional "PLAY${n}.BAT"
  check_bat_optional "_PLAY${n}.BAT"
done
total_play_bats=$(( ( ${#PLAY_REQUIRED_CELLS[@]} + ${#PLAY_OPTIONAL_CELLS[@]} ) * 2 ))
total_bats=$(( ${#PROBE_BATS[@]} + total_play_bats ))
echo "  PASS: ${total_bats} BAT files (${#PROBE_BATS[@]} PROBE + ${#PLAY_REQUIRED_CELLS[@]} req PLAY pair + ${#PLAY_OPTIONAL_CELLS[@]} opt PLAY pair) CRLF + ASCII"

echo "[6/6] sync + unmount CF"
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
    echo "  Run manually before moving CF to g2k:"
    echo "    udisksctl unmount -b ${cf_device:-${CF_MOUNT}}"
    echo "    OR: sudo umount ${CF_MOUNT}"
  fi
else
  echo "  CF already unmounted"
fi
echo

echo "=== install OK; insert CF in g2k + boot to DOS ==="
echo
echo "On g2k DOS prompt, run:"
echo
# ----------------------------------------------------------------------------
# EDIT THE HERE-DOC BELOW PER WAVE -- this is what the operator types at the
# DOS prompt after inserting the CF in g2k.
# ----------------------------------------------------------------------------
cat <<'EOF'
C:
CD \DOSKUTSU

REM === PROBE leg (BEFORE PLAY; one-shot probes; XX-YY sec total) ===
REM HAZARD notes per probe (screen flash, audio buzz, watchdog timeout)
BLTPAT.BAT
AUDRQ.BAT

REM === PLAY matrix ===
REM PLAY1 = description of cell
REM PLAY2 = description of cell
REM PLAY3 = description of cell
REM PLAY4 = description of cell
PLAY1.BAT
PLAY2.BAT
PLAY3.BAT
PLAY4.BAT

REM === OPTIONAL PLAY5 (skip if time-short) ===
PLAY5.BAT
EOF
echo
echo "Each PLAY engine writes its logs DIRECTLY as W<wave>A<n>.LOG + W<wave>A<n>SDL.LOG"
echo "(no REN needed; LOG_TAG env-var is set inside each _PLAY<n>.BAT child shell)."
echo
echo "After all PLAYs complete, re-insert CF in laptop and run:"
echo "  scp claude:/tmp/logback-${WAVE_TAG}-main.sh /tmp/ && bash /tmp/logback-${WAVE_TAG}-main.sh"
