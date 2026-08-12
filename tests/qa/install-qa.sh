#!/usr/bin/env bash
# install-qa-v163.sh -- ONE-TIME CF population for the doskutsu v1.6.3 FULL-QA
# pass (reduced 2026-07-26, re-ordered 2026-08-05 into 3 card-major phases:
# 486DX2-66+ViRGE+PicoGUS, then POD-83+ViRGE+PicoGUS, then POD-83+ViRGE+Vibra).
# Runs on the operator laptop; self-fetches the
# QA payload tarball from claude, sha-asserts the shipping binaries, populates
# the CF once (game + CWSDPMI + SETUP + QA BAT kit + CFG seeds + MIDI sets +
# opl3bank + pre-rendered Organya 11025 cache + showcase TAS + CHECKLST),
# ensures + CLEARS the CF LOGS dir, audits the CF AUTOEXEC for stale DOSKUTSU_*
# SETs (LOUD, never edits), then unmounts so the CF can move to the test PC.
#
#   Operator runs:  scp claude:/tmp/install-qa-v163.sh /tmp/ && bash /tmp/install-qa-v163.sh
#
# The payload was BUILT ON CLAUDE from tag v1.6.3 (working tree d4fc462 = v1.6.3,
# clean); this script does not rebuild -- it fetches that verified payload and
# re-asserts the shipping shas on the CF. Findings from this pass are v1.6.4
# candidates, not tag blocks.
set -e

# ============================================================================
# CONFIG
# ============================================================================
TARBALL="doskutsu-cf-2026-08-10-qa-v170-09e449c5a81d.tar.gz"

EXP_DOSKUTSU_SHA="09e449c5a81ddbdc405c7fc07c0964685db8e1c5925edf3eb9086fb97e35cc42"
EXP_SETUP_SHA="723d6991b30308083daadcc8b35ca972cff1bb3604e7fec3f4628b2c0acb9ba3"
EXP_SETUPBAT_SHA="ee9140aac514abe1d6eaca9d3c08817f1599201f59916b145c904b5c3ed18741"
EXP_CWSDPMI_SHA="2de899fecaa90632b8b9bdfc0305cb0375e59ae252c37e32d06c1ed3f98a8f44"
EXP_TAS_SHA="5c661a8723e518e61fbd23f8d2819c499a8232b6ba2a6ea1f9d741b80f230266"  # QA.TAS fallback reel (492 B)

CF_MOUNT="/media/micheal/DOS"
CF_GAME_DIR="${CF_MOUNT}/doskutsu"
CF_LOGS="${CF_GAME_DIR}/LOGS"
STAGING="/home/micheal/Projects/gateway2000/doskutsu"

# ============================================================================
echo "=== doskutsu v1.6.3 FULL-QA :: CF population (one-time) ==="

echo "[1/8] verify CF mounted at ${CF_MOUNT}"
if [ ! -d "${CF_MOUNT}" ]; then
  echo "  FAIL: ${CF_MOUNT} missing -- CF not mounted."
  exit 1
fi
echo "  PASS: ${CF_MOUNT} present"

echo "[2/8] fetch payload tarball claude -> laptop"
mkdir -p "${STAGING}"
scp "claude:/tmp/${TARBALL}" "${STAGING}/"
echo "  PASS: ${STAGING}/${TARBALL}"

echo "[3/8] extract payload onto CF (non-destructive overlay; base DATA preserved)"
mkdir -p "${CF_GAME_DIR}"
# An operator RECORD.BAT take lives at DOSKUTSU/QA.TAS and the payload carries
# the shipped FALLBACK reel at that same path -- extracting would silently
# destroy the take, and every watch cell would then replay the wrong reel.
# Stash it, extract, put it back.
OPTAKE=""
if [ -f "${CF_GAME_DIR}/QA.TAS" ]; then
  cursha=$(sha256sum "${CF_GAME_DIR}/QA.TAS" | cut -d' ' -f1)
  cursz=$(stat -c%s "${CF_GAME_DIR}/QA.TAS" 2>/dev/null || echo 0)
  if [ "${cursha}" != "${EXP_TAS_SHA}" ] && [ "${cursz}" -gt 100 ]; then
    OPTAKE="${STAGING}/QA-OPERATOR-TAKE.TAS"
    cp "${CF_GAME_DIR}/QA.TAS" "${OPTAKE}"
    echo "  OPERATOR REEL FOUND on CF (${cursz} B, sha ${cursha:0:12}) -- preserving it"
    echo "  backup also kept at ${OPTAKE}"
  fi
fi
# Extract everything EXCEPT the big Organya-HQ (22050 stereo) cache -- that is
# gated on CF free space in step [5] so a tight card never ENOSPCs mid-extract.
tar -xzf "${STAGING}/${TARBALL}" -C "${CF_MOUNT}/" \
    --exclude='DOSKUTSU/CACHE/22050_2' --exclude='DOSKUTSU/CACHE/22050_2/*'
echo "  PASS: extracted DOSKUTSU/ into ${CF_GAME_DIR} (HQ cache deferred)"
if [ -n "${OPTAKE}" ]; then
  cp "${OPTAKE}" "${CF_GAME_DIR}/QA.TAS"
  echo "  PASS: operator reel RESTORED over the shipped fallback"
fi

echo "[4/8] sha-assert shipping binaries on CF"
assert_sha() {
  local name="$1" want="$2" f="${CF_GAME_DIR}/$1"
  [ -f "$f" ] || { echo "  FAIL: ${name} missing on CF"; exit 1; }
  local got; got=$(sha256sum "$f" | cut -d' ' -f1)
  if [ "$got" != "$want" ]; then
    echo "  FAIL: ${name} sha mismatch"; echo "    want ${want}"; echo "    got  ${got}"; exit 1
  fi
  echo "  PASS: ${name} ${got:0:12}"
}
assert_sha DOSKUTSU.EXE "${EXP_DOSKUTSU_SHA}"
assert_sha SETUP.EXE    "${EXP_SETUP_SHA}"      # 723d6991 = AUDIOTEST=1 release (never the stub)
assert_sha SETUP.BAT    "${EXP_SETUPBAT_SHA}"   # ee9140aa = 14 audio clears + SETUP.EXE
assert_sha CWSDPMI.EXE  "${EXP_CWSDPMI_SHA}"
# QA.TAS is EITHER the shipped fallback OR the operator's own recorded take --
# assert-by-sha only makes sense for the former.
tassha=$(sha256sum "${CF_GAME_DIR}/QA.TAS" | cut -d' ' -f1)
tassz=$(stat -c%s "${CF_GAME_DIR}/QA.TAS" 2>/dev/null || echo 0)
if [ "${tassha}" = "${EXP_TAS_SHA}" ]; then
  echo "  PASS: QA.TAS = shipped fallback reel (${tassz} B)"
elif [ "${tassz}" -gt 100 ]; then
  echo "  PASS: QA.TAS = OPERATOR TAKE, ${tassz} B, sha ${tassha:0:12} (preserved)"
else
  echo "  FAIL: QA.TAS is ${tassz} B -- too small to be a real reel"; exit 1
fi

echo "[5/8] cache-key + kit presence sanity"
if [ -f "${CF_GAME_DIR}/CACHE/11025_1/READY.OK" ]; then
  key=$(cat "${CF_GAME_DIR}/CACHE/11025_1/READY.OK" 2>/dev/null | tr -d '\r\n')
  if [ "$key" = "f8d446b4b0e0" ]; then
    echo "  PASS: Organya 11025 cache keyed f8d446b4b0e0 (hits on aef02e5c)"
  else
    echo "  WARN: cache key '${key}' != f8d446b4b0e0 -- Organya cells may cold-render"
  fi
else
  echo "  WARN: no CACHE/11025_1/READY.OK on CF -- Organya cells will cold-render"
fi
for need in QA.BAT CHECKLST.TXT CFGS/OPL3.CFG DATA/orgmid2/access.mid DATA/midi/access.mid DATA/opl3bank.dat; do
  [ -e "${CF_GAME_DIR}/${need}" ] || echo "  WARN: expected kit file missing: ${need}"
done
[ -e "${CF_GAME_DIR}/RECORD.BAT" ] && echo "  PASS: RECORD.BAT present (optional operator-take reel)" || echo "  WARN: RECORD.BAT missing"

# Core Cave Story data must ALREADY be on the CF. The kit DATA/ is an AUDIO
# OVERLAY ONLY (midi sets + opl3bank), NOT base data -- and the tar extract
# above MERGES (adds/overwrites, NEVER deletes), so an existing g2k card keeps
# all its sprites/maps/fonts/org. On a FRESH card with no base data, every cell
# crashes ("Couldn't open data/spot.png"). Warn LOUD so it's caught here.
core_ok=0
for probe in DATA/spot.png data/spot.png DATA/font_1.fnt data/font_1.fnt DATA/Stage data/Stage; do
  [ -e "${CF_GAME_DIR}/${probe}" ] && core_ok=1 && break
done
if [ ${core_ok} -eq 1 ]; then
  echo "  PASS: core Cave Story data present on CF (audio overlay merged onto it)"
else
  echo "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  echo "  !! CORE CAVE STORY DATA NOT FOUND on this CF (no DATA/spot.png)."
  echo "  !! The kit ships only the AUDIO overlay (MIDI sets + opl3bank), NOT"
  echo "  !! base data. EVERY cell will CRASH ('Couldn't open data/spot.png')"
  echo "  !! until Cave Story data is extracted onto the CF (docs/ASSETS.md),"
  echo "  !! OR you use your existing g2k card which already has it."
  echo "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
fi

# Organya-HQ (22050 stereo) cache -- extract ONLY if the CF has room (~190 MB).
# Otherwise skip it: cell 1.12 falls back to a one-time cold render (the wizard
# already documents this) rather than filling the card. Record the outcome in a
# laptop-side status file the wizard reads for the 1.12 screen.
QA_DIR="${HOME}/qa-v163"; mkdir -p "${QA_DIR}"
HQ_STATE="absent"
if tar -tzf "${STAGING}/${TARBALL}" 2>/dev/null | grep -q 'DOSKUTSU/CACHE/22050_2/.*READY.OK'; then
  avail_kb=$(df -Pk "${CF_MOUNT}" | awk 'NR==2{print $4}')
  need_kb=215040   # ~210 MB (HQ cache ~190 MB + margin)
  if [ "${avail_kb:-0}" -ge "${need_kb}" ]; then
    tar -xzf "${STAGING}/${TARBALL}" -C "${CF_MOUNT}/" DOSKUTSU/CACHE/22050_2 2>/dev/null || true
    if [ -f "${CF_GAME_DIR}/CACHE/22050_2/READY.OK" ]; then
      echo "  PASS: Organya-HQ 22050 cache extracted (CF had $((avail_kb/1024)) MB free)"
      HQ_STATE="shipped"
    else
      echo "  WARN: HQ cache extract produced no READY.OK -- 1.12 will cold-render"
      HQ_STATE="skipped"
    fi
  else
    echo "  NOTE: CF only $(( ${avail_kb:-0} /1024 )) MB free (< 210 MB) -- SKIPPING HQ cache."
    echo "        Cell 1.12 will do a one-time cold render on the bench (wizard fallback)."
    HQ_STATE="skipped"
  fi
else
  echo "  NOTE: no HQ (22050_2) cache in payload -- cell 1.12 will cold-render (wizard fallback)."
fi
printf 'HQCACHE=%s\n' "${HQ_STATE}" > "${QA_DIR}/KIT_STATUS"
echo "  wrote ${QA_DIR}/KIT_STATUS (HQCACHE=${HQ_STATE})"

echo "[5b/8] fetch the round-2 QA bundle (BATs + cleaner + verdict script)"
# These postdate the payload tarball, so they are fetched separately rather
# than repacked -- a payload rebuild costs a ~40 min Organya re-render and a
# re-populate, which is far too much to move a batch file. Fetching here is
# what makes THIS script self-sufficient: re-running it puts the current BATs
# on the card with no follow-up patch step.
QA_BUNDLE_DIR="$(mktemp -d)"
trap 'rm -rf "${QA_BUNDLE_DIR}"' EXIT
QA_BUNDLE_OK=0
if [ -n "${QA_BATS_TARBALL:-}" ] && [ -f "${QA_BATS_TARBALL}" ]; then
  cp -f "${QA_BATS_TARBALL}" "${QA_BUNDLE_DIR}/qa-bats.tar.gz" && QA_BUNDLE_OK=1
  echo "  using local bundle: ${QA_BATS_TARBALL}"
elif scp -q claude:/tmp/qa-bats.tar.gz "${QA_BUNDLE_DIR}/" 2>/dev/null; then
  QA_BUNDLE_OK=1
  echo "  fetched claude:/tmp/qa-bats.tar.gz"
else
  echo "  WARNING: could not fetch qa-bats.tar.gz -- round-2 BATs will NOT be"
  echo "  installed and the card keeps whatever BATs it already has."
fi
if [ "${QA_BUNDLE_OK}" = "1" ]; then
  tar xzf "${QA_BUNDLE_DIR}/qa-bats.tar.gz" -C "${QA_BUNDLE_DIR}" || QA_BUNDLE_OK=0
fi

echo "[5c/8] purge accumulated iter debris (superseded binaries, retired probes)"
# ~141 entries on the card that no current sweep references: old game binaries
# at ~7 MB each, one-off probe EXEs, retired reels, and closed campaigns' BAT
# cells. The list is DERIVED (present on the card, absent from the kit
# tarball) minus an explicit keep-list. Kept on purpose: LICENSE/GPLV3/
# 3RDPARTY/README -- the binary is GPLv3 and its licence text stays with it --
# and DOSKUTSU.CFG/SETTINGS.DAT, which are runtime state.
if [ "${QA_BUNDLE_OK}" = "1" ] && [ -f "${QA_BUNDLE_DIR}/cf-clean.sh" ]; then
  CF_GAME_DIR="${CF_GAME_DIR}" bash "${QA_BUNDLE_DIR}/cf-clean.sh" \
    || echo "  (cf-clean reported a problem; continuing -- debris is cosmetic)"
else
  echo "  SKIP: no cf-clean.sh in the bundle"
fi

echo "[5d/8] install the round-2 QA BATs (PROVE / RB / GAP / EAR)"
# PROVE.BAT switches the PicoGUS sb -> adlib itself. That is load-bearing: the
# PIT music pump is the single owner of ch0 and REFUSES to start while a Sound
# Blaster is hot, so on an SB-mode card both A/B arms come back identical and
# the key measurement silently measures nothing.
if [ "${QA_BUNDLE_OK}" = "1" ]; then
  _bat_bad=0
  for _f in "${QA_BUNDLE_DIR}"/*.BAT; do
    [ -e "${_f}" ] || continue
    _b=$(basename "${_f}")
    _cr=$(tr -dc '\r' < "${_f}" | wc -c); _lf=$(tr -dc '\n' < "${_f}" | wc -c)
    # Non-ASCII counted by DELETING the ASCII range: a [^\x00-\x7F] bracket
    # expression means different things to GNU grep, BSD grep and ugrep, and
    # this runs on the operator laptop rather than the build host.
    _na=$(LC_ALL=C tr -d '\000-\177' < "${_f}" | wc -c)
    if [ "${_cr}" -ne "${_lf}" ] || [ "${_na}" -ne 0 ]; then
      echo "  FAIL ${_b}: cr=${_cr} lf=${_lf} non-ascii=${_na} -- NOT copied"; _bat_bad=1
    else
      cp -f "${_f}" "${CF_GAME_DIR}/" && echo "  installed ${_b} (${_cr} lines, CRLF+ASCII clean)"
    fi
  done
  [ "${_bat_bad}" = "0" ] || echo "  WARNING: at least one BAT failed its gate and was skipped."
else
  echo "  SKIP: no bundle -- card keeps its existing BATs"
fi

echo "[6/8] ensure + CLEAR CF LOGS (campaign accumulates CPU-tagged logs here)"
mkdir -p "${CF_LOGS}"
# Keep anything already there (e.g. a pre-fix ENV PROBE that witnessed a stale
# master-env leak) -- it is evidence, and clearing is not worth losing it.
if ls "${CF_LOGS}"/* >/dev/null 2>&1; then
  keep="${STAGING}/logs-before-$(date +%Y%m%d-%H%M%S)"
  mkdir -p "${keep}"; cp "${CF_LOGS}"/* "${keep}/" 2>/dev/null || true
  echo "  NOTE: existing CF logs archived -> ${keep}"
fi
rm -f "${CF_LOGS}/"*.LOG "${CF_LOGS}/"*.TXT "${CF_LOGS}/"*.CFG 2>/dev/null || true
echo "  PASS: ${CF_LOGS} present + cleared"

echo "[7/8] AUTOEXEC audit -- flag stale audio SETs (LOUD; never edited)"
AE=""
for cand in "${CF_MOUNT}/AUTOEXEC.BAT" "${CF_MOUNT}/autoexec.bat"; do
  [ -f "$cand" ] && AE="$cand" && break
done
if [ -n "$AE" ]; then
  hits=$(grep -inE '^[[:space:]]*SET[[:space:]]+(SDL_HINT_DOSKUTSU_|DOSKUTSU_)' "$AE" 2>/dev/null || true)
  if [ -n "$hits" ]; then
    echo "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "  !! STALE DOSKUTSU_* SET(s) in ${AE}:"
    echo "$hits" | sed 's/^/  !!   /'
    echo "  !! These beat CFG values (env > file) and can MASK a cell's witness"
    echo "  !! (e.g. a v1.4.x 'SET ...MIDI_SOURCE=wiimidi' line silently pins the"
    echo "  !! MIDI set). NOT edited automatically -- your call. If a cell's audio"
    echo "  !! reads wrong, REM these out + reboot before that cell."
    echo "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  else
    echo "  PASS: no DOSKUTSU_* / SDL_HINT_DOSKUTSU_* SET in ${AE}"
  fi
  bl=$(grep -inE '^[[:space:]]*SET[[:space:]]+BLASTER' "$AE" 2>/dev/null || true)
  [ -n "$bl" ] && echo "  note: BLASTER line (expected -- hardware default): ${bl}"
else
  echo "  note: no AUTOEXEC.BAT at CF root (${CF_MOUNT}) -- nothing to audit"
fi

echo "[8/8] verify BAT kit CRLF + ASCII (pack-gate backstop) + sync + unmount"
badbat=0
for b in "${CF_GAME_DIR}"/*.BAT; do
  file "$b" | grep -q "CRLF line terminators" || { echo "  FAIL: $(basename "$b") not CRLF"; badbat=1; }
  file "$b" | grep -q "UTF-8" && { echo "  FAIL: $(basename "$b") has UTF-8"; badbat=1; }
done
[ $badbat -eq 0 ] && echo "  PASS: all $(ls "${CF_GAME_DIR}"/*.BAT | wc -l) BATs CRLF + ASCII" || { echo "  ABORT: BAT gate failed"; exit 1; }
sync
echo "  sync done"
if mountpoint -q "${CF_MOUNT}"; then
  dev=$(findmnt -no SOURCE "${CF_MOUNT}" 2>/dev/null || true)
  if [ -n "$dev" ] && udisksctl unmount -b "$dev" 2>/dev/null; then echo "  PASS: unmounted via udisksctl"
  elif sudo -n umount "${CF_MOUNT}" 2>/dev/null; then echo "  PASS: unmounted via sudo umount"
  elif umount "${CF_MOUNT}" 2>/dev/null; then echo "  PASS: unmounted via umount"
  else
    echo "  WARN: auto-unmount failed; unmount manually before moving CF:"
    echo "    udisksctl unmount -b ${dev:-${CF_MOUNT}}"
  fi
else
  echo "  CF already unmounted"
fi

cat <<'EOF'

=== CF POPULATED. Move it to the test PC and boot to DOS. ===

ON THE TEST PC (CF is C:) -- the laptop wizard drives every step:

  On the laptop:  bash ~/qa-v163/qa-feedback.sh     (or: scp claude:/tmp/qa-feedback.sh ~/qa-v163/ first)

  On the DOS box (what the wizard tells you to type):
    C:
    CD \DOSKUTSU
    QA            <- pick your CPU once per boot. The menu still lists four,
                     but THIS campaign uses only [3] DX2-66 (phase 1) and
                     [1] POD-83 (phases 2 and 3).
    <cell>        <- run each cell BY NAME as the wizard dictates (e.g. G18, C3)

  SCOPE (reduced 2026-07-26, re-ordered 2026-08-05): THREE bench phases, S3
  ViRGE resident throughout, card-major so the PicoGUS goes in once and comes
  out once:
    PHASE 1  486DX2-66 + ViRGE + PicoGUS   QA pick [3], logs tagged 6..
    PHASE 2  POD-83    + ViRGE + PicoGUS   QA pick [1], logs tagged G..
    PHASE 3  POD-83    + ViRGE + Vibra16   QA pick [1], logs tagged G..
  Phases 1 and 2 run the SAME cells with only the CPU changed -- that is what
  makes the fps anchor pair 6C4 vs GC4 comparable. Cell BATs are CPU-agnostic
  (the tag is the QA.BAT pick + the cell number), so C4 on the POD-83 writes
  GC4.LOG and G22 on the 486 writes 622.LOG.
  Am5x86-133, DX2-50 and the Cirrus / Mach64 cards are OUT OF SCOPE. SEVEN
  extra cell BATs stay on the CF as optional extras: G25, G52, C6, and the
  Vibra-only G19 G110 G113 G114 (save those for phase 3). CHECKLST.TXT lists
  them; the wizard does not walk them. Run any ad hoc if you have bench time.
  PicoGUS modes: run PGUSGUS / PGUSSB / PGUSADL (each opens its mode; EXIT to leave it).
  The wizard captures your o/i/s verdict after every cell -- no typing into Claude.

Logs accumulate CPU-tagged in C:\DOSKUTSU\LOGS across ALL THREE phases.
Do NOT clear LOGS between phases -- the logback pulls the whole set at the end.

=== AFTER THE LAST PHASE, re-insert CF in the laptop and run ONE command: ===

  scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh

EOF
