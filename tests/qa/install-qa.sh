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
TARBALL="doskutsu-cf-2026-08-12-qa-r2-fe44805fb603.tar.gz"

EXP_DOSKUTSU_SHA="fe44805fb60375f7c773c92fa28e1164739f0f2391fcc81e9232e40b95ac7898"
EXP_SETUP_SHA="723d6991b30308083daadcc8b35ca972cff1bb3604e7fec3f4628b2c0acb9ba3"
EXP_SETUPBAT_SHA="ee9140aac514abe1d6eaca9d3c08817f1599201f59916b145c904b5c3ed18741"
EXP_CWSDPMI_SHA="2de899fecaa90632b8b9bdfc0305cb0375e59ae252c37e32d06c1ed3f98a8f44"
EXP_TAS_SHA="4118561edf26b93ab9b7a50e894e04ba7b0e7f089c6aa6418afcc8340ebf0bf1"  # QA.TAS ROUND-1 BENCHMARK reel (1956 B, ~5100 ticks/102 s)
# The payload keeps its filename across repacks, so the staged copy is checked
# by content. Bump this whenever the tarball is rebuilt.
EXP_TARBALL_SHA="d7d1d01dbbd9b4e14a1540eca8e65650924edec603d9be8fb8f748c7beaca206"

# Overridable ONLY so the script can be exercised end to end without a card --
# every default is unchanged, so the operator command line is identical. Two of
# three field failures were in the gap between "the source is right" and "the
# artifact the operator fetches is right", and that gap existed because this
# script could not be run at all without the hardware.
CF_MOUNT="${CF_MOUNT:-/media/micheal/DOS}"
CF_GAME_DIR="${CF_MOUNT}/doskutsu"
CF_LOGS="${CF_GAME_DIR}/LOGS"
STAGING="${STAGING:-/home/micheal/Projects/gateway2000/doskutsu}"
DRY_RUN="${DRY_RUN:-0}"   # 1 = skip the payload fetch and the final unmount

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
# Cache on CONTENT, not on filename. The payload gets repacked in place -- new
# saves, a different reel -- while keeping its name, so "a file by that name is
# already here" accepted a stale copy indefinitely. That silently installed the
# previous payload and every downstream PASS still read green.
if [ -f "${STAGING}/${TARBALL}" ]; then
  _tsha=$(sha256sum "${STAGING}/${TARBALL}" | cut -d' ' -f1)
  if [ "${_tsha}" = "${EXP_TARBALL_SHA}" ]; then
    echo "  already staged and current (sha ${_tsha:0:12}) -- not re-fetching 188 MB"
  else
    echo "  STALE payload staged (sha ${_tsha:0:12}, want ${EXP_TARBALL_SHA:0:12}) -- re-fetching"
    if [ "${DRY_RUN}" = "1" ]; then
      echo "  FAIL: DRY_RUN=1 but the staged payload is stale"; exit 1
    fi
    mv -f "${STAGING}/${TARBALL}" "${STAGING}/${TARBALL}.stale" 2>/dev/null
    scp "claude:/tmp/${TARBALL}" "${STAGING}/"
  fi
elif [ "${DRY_RUN}" = "1" ]; then
  echo "  FAIL: DRY_RUN=1 but ${STAGING}/${TARBALL} is not staged"; exit 1
else
  scp "claude:/tmp/${TARBALL}" "${STAGING}/"
fi
_tsha=$(sha256sum "${STAGING}/${TARBALL}" | cut -d' ' -f1)
if [ "${_tsha}" != "${EXP_TARBALL_SHA}" ]; then
  echo "  FAIL: payload sha ${_tsha:0:12}, expected ${EXP_TARBALL_SHA:0:12}"
  echo "        Everything below this point would install the wrong payload."
  exit 1
fi
echo "  PASS: ${STAGING}/${TARBALL} (sha ${_tsha:0:12})"

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
    echo "  NON-BENCHMARK REEL on CF (${cursz} B, sha ${cursha:0:12}) -- backing it up"
    echo "  backup also kept at ${OPTAKE}"
  fi
fi
# Same hazard, same shape, for the benchmark saves -- but "the card always wins"
# is too blunt. It protects an operator's own progress, and it also pins a stale
# benchmark pair forever, so a payload that ships corrected saves can never
# deliver them. Decide by what the card's saves ARE:
#
#   already benchmark state (map 20 + a weapon) -> keep the card's, no churn
#   anything else                               -> shipped pair wins
#
# Either way the card's copy is backed up first, timestamped, so the choice is
# always reversible. KEEP_SAVES=1 forces the card's saves to win regardless.
_isbench() {  # map 20 with a non-zero weapon in the first slot
  local f="$1" st wp
  st=$(od -An -tu4 -j8  -N4 "$f" 2>/dev/null | tr -d ' ')
  wp=$(od -An -tu4 -j56 -N4 "$f" 2>/dev/null | tr -d ' ')
  [ "${st:-x}" = "20" ] && [ "${wp:-0}" != "0" ]
}
SAVESTASH=""; SAVEKEEP=0; _cardbench=1; _cardn=0
for _f in "${CF_GAME_DIR}"/[Pp][Rr][Oo][Ff][Ii][Ll][Ee]*.[Dd][Aa][Tt]; do
  [ -e "$_f" ] || continue
  if [ -z "${SAVESTASH}" ]; then
    SAVESTASH=$(mktemp -d "${TMPDIR:-/tmp}/qa-savestash.XXXXXX")
    _pre="${STAGING}/saves-backup/$(date -u +%Y%m%dT%H%M%SZ)-pre-extract"
    mkdir -p "${_pre}"
  fi
  cp -p "$_f" "${SAVESTASH}/"; cp -p "$_f" "${_pre}/"
  _cardn=$((_cardn+1))
  _isbench "$_f" || _cardbench=0
done
if [ -n "${SAVESTASH}" ]; then
  echo "  ${_cardn} save(s) already on CF -- backed up to ${_pre}/"
  if [ "${KEEP_SAVES:-0}" = "1" ]; then
    SAVEKEEP=1; echo "  KEEP_SAVES=1 -- the CF's saves will be kept"
  elif [ "${_cardbench}" = "1" ]; then
    SAVEKEEP=1; echo "  they are already benchmark state (map 20 + weapon) -- keeping them"
  else
    echo "  they are NOT benchmark state -- the shipped pair will replace them"
  fi
fi
# Extract everything EXCEPT the big Organya-HQ (22050 stereo) cache -- that is
# gated on CF free space in step [5] so a tight card never ENOSPCs mid-extract.
tar -xzf "${STAGING}/${TARBALL}" -C "${CF_MOUNT}/" \
    --exclude='DOSKUTSU/CACHE/22050_2' --exclude='DOSKUTSU/CACHE/22050_2/*'
echo "  PASS: extracted DOSKUTSU/ into ${CF_GAME_DIR} (HQ cache deferred)"
if [ -n "${OPTAKE}" ]; then
  cp "${OPTAKE}" "${CF_GAME_DIR}/QA.TAS"
  echo "  restored it post-extract; step [5d] decides which reel ships"
fi
if [ -n "${SAVESTASH}" ] && [ "${SAVEKEEP}" = "1" ]; then
  cp -p "${SAVESTASH}"/* "${CF_GAME_DIR}/" 2>/dev/null
  echo "  restored the CF's own saves over the shipped pair ($(ls -1 "${SAVESTASH}" | wc -l) file(s))"
  rm -rf "${SAVESTASH}"
else
  [ -n "${SAVESTASH}" ] && rm -rf "${SAVESTASH}"
  # Assert it, do not announce it. This line previously claimed an install it
  # had not checked, and read green while the payload carried no saves at all.
  _got=0
  for _f in "${CF_GAME_DIR}"/[Pp][Rr][Oo][Ff][Ii][Ll][Ee][35].[Dd][Aa][Tt]; do
    [ -e "$_f" ] && _got=$((_got+1))
  done
  if [ "${_got}" -eq 2 ]; then
    echo "  shipped benchmark pair installed (2 files)"
  else
    echo "  FAIL: the payload did not deliver PROFILE3/5.DAT (found ${_got}/2)."
    echo "        The reel loads a save and enters map 20; without it every cell"
    echo "        replays a different run and the round is void."
    exit 1
  fi
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
  echo "  PASS: QA.TAS = benchmark reel (${tassz} B)"
elif [ "${tassz}" -gt 100 ]; then
  echo "  NOTE: QA.TAS is not the benchmark reel yet (${tassz} B, sha ${tassha:0:12}); step [5d] handles it"
else
  echo "  FAIL: QA.TAS is ${tassz} B -- too small to be a real reel"; exit 1
fi

echo "[5/8] cache-key + kit presence sanity"
if [ -f "${CF_GAME_DIR}/CACHE/11025_1/READY.OK" ]; then
  key=$(cat "${CF_GAME_DIR}/CACHE/11025_1/READY.OK" 2>/dev/null | tr -d '\r\n')
  if [ "$key" = "1f79ce20e4ee" ]; then
    echo "  PASS: Organya 11025 cache keyed 1f79ce20e4ee (hits on aef02e5c)"
  else
    echo "  WARN: cache key '${key}' != 1f79ce20e4ee -- Organya cells may cold-render"
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

echo "[5a/8] back up save files before any purge"
# The benchmark reel opens Load Game and reads profile3.dat / profile5.dat.
# Deleting those desyncs the whole route, and the purge below once listed them
# by name. Copy anything save-shaped aside first, unconditionally, so a purge
# bug can never be unrecoverable again.
#
# Each run gets its OWN directory. A fixed destination is only one generation
# deep: the second populate overwrites the backup taken by the first, so the
# copy of the save you actually want is gone exactly when you reach for it.
_sv=0
_bdir="${STAGING}/saves-backup/$(date -u +%Y%m%dT%H%M%SZ)"
for _f in "${CF_GAME_DIR}"/[Pp][Rr][Oo][Ff][Ii][Ll][Ee]*.[Dd][Aa][Tt]; do
  [ -e "$_f" ] || continue
  mkdir -p "${_bdir}"
  cp -p "$_f" "${_bdir}/" && _sv=$((_sv+1))
done
if [ "$_sv" -gt 0 ]; then
  echo "  backed up ${_sv} save file(s) -> ${_bdir}/"
else
  echo "  WARNING: no profile*.dat on the CF."
  echo "           The benchmark reel loads a SAVE (profile3/profile5) and enters"
  echo "           map 20. Without them the reel desyncs and every cell is void."
fi
# Present is not the same as correct. A save the game wrote during ordinary play
# is at whatever map the player stood on, and the reel replays against it just as
# happily -- producing a full, normal-looking log of the wrong run. The stage
# field is bytes 8..11 (LE) right after the 'Do041220' magic.
_ok20=0
for _f in "${CF_GAME_DIR}"/[Pp][Rr][Oo][Ff][Ii][Ll][Ee][35].[Dd][Aa][Tt]; do
  [ -e "$_f" ] || continue
  _st=$(od -An -tu4 -j8 -N4 "$_f" 2>/dev/null | tr -d ' ')
  # Weapon list starts at 0x38: id, level, xp, maxammo, ammo. The reel presses
  # fire, so a save with no weapon replays the same route drawing no bullets --
  # less work per frame, and a frame rate that reads better than round 1 for a
  # reason that has nothing to do with the binary.
  _wp=$(od -An -tu4 -j56 -N4 "$_f" 2>/dev/null | tr -d ' ')
  if [ "${_st:-x}" = "20" ] && [ "${_wp:-0}" != "0" ]; then
    echo "  ${_f##*/}: map 20 'Save Point', weapon ${_wp} -- benchmark state"
    _ok20=1
  elif [ "${_st:-x}" = "20" ]; then
    echo "  ${_f##*/}: map 20 but NO WEAPON -- the reel will fire nothing"
  else
    echo "  ${_f##*/}: map ${_st:-?} -- NOT the benchmark state"
  fi
done
if [ "$_sv" -gt 0 ] && [ "$_ok20" -eq 0 ]; then
  echo "  WARNING: the saves on this CF are not benchmark state."
  echo "           The reel will replay against them, the route will not be"
  echo "           72 20 11 17 11 15 11 19 11 14 11, and the round is void."
  if [ "${KEEP_SAVES:-0}" = "1" ]; then
    echo "           KEEP_SAVES=1 kept them deliberately. Re-run without it to"
    echo "           install the shipped benchmark pair."
  else
    echo "           This should not happen -- step [3/8] replaces non-benchmark"
    echo "           saves with the shipped pair. Re-run; if it persists, the"
    echo "           payload's saves are wrong, not the card's."
  fi
fi

echo "[5b/8] purge accumulated iter debris (superseded binaries, retired probes)"
# ~141 entries that no current sweep references: superseded game binaries at
# ~7 MB each, one-off probe EXEs, retired reels, and closed campaigns' BAT
# cells. The list is DERIVED -- present on the card, absent from the kit
# tarball -- minus an explicit keep-list. Embedded rather than fetched so this
# script works from scratch with no companion downloads.
# KEPT ON PURPOSE: LICENSE/GPLV3/3RDPARTY/README, because the shipped binary
# is GPLv3 and its licence text belongs beside it; DOSKUTSU.CFG/SETTINGS.DAT,
# which are runtime state the sweeps rewrite.
QA_STALE="
  ADLB.BAT
  ADLIB
  ADLIB.BAT
  BEEP.BAT
  CAVEA.BAT
  CAVE.BAT
  CAVEB.BAT
  CFG
  CFGTEMPO.CFG
  CFGWB.CFG
  CORE.BAT
  DK281.EXE
  DK282.EXE
  DK283.EXE
  DKSTOCK.EXE
  DKWBWIN.EXE
  DMAB.BAT
  DMAC.BAT
  DMAK.BAT
  DOSKUT_O.EXE
  DOSKUTSU.BAK
  DPAD.BAT
  ESCNOTE.BAT
  FBOTH.BAT
  FCANON.BAT
  FMUL.BAT
  FOFF.BAT
  FREL.BAT
  FULL
  FXOFF.BAT
  FXON.BAT
  G4428.BAT
  G4432.BAT
  GMUI.BAT
  GOLD.EXE
  GUS
  GUS17.TXT
  GUS5A.BAT
  GUS68.BAT
  GUS6D.BAT
  GUS7N.BAT
  GUS8B0.BAT
  GUS8B.BAT
  GUS8C.BAT
  GUSAB.BAT
  GUS.BAT
  GUSDET.BAT
  GUSDET.EXE
  GUSDMA.BAT
  GUSDUMP.BAT
  GUSDUMP.EXE
  GUSG1.BAT
  GUSG2.BAT
  GUSIN44.BAT
  GUSINIT.BAT
  GUSM.BAT
  GUSNAME.BAT
  GUSPROBE.TXT
  GUSRSV.BAT
  GUSS.BAT
  GUSSFX.BAT
  GUSSFX.EXE
  GUSTEST.BAT
  GUSTONE.BAT
  GUSTONE.EXE
  GV14.BAT
  GV32.BAT
  MDEMO.BAT
  MFIX.BAT
  MIDIO.BAT
  MIDIW.BAT
  MOFF.BAT
  OPNOTES.TXT
  ORG.BAT
  ORGD0.BAT
  ORGD1.BAT
  ORGPRB.BAT
  ORGRV.BAT
  PCSPK.BAT
  PCSPKPWM.BAT
  PCSPKPWM.EXE
  PCTONE.BAT
  PLAY0.BAT
  PLAY10.BAT
  PLAY11.BAT
  _PLAY1.BAT
  PLAY1.BAT
  _PLAY2.BAT
  PLAY2.BAT
  _PLAY3.BAT
  PLAY3.BAT
  _PLAY4.BAT
  PLAY4.BAT
  PLAY5.BAT
  PLAY6.BAT
  PLAY7.BAT
  PLAY8.BAT
  PLAY9.BAT
  PLAYC14.BAT
  PLAYC28.BAT
  PLAYG44.BAT
  PLAYGUS.BAT
  PRECACHE.BAT
  PSPK0.BAT
  QA281.TAS
  QA282.TAS
  READADL.TXT
  READV161.TXT
  READWBG.TXT
  REC281.BAT
  REEL
  RP283.BAT
  RT282.BAT
  RUN
  RUN.BAT
  SB
  SCRA.BAT
  SCRB.BAT
  SDESC.BAT
  SETA.BAT
  SETGUS.BAT
  SETSB.BAT
  SETV.BAT
  TAS0.BAT
  TAS1.BAT
  TONE16.BAT
  TONE8.BAT
  TONEFIX.BAT
  V14.BAT
  V16.BAT
  V20.BAT
  V24.BAT
  V27.BAT
  V28.BAT
  V29.BAT
  V32.BAT
  WB.BAT
  WBHOT.EXE
  WGUS18.TXT
"
_n=0; _kb=0
for _f in ${QA_STALE}; do
  _p="${CF_GAME_DIR}/${_f}"
  [ -e "${_p}" ] || continue
  _sz=$(du -sk "${_p}" 2>/dev/null | cut -f1); _sz=${_sz:-0}
  _kb=$((_kb + _sz)); _n=$((_n + 1))
  rm -rf -- "${_p}"
done
echo "  removed ${_n} stale entries, freed $((_kb / 1024)) MB"
echo "  kept: kit files + LICENSE/GPLV3/3RDPARTY/README + DOSKUTSU.CFG/SETTINGS.DAT"

echo "[5c/8] install the round-2 QA BATs (PROVE / RB / GAP / EAR)"
# Embedded base64 so the bytes are exact: these are CRLF files and DOS 6.22
# will not run an LF-only BAT. Each sweep sets its own PicoGUS mode, because
# the PIT music pump is the single owner of ch0 and refuses to start while a
# Sound Blaster is hot -- an AdLib cell on an SB-mode card measures nothing,
# and in EAR's case plays silence to an operator judging audio quality.
_bat_write() {
  _name="$1"; _b64="$2"
  printf '%s' "${_b64}" | base64 -d > "${CF_GAME_DIR}/${_name}" 2>/dev/null || {
    echo "  FAIL ${_name}: base64 decode failed"; return 1; }
  _cr=$(tr -dc '\r' < "${CF_GAME_DIR}/${_name}" | wc -c)
  _lf=$(tr -dc '\n' < "${CF_GAME_DIR}/${_name}" | wc -c)
  # Non-ASCII by DELETING the ASCII range: a [^\x00-\x7F] bracket expression
  # is read differently by GNU grep, BSD grep and ugrep, and this runs on the
  # operator laptop rather than the build host.
  _na=$(LC_ALL=C tr -d '\000-\177' < "${CF_GAME_DIR}/${_name}" | wc -c)
  if [ "${_cr}" -ne "${_lf}" ] || [ "${_na}" -ne 0 ]; then
    echo "  FAIL ${_name}: cr=${_cr} lf=${_lf} non-ascii=${_na}"; return 1
  fi
  echo "  installed ${_name} (${_cr} lines, CRLF + ASCII clean)"
}

_B64_PROVE='
QEVDSE8gT0ZGDQpJRiAiJVFBTSUiPT0iIiBHT1RPIE5PTUFDSA0KSUYgIiUxIj09IkdPIiBHT1RP
IEdPDQpDT01NQU5EIC9FOjIwNDggL0MgJTAgR08NCkdPVE8gRU5EDQo6Tk9NQUNIDQpFQ0hPIEVS
Uk9SOiBtYWNoaW5lIG5vdCBzZXQuIFJ1biBRQS5CQVQgZmlyc3QgKHBpY2sgeW91ciBDUFUpLCB0
aGVuIHJlLXJ1bi4NCkdPVE8gRU5EDQo6R08NClJFTSAtLS0gUUEtSU5TVFJVTUVOVCB2MSAoc2Ft
ZSBiYW5uZXIgKyAuTkZPIGNvbnRyYWN0IGFzIFBHL1ZCKSAtLS0NCklGICIlUUFNJSI9PSJHIiBT
RVQgUUFDUFU9UGVudGl1bSBPdmVyRHJpdmUgODMNCklGICIlUUFNJSI9PSJBIiBTRVQgUUFDUFU9
QW01eDg2LTEzMw0KSUYgIiVRQU0lIj09IjYiIFNFVCBRQUNQVT00ODZEWDItNjYNCklGICIlUUFN
JSI9PSI1IiBTRVQgUUFDUFU9NDg2RFgyLTUwDQpJRiBOT1QgRVhJU1QgTE9HU1xOVUwgTUtESVIg
TE9HUw0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT0NCkVDSE8gIFNXRUVQIDogUFJPVkUtT1VUIC0tIHZhbGlkYXRlcyB0aGUg
cm91bmQtMiBiaW5hcnkgb24gcmVhbCBzaWxpY29uDQpFQ0hPICBDRUxMUyA6IDYgY2VsbHMsIH4x
NSBtaW4sIGZ1bGx5IHVuYXR0ZW5kZWQNCkVDSE8gIENQVSAgIDogJVFBQ1BVJQ0KRUNITyAgU09V
TkQgOiBQaWNvR1VTIFJFUVVJUkVEIC0tIHRoaXMgc3dlZXAgc3dpdGNoZXMgaXQgc2IgLT4gYWRs
aWINCkVDSE8gIFZJREVPIDogd2hpY2hldmVyIGNhcmQgaXMgaW5zdGFsbGVkIC0tIHJ1biBvbmNl
IHBlciBjYXJkDQpFQ0hPICBMT0dTICA6IHRhZ2dlZCAlUUFNJVAuLiAgaW4gTE9HU1wNCkVDSE8g
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09DQpFQ0hPICBQNSB2cyBQNUsgaXMgdGhlIEtFWSBwYWlyOiBpdCBBL0JzIHRoZSB0aW1lYmFz
ZSBmaXggb24gcmVhbA0KRUNITyAgaGFyZHdhcmUsIHRoZSBvbmUgdGhpbmcgRE9TQm94IHN0cnVj
dHVyYWxseSBjYW5ub3QgdGVzdC4NCkVDSE8gIFRoZSBBZExpYiBjZWxscyBORUVEIC9tb2RlIGFk
bGliIC0tIHdpdGggYW4gU0IgaG90IHRoZSBtdXNpYw0KRUNITyAgcHVtcCByZWZ1c2VzIHRvIHN0
YXJ0IGFuZCB0aGUgQS9CIHdvdWxkIG1lYXN1cmUgbm90aGluZy4NCkVDSE8gPT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPIERP
U0tVVFNVIFFBIGxhbmUgbWFuaWZlc3QgPiBMT0dTXCVRQU0lUFIuTkZPDQpFQ0hPIHN3ZWVwPVBS
T1ZFID4+IExPR1NcJVFBTSVQUi5ORk8NCkVDSE8gY2VsbHM9NiA+PiBMT0dTXCVRQU0lUFIuTkZP
DQpFQ0hPIGNwdT0lUUFDUFUlID4+IExPR1NcJVFBTSVQUi5ORk8NCkVDSE8gbG9nX3RhZ19wcmVm
aXg9JVFBTSUgPj4gTE9HU1wlUUFNJVBSLk5GTw0KRUNITyBzb3VuZD1QaWNvR1VTIChzYiBmb3Ig
UDQvUDRCL1AzL1AwLCBhZGxpYiBmb3IgUDUvUDVLKSA+PiBMT0dTXCVRQU0lUFIuTkZPDQpFQ0hP
IHZpZGVvPXJ1biBvbmNlIHBlciBjYXJkID4+IExPR1NcJVFBTSVQUi5ORk8NCkVDSE8gbm90ZT1y
dW4gb3JkZXIgaXMgcmVjb3ZlcmFibGUgZnJvbSB0aGUgW0hIOk1NOlNTXSBwcmVmaXhlcyBpbiB0
aGUgY2VsbCBsb2dzID4+IExPR1NcJVFBTSVQUi5ORk8NClNFVCBTRExfSElOVF9ET1NLVVRTVV9C
QU5ORVJfREVMQVlfTVM9MTAwMDANClBBVVNFDQpFQ0hPLg0KRUNITyAjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjDQpFQ0hPICBzd2l0Y2hp
bmcgUGljb0dVUyAtPiBTQiBNT0RFICAoUDQgUDRCIFAzIFAwIG5lZWQgYSBsaXZlIFNCIERBQykN
CkVDSE8gIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIw0KcGd1c2luaXQgL21vZGUgc2INCnBndXNpbml0IC9zYmVudg0KU0VUIEJMQVNURVI9
QTIyMCBJNyBEMyBQMzMwIFQzDQpDQUxMIENMUkVOVg0KU0VUIFNETF9ISU5UX0RPU0tVVFNVX1BV
TVBfVElNRUJBU0U9DQpJRiBFWElTVCBQUk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkg
Q0ZHU1xPUEwzLkNGRyBET1NLVVRTVS5DRkcgPiBOVUwNClNFVCBET1NLVVRTVV9MT0dfVEFHPSVR
QU0lUDQNClNFVCBET1NLVVRTVV9UQVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNVX1RBU19Q
Uk5HX1NFRUQ9MTIzNDUNClNFVCBET1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAwMDANCkVD
SE8uDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpF
Q0hPICBbMS82XSBDRUxMIFA0ICAgdGFnICVRQU0lUDQNCkVDSE8gIE9QTDMgLS0gdGhlIGNvbnRy
b2wNCkVDSE8gIE5vbi1wdW1wLiBNdXN0IG1hdGNoIHRoZSBiYW5rZWQgQzQgYW5jaG9yIGZvciB0
aGlzIENQVS4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT0NCkRPU0tVVFNVLkVYRQ0KQ0FMTCBDTFJFTlYNClNFVCBTRExfSElOVF9ET1NLVVRTVV9QVU1Q
X1RJTUVCQVNFPQ0KSUYgRVhJU1QgUFJPRklMRS5EQVQgREVMIFBST0ZJTEUuREFUDQpDT1BZIENG
R1NcT1BMMy5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQgRE9TS1VUU1VfTE9HX1RBRz0lUUFN
JVA0Qg0KU0VUIERPU0tVVFNVX1RBU19SRVBMQVk9UUEuVEFTDQpTRVQgRE9TS1VUU1VfVEFTX1BS
TkdfU0VFRD0xMjM0NQ0KU0VUIERPU0tVVFNVX1RBU19BVVRPX0VYSVRfVElDSz0zMDAwMA0KRUNI
Ty4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkVD
SE8gIFsyLzZdIENFTEwgUDRCICAgdGFnICVRQU0lUDRCDQpFQ0hPICBPUEwzIHJlcGVhdCAtLSB0
aGUgbm9pc2UgZmxvb3INCkVDSE8gIFA0IHZzIFA0QiBzcHJlYWQgSVMgdGhlIG5vaXNlIGZsb29y
Lg0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRE9T
S1VUU1UuRVhFDQpDQUxMIENMUkVOVg0KU0VUIFNETF9ISU5UX0RPU0tVVFNVX1BVTVBfVElNRUJB
U0U9DQpJRiBFWElTVCBQUk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xPUkdB
TllBLkNGRyBET1NLVVRTVS5DRkcgPiBOVUwNClNFVCBET1NLVVRTVV9MT0dfVEFHPSVRQU0lUDMN
ClNFVCBET1NLVVRTVV9UQVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNVX1RBU19QUk5HX1NF
RUQ9MTIzNDUNClNFVCBET1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAwMDANCkVDSE8uDQpF
Q0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPICBb
My82XSBDRUxMIFAzICAgdGFnICVRQU0lUDMNCkVDSE8gIE9yZ2FueWEgLS0gdGhlIHJlLXJlbmRl
cmVkIFBDTSBjYWNoZQ0KRUNITyAgQ2F0Y2hlcyBhIHNpbGVudGx5IGRpZmZlcmVudCByZS1yZW5k
ZXIuIE1hdGNoIGJhbmtlZCBDMy4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT0NCkRPU0tVVFNVLkVYRQ0KQ0FMTCBDTFJFTlYNClNFVCBTRExfSElOVF9E
T1NLVVRTVV9QVU1QX1RJTUVCQVNFPQ0KSUYgRVhJU1QgUFJPRklMRS5EQVQgREVMIFBST0ZJTEUu
REFUDQpDT1BZIENGR1NcT1BMMy5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQgU0RMX0hJTlRf
RE9TS1VUU1VfQVVESU9fT0ZGPTENClNFVCBET1NLVVRTVV9MT0dfVEFHPSVRQU0lUDANClNFVCBE
T1NLVVRTVV9UQVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNVX1RBU19QUk5HX1NFRUQ9MTIz
NDUNClNFVCBET1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAwMDANCkVDSE8uDQpFQ0hPID09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPICBbNC82XSBD
RUxMIFAwICAgdGFnICVRQU0lUDANCkVDSE8gIFRSVUUgYXVkaW8gZmxvb3IgKEFVRElPX09GRj0x
KQ0KRUNITyAgRGV2aWNlLWxldmVsIG9mZi4gU0lMRU5ULkNGRyBpcyBOT1QgYSBmbG9vci4NCkVD
SE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkRPU0tVVFNV
LkVYRQ0KRUNITy4NCkVDSE8gIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIw0KRUNITyAgc3dpdGNoaW5nIFBpY29HVVMgLT4gQURMSUIgTU9E
RSAgKG5vIFNCLCBzbyB0aGUgUElUIHB1bXAgY2FuIG93biBjaDApDQpFQ0hPICMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMNCnBndXNpbml0
IC9tb2RlIGFkbGliDQpDQUxMIENMUkVOVg0KU0VUIFNETF9ISU5UX0RPU0tVVFNVX1BVTVBfVElN
RUJBU0U9DQpJRiBFWElTVCBQUk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xB
RExJQi5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQgRE9TS1VUU1VfTE9HX1RBRz0lUUFNJVA1
DQpTRVQgRE9TS1VUU1VfVEFTX1JFUExBWT1RQS5UQVMNClNFVCBET1NLVVRTVV9UQVNfUFJOR19T
RUVEPTEyMzQ1DQpTRVQgRE9TS1VUU1VfVEFTX0FVVE9fRVhJVF9USUNLPTMwMDAwDQpFQ0hPLg0K
RUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRUNITyAg
WzUvNl0gQ0VMTCBQNSAgIHRhZyAlUUFNJVA1DQpFQ0hPICBBZExpYiwgdGltZWJhc2UgZml4IE9O
IC0tIFRIRSBLRVkgQ0VMTA0KRUNITyAgRXhwZWN0IDEwLTQwbXMgYmFuZCBQT1BVTEFURUQsIHB1
bXBfY2xvY2tfc3RhdGU9cHVtcC10aW1lYmFzZS1vay4NCkVDSE8gPT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkRPU0tVVFNVLkVYRQ0KQ0FMTCBDTFJFTlYNClNF
VCBTRExfSElOVF9ET1NLVVRTVV9QVU1QX1RJTUVCQVNFPQ0KSUYgRVhJU1QgUFJPRklMRS5EQVQg
REVMIFBST0ZJTEUuREFUDQpDT1BZIENGR1NcQURMSUIuQ0ZHIERPU0tVVFNVLkNGRyA+IE5VTA0K
U0VUIFNETF9ISU5UX0RPU0tVVFNVX1BVTVBfVElNRUJBU0U9MA0KU0VUIERPU0tVVFNVX0xPR19U
QUc9JVFBTSVQNUsNClNFVCBET1NLVVRTVV9UQVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNV
X1RBU19QUk5HX1NFRUQ9MTIzNDUNClNFVCBET1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAw
MDANCkVDSE8uDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09DQpFQ0hPICBbNi82XSBDRUxMIFA1SyAgIHRhZyAlUUFNJVA1Sw0KRUNITyAgQWRMaWIsIHRp
bWViYXNlIGZpeCBPRkYgLS0gdGhlIEEvQiBhcm0NCkVDSE8gIEV4cGVjdCB0aGF0IGJhbmQgRVhB
Q1RMWSBFTVBUWS4gUHJvdmVzIHRoZSBsZXZlciBvbiByZWFsIHNpbGljb24uDQpFQ0hPID09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpET1NLVVRTVS5FWEUNCkVD
SE8uDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PQ0KRUNITyAgUFJPVkUtT1VUIERPTkUuIExvZ3MgJVFBTSVQNCAlUUFNJVA0
QiAlUUFNJVAzICVRQU0lUDAgJVFBTSVQNSAlUUFNJVA1Sw0KRUNITyAgVGhlIFBpY29HVVMgaXMg
bGVmdCBpbiBBRExJQiBtb2RlIC0tIHJlLXJ1biBwZ3VzaW5pdCBpZiB5b3UNCkVDSE8gIG5leHQg
d2FudCBzYiBvciBndXMuDQpFQ0hPICBWaWJyYSBjYXJkLXZzLURNQS1wYXRoIGNoZWNrIGlzIEdB
UC5CQVQgY2VsbCBYOCwgbm90IHRoaXMgc3dlZXAuDQpFQ0hPID09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KU0VUIFNETF9ISU5UX0RP
U0tVVFNVX1BVTVBfVElNRUJBU0U9DQpFQ0hPLg0KOkVORA0K
'
_bat_write PROVE.BAT "${_B64_PROVE}"

_B64_RB='
QEVDSE8gT0ZGDQpJRiAiJVFBTSUiPT0iIiBHT1RPIE5PTUFDSA0KSUYgIiUxIj09IkdPIiBHT1RP
IEdPDQpDT01NQU5EIC9FOjIwNDggL0MgJTAgR08NCkdPVE8gRU5EDQo6Tk9NQUNIDQpFQ0hPIEVS
Uk9SOiBtYWNoaW5lIG5vdCBzZXQuIFJ1biBRQS5CQVQgZmlyc3QgKHBpY2sgeW91ciBDUFUpLCB0
aGVuIHJlLXJ1bi4NCkdPVE8gRU5EDQo6R08NClJFTSAtLS0gcm91bmQtMiBndWFyZDogcmUtYmFz
ZWxpbmluZyByb3VuZCAxIGFnYWluc3Qgcm91bmQgMSBhZ3JlZXMNClJFTSAtLS0gcGVyZmVjdGx5
IGFuZCBwcm92ZXMgbm90aGluZy4gUk9VTkQyLk9LIGlzIHdyaXR0ZW4gYnkgdGhlDQpSRU0gLS0t
IGluc3RhbGxlciBPTkxZIHdoZW4gdGhlIGNhcmQgY2FycmllcyBhIG5vbi1yb3VuZC0xIGJpbmFy
eS4NCklGIEVYSVNUIFJPVU5EMi5PSyBHT1RPIFIyT0sNCkVDSE8gPT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPICBSQiBSRUZV
U0VTIFRPIFJVTg0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT0NCkVDSE8gIFRoaXMgY2FyZCBjYXJyaWVzIHRoZSBST1VORC0x
IGJpbmFyeS4gUmUtYmFzZWxpbmluZyBpdCBhZ2FpbnN0DQpFQ0hPICByb3VuZC0xIG51bWJlcnMg
YWdyZWVzIHBlcmZlY3RseSBhbmQgdGVzdHMgbm90aGluZy4NCkVDSE8uDQpFQ0hPICBTZWUgQklO
QVJZLk5GTyBvbiB0aGlzIGNhcmQgZm9yIHdoYXQgaXMgaW5zdGFsbGVkLg0KRUNITyAgUG9wdWxh
dGUgd2l0aCBhIHJvdW5kLTIgcGF5bG9hZCwgdGhlbiByZS1ydW4gUkIuDQpFQ0hPLg0KRUNITyAg
UFJPVkUgLyBHQVAgLyBFQVIgZG8gbm90IG5lZWQgcm91bmQgMiBhbmQgcnVuIGZpbmUgbm93Lg0K
RUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT0NCkdPVE8gRU5EDQo6UjJPSw0KUkVNIC0tLSBRQS1JTlNUUlVNRU5UIHYxIC0tLQ0K
SUYgIiVRQU0lIj09IkciIFNFVCBRQUNQVT1QZW50aXVtIE92ZXJEcml2ZSA4Mw0KSUYgIiVRQU0l
Ij09IkEiIFNFVCBRQUNQVT1BbTV4ODYtMTMzDQpJRiAiJVFBTSUiPT0iNiIgU0VUIFFBQ1BVPTQ4
NkRYMi02Ng0KSUYgIiVRQU0lIj09IjUiIFNFVCBRQUNQVT00ODZEWDItNTANCklGIE5PVCBFWElT
VCBMT0dTXE5VTCBNS0RJUiBMT0dTDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRUNITyAgU1dFRVAgOiBSRS1CQVNFTElO
RSBhbmNob3Igc2V0IChydW4gYWZ0ZXIgQU5ZIG5ldyBiaW5hcnkpDQpFQ0hPICBDRUxMUyA6IDQg
Y2VsbHMsIH4xMCBtaW4NCkVDSE8gIENQVSAgIDogJVFBQ1BVJQ0KRUNITyAgU09VTkQgOiBQaWNv
R1VTIFJFUVVJUkVEIC0tIHRoaXMgc3dlZXAgc3dpdGNoZXMgaXQgc2IgLT4gYWRsaWINCkVDSE8g
IFZJREVPIDogUzMgVmlSR0UgKG1hdGNoIHRoZSBiYW5rZWQgYW5jaG9ycykNCkVDSE8gIExPR1Mg
IDogdGFnZ2VkICVRQU0lLi4gIGluIExPR1NcDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRUNITyAgRWFjaCBjZWxsIGNv
dmVycyBhIERJRkZFUkVOVCB0aGluZyB0aGUgbmV3IGJpbmFyeSBjaGFuZ2VkLg0KRUNITyAgUjQv
UjRCIGFyZSBBREpBQ0VOVCBvbiBwdXJwb3NlOiB0aGF0IHNwcmVhZCBJUyB0aGUgbm9pc2UgZmxv
b3IuDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PQ0KRUNITyBET1NLVVRTVSBRQSBsYW5lIG1hbmlmZXN0ID4gTE9HU1wlUUFN
JVJCLk5GTw0KRUNITyBzd2VlcD1SQiA+PiBMT0dTXCVRQU0lUkIuTkZPDQpFQ0hPIGNlbGxzPTQg
Pj4gTE9HU1wlUUFNJVJCLk5GTw0KRUNITyBjcHU9JVFBQ1BVJSA+PiBMT0dTXCVRQU0lUkIuTkZP
DQpFQ0hPIGxvZ190YWdfcHJlZml4PSVRQU0lID4+IExPR1NcJVFBTSVSQi5ORk8NCkVDSE8gc291
bmQ9UGljb0dVUyBSRVFVSVJFRCAtLSB0aGlzIHN3ZWVwIHN3aXRjaGVzIGl0IHNiIC0+IGFkbGli
ID4+IExPR1NcJVFBTSVSQi5ORk8NCkVDSE8gdmlkZW89UzMgVmlSR0UgKG1hdGNoIHRoZSBiYW5r
ZWQgYW5jaG9ycykgPj4gTE9HU1wlUUFNJVJCLk5GTw0KRUNITyBub3RlPXJ1biBvcmRlciBpcyBy
ZWNvdmVyYWJsZSBmcm9tIHRoZSBbSEg6TU06U1NdIHByZWZpeGVzIGluIHRoZSBjZWxsIGxvZ3Mg
Pj4gTE9HU1wlUUFNJVJCLk5GTw0KU0VUIFNETF9ISU5UX0RPU0tVVFNVX0JBTk5FUl9ERUxBWV9N
Uz0xMDAwMA0KUEFVU0UNCkVDSE8uDQpFQ0hPICMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMNCkVDSE8gIHN3aXRjaGluZyBQaWNvR1VTIC0+
IFNCIE1PREUgKGNlbGxzIG5lZWRpbmcgYSBsaXZlIFNCIERBQykNCkVDSE8gIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIw0KcGd1c2luaXQg
L21vZGUgc2INCnBndXNpbml0IC9zYmVudg0KU0VUIEJMQVNURVI9QTIyMCBJNyBEMyBQMzMwIFQz
DQpDQUxMIENMUkVOVg0KSUYgRVhJU1QgUFJPRklMRS5EQVQgREVMIFBST0ZJTEUuREFUDQpDT1BZ
IENGR1NcT1BMMy5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQgRE9TS1VUU1VfTE9HX1RBRz0l
UUFNJVI0DQpTRVQgRE9TS1VUU1VfVEFTX1JFUExBWT1RQS5UQVMNClNFVCBET1NLVVRTVV9UQVNf
UFJOR19TRUVEPTEyMzQ1DQpTRVQgRE9TS1VUU1VfVEFTX0FVVE9fRVhJVF9USUNLPTMwMDAwDQpF
Q0hPLg0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0K
RUNITyAgWzEvNF0gQ0VMTCBSNCAgIHRhZyAlUUFNJVI0DQpFQ0hPICBPUEwzIC0tIHRoZSBjb250
cm9sDQpFQ0hPICBOb24tcHVtcC4gTXVzdCBtYXRjaCB0aGUgYmFua2VkIEM0IGFuY2hvci4NCkVD
SE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkRPU0tVVFNV
LkVYRQ0KQ0FMTCBDTFJFTlYNCklGIEVYSVNUIFBST0ZJTEUuREFUIERFTCBQUk9GSUxFLkRBVA0K
Q09QWSBDRkdTXE9QTDMuQ0ZHIERPU0tVVFNVLkNGRyA+IE5VTA0KU0VUIERPU0tVVFNVX0xPR19U
QUc9JVFBTSVSNEINClNFVCBET1NLVVRTVV9UQVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNV
X1RBU19QUk5HX1NFRUQ9MTIzNDUNClNFVCBET1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAw
MDANCkVDSE8uDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09DQpFQ0hPICBbMi80XSBDRUxMIFI0QiAgIHRhZyAlUUFNJVI0Qg0KRUNITyAgT1BMMyByZXBl
YXQgLS0gdGhlIG5vaXNlIGZsb29yDQpFQ0hPICBCYWNrLXRvLWJhY2sgd2l0aCBSNC4gVGhlIHNw
cmVhZCBJUyB0aGUgbm9pc2UgZmxvb3IuDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09DQpET1NLVVRTVS5FWEUNCkNBTEwgQ0xSRU5WDQpJRiBFWElTVCBQ
Uk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xPUkdBTllBLkNGRyBET1NLVVRT
VS5DRkcgPiBOVUwNClNFVCBET1NLVVRTVV9MT0dfVEFHPSVRQU0lUjMNClNFVCBET1NLVVRTVV9U
QVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNVX1RBU19QUk5HX1NFRUQ9MTIzNDUNClNFVCBE
T1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAwMDANCkVDSE8uDQpFQ0hPID09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPICBbMy80XSBDRUxMIFIzICAg
dGFnICVRQU0lUjMNCkVDSE8gIE9yZ2FueWEgLS0gdGhlIHJlLXJlbmRlcmVkIFBDTSBjYWNoZQ0K
RUNITyAgQ2F0Y2hlcyBhIHNpbGVudGx5IGRpZmZlcmVudCByZS1yZW5kZXIuDQpFQ0hPID09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpET1NLVVRTVS5FWEUNCkVD
SE8uDQpFQ0hPICMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMNCkVDSE8gIHN3aXRjaGluZyBQaWNvR1VTIC0+IEFETElCIE1PREUgKG5vIFNC
LCBzbyB0aGUgUElUIHB1bXAgY2FuIG93biBjaDApDQpFQ0hPICMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMNCnBndXNpbml0IC9tb2RlIGFk
bGliDQpDQUxMIENMUkVOVg0KSUYgRVhJU1QgUFJPRklMRS5EQVQgREVMIFBST0ZJTEUuREFUDQpD
T1BZIENGR1NcQURMSUIuQ0ZHIERPU0tVVFNVLkNGRyA+IE5VTA0KU0VUIERPU0tVVFNVX0xPR19U
QUc9JVFBTSVSNQ0KU0VUIERPU0tVVFNVX1RBU19SRVBMQVk9UUEuVEFTDQpTRVQgRE9TS1VUU1Vf
VEFTX1BSTkdfU0VFRD0xMjM0NQ0KU0VUIERPU0tVVFNVX1RBU19BVVRPX0VYSVRfVElDSz0zMDAw
MA0KRUNITy4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT0NCkVDSE8gIFs0LzRdIENFTEwgUjUgICB0YWcgJVFBTSVSNQ0KRUNITyAgQWRMaWIgLS0gdGhl
IHB1bXAgcGF0aA0KRUNITyAgVGhlIHRpbWViYXNlIGZpeCB0b3VjaGVzIE9OTFkgZ3VzL2FkbGli
LiBNb3ZlbWVudCBoZXJlIGlzIGEgRklORElORy4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT0NCkRPU0tVVFNVLkVYRQ0KRUNITy4NCkVDSE8gPT09IFJF
LUJBU0VMSU5FIERPTkUuIExvZ3MgJVFBTSVSNCAlUUFNJVI0QiAlUUFNJVIzICVRQU0lUjUgPT09
DQpFQ0hPID09PSBSNCB2cyBiYW5rZWQgQzQgPSB0aGUgcmUtYmFzZWxpbmU7IFI0IHZzIFI0QiA9
IHRoZSBub2lzZSBmbG9vci4gPT09DQpFQ0hPID09PSBQaWNvR1VTIGxlZnQgaW4gQURMSUIgbW9k
ZS4gPT09DQpFQ0hPLg0KOkVORA0K
'
_bat_write RB.BAT "${_B64_RB}"

_B64_GAP='
QEVDSE8gT0ZGDQpJRiAiJVFBTSUiPT0iIiBHT1RPIE5PTUFDSA0KSUYgIiUxIj09IkdPIiBHT1RP
IEdPDQpDT01NQU5EIC9FOjIwNDggL0MgJTAgR08gJTENCkdPVE8gRU5EDQo6Tk9NQUNIDQpFQ0hP
IEVSUk9SOiBtYWNoaW5lIG5vdCBzZXQuIFJ1biBRQS5CQVQgZmlyc3QgKHBpY2sgeW91ciBDUFUp
LCB0aGVuIHJlLXJ1bi4NCkdPVE8gRU5EDQo6R08NClJFTSAtLS0gUUEtSU5TVFJVTUVOVCB2MSAo
bWF0Y2hlcyBQRy9WQiBzd2VlcCBtYW5pZmVzdHMpIC0tLQ0KSUYgIiVRQU0lIj09IkciIFNFVCBR
QUNQVT1QZW50aXVtIE92ZXJEcml2ZSA4Mw0KSUYgIiVRQU0lIj09IkEiIFNFVCBRQUNQVT1BbTV4
ODYtMTMzDQpJRiAiJVFBTSUiPT0iNiIgU0VUIFFBQ1BVPTQ4NkRYMi02Ng0KSUYgIiVRQU0lIj09
IjUiIFNFVCBRQUNQVT00ODZEWDItNTANCklGIE5PVCBFWElTVCBMT0dTXE5VTCBNS0RJUiBMT0dT
DQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PQ0KRUNITyAgU1dFRVAgOiBHQVAgY2VsbHMgLS0gY2xvc2VzIGZvdXIgbWVhc3Vy
ZW1lbnQgZ2Fwcw0KRUNITyAgQ0VMTFMgOiA0IGNlbGxzLCB+MTAgbWluDQpFQ0hPICBDUFUgICA6
ICVRQUNQVSUNCkVDSE8gIFNPVU5EIDogTUlYRUQgLS0gWDggbmVlZHMgdGhlIFZJQlJBOyB0aGUg
cmVzdCBydW4gb24gYW55IGNhcmQNCkVDSE8gIFZJREVPIDogUzMgVmlSR0UNCkVDSE8gIExPR1Mg
IDogdGFnZ2VkICVRQU0lLi4gIGluIExPR1NcDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRUNITyAgQ2hlY2sgdGhlIENQ
VSBhbmQgY2FyZHMgYWJvdmUgbWF0Y2ggdGhlIGJveCBiZWZvcmUgc3RhcnRpbmcuDQpFQ0hPICBY
OCBpcyBNRUFOSU5HTEVTUyBvZmYgdGhlIFZpYnJhIC0tIHNraXAgb3IgaWdub3JlIGl0IHRoZXJl
Lg0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT0NCkVDSE8gRE9TS1VUU1UgUUEgbGFuZSBtYW5pZmVzdCA+IExPR1NcJVFBTSVH
QVAuTkZPDQpFQ0hPIHN3ZWVwPUdBUCA+PiBMT0dTXCVRQU0lR0FQLk5GTw0KRUNITyBjZWxscz00
ID4+IExPR1NcJVFBTSVHQVAuTkZPDQpFQ0hPIGNwdT0lUUFDUFUlID4+IExPR1NcJVFBTSVHQVAu
TkZPDQpFQ0hPIGxvZ190YWdfcHJlZml4PSVRQU0lID4+IExPR1NcJVFBTSVHQVAuTkZPDQpFQ0hP
IHNvdW5kPU1JWEVEIC0tIFg4IG5lZWRzIHRoZSBWSUJSQTsgdGhlIHJlc3QgcnVuIG9uIGFueSBj
YXJkID4+IExPR1NcJVFBTSVHQVAuTkZPDQpFQ0hPIHZpZGVvPVMzIFZpUkdFID4+IExPR1NcJVFB
TSVHQVAuTkZPDQpFQ0hPIG5vdGU9cnVuIG9yZGVyIGlzIHJlY292ZXJhYmxlIGZyb20gdGhlIFtI
SDpNTTpTU10gcHJlZml4ZXMgaW4gdGhlIGNlbGwgbG9ncyA+PiBMT0dTXCVRQU0lR0FQLk5GTw0K
U0VUIFNETF9ISU5UX0RPU0tVVFNVX0JBTk5FUl9ERUxBWV9NUz0xMDAwMA0KUEFVU0UNCkVDSE8u
DQpJRiBOT1QgIiUyIj09IlBHIiBHT1RPIE5PUEdVUw0KRUNITyAjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjDQpFQ0hPICBQaWNvR1VTIGJv
eDogc3dpdGNoaW5nIHRvIFNCIE1PREUgKFg4L1hIIG5lZWQgYSBsaXZlIFNCIERBQykNCkVDSE8g
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
Iw0KcGd1c2luaXQgL21vZGUgc2INCnBndXNpbml0IC9zYmVudg0KU0VUIEJMQVNURVI9QTIyMCBJ
NyBEMyBQMzMwIFQzDQpHT1RPIENBUkRPSw0KOk5PUEdVUw0KRUNITyAjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjDQpFQ0hPICBVc2luZyB0
aGUgU0IgYWxyZWFkeSBpbiB0aGUgYm94IChWaWJyYTE2LCBvciBhIFBpY29HVVMgbGVmdA0KRUNI
TyAgaW4gc2IgbW9kZSkuIE5vIHBndXNpbml0IGlzIHJ1biwgc28gbm8gUGljb0dVUyBpcyByZXF1
aXJlZC4NCkVDSE8gIE9uIGEgUElDT0dVUyBib3ggcnVuICBHQVAgUEcgIGluc3RlYWQsIHRvIGZv
cmNlIHNiIG1vZGUgLS0NCkVDSE8gIFJCIGFuZCBFQVIgbGVhdmUgdGhlIGNhcmQgaW4gYWRsaWIg
YW5kIFg4L1hIIG5lZWQgYSBEQUMuDQpFQ0hPICMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMNCjpDQVJET0sNCkNBTEwgQ0xSRU5WDQpJRiBF
WElTVCBQUk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xPUEwzLkNGRyBET1NL
VVRTVS5DRkcgPiBOVUwNClNFVCBTRExfSElOVF9ET1NLVVRTVV9BVURJT19TQl9GT1JDRV84QklU
PTENClNFVCBET1NLVVRTVV9MT0dfVEFHPSVRQU0lWDgNClNFVCBET1NLVVRTVV9UQVNfUkVQTEFZ
PVFBLlRBUw0KU0VUIERPU0tVVFNVX1RBU19QUk5HX1NFRUQ9MTIzNDUNClNFVCBET1NLVVRTVV9U
QVNfQVVUT19FWElUX1RJQ0s9MzAwMDANCkVDSE8uDQpFQ0hPID09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPICBbMS80XSBDRUxMIFg4ICBWaWJyYSBmb3Jj
ZWQgdG8gOC1iaXQgRE1BDQpFQ0hPICB0YWcgJVFBTSVYOA0KRUNITyAgVklCUkEgT05MWS4gU2Vw
YXJhdGVzIGNhcmQgZnJvbSBETUEgcGF0aC4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT0NCkRPU0tVVFNVLkVYRQ0KQ0FMTCBDTFJFTlYNCklGIEVYSVNU
IFBST0ZJTEUuREFUIERFTCBQUk9GSUxFLkRBVA0KQ09QWSBDRkdTXE9SR0hRLkNGRyBET1NLVVRT
VS5DRkcgPiBOVUwNClNFVCBET1NLVVRTVV9MT0dfVEFHPSVRQU0lWEgxDQpTRVQgRE9TS1VUU1Vf
VEFTX1JFUExBWT1RQS5UQVMNClNFVCBET1NLVVRTVV9UQVNfUFJOR19TRUVEPTEyMzQ1DQpTRVQg
RE9TS1VUU1VfVEFTX0FVVE9fRVhJVF9USUNLPTMwMDAwDQpFQ0hPLg0KRUNITyA9PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRUNITyAgWzIvNF0gQ0VMTCBYSDEg
IE9yZ2FueWEtSFEgcmUtbWVhc3VyZSwgcnVuIDEgb2YgMg0KRUNITyAgdGFnICVRQU0lWEgxDQpF
Q0hPICA1MTEyIGdhdmUgYW4gaW1wb3NzaWJsZSByYXRpby4gVHdvIHJ1bnMgdGVzdCByZXByb2R1
Y2liaWxpdHkuDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09DQpET1NLVVRTVS5FWEUNCkNBTEwgQ0xSRU5WDQpJRiBFWElTVCBQUk9GSUxFLkRBVCBERUwg
UFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xPUkdIUS5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQg
RE9TS1VUU1VfTE9HX1RBRz0lUUFNJVhIMg0KU0VUIERPU0tVVFNVX1RBU19SRVBMQVk9UUEuVEFT
DQpTRVQgRE9TS1VUU1VfVEFTX1BSTkdfU0VFRD0xMjM0NQ0KU0VUIERPU0tVVFNVX1RBU19BVVRP
X0VYSVRfVElDSz0zMDAwMA0KRUNITy4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT0NCkVDSE8gIFszLzRdIENFTEwgWEgyICBPcmdhbnlhLUhRIHJlLW1l
YXN1cmUsIHJ1biAyIG9mIDINCkVDSE8gIHRhZyAlUUFNJVhIMg0KRUNITyAgSWYgWEgxIGFuZCBY
SDIgZGlzYWdyZWUsIE9SR0hRIGlzIG5vbi1yZXByb2R1Y2libGUuDQpFQ0hPID09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpET1NLVVRTVS5FWEUNCkNBTEwgQ0xS
RU5WDQpJRiBFWElTVCBQUk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xPUEwz
LkNGRyBET1NLVVRTVS5DRkcgPiBOVUwNClNFVCBTRExfSElOVF9ET1NLVVRTVV9BVURJT19PRkY9
MQ0KU0VUIERPU0tVVFNVX0xPR19UQUc9JVFBTSVYMA0KU0VUIERPU0tVVFNVX1RBU19SRVBMQVk9
UUEuVEFTDQpTRVQgRE9TS1VUU1VfVEFTX1BSTkdfU0VFRD0xMjM0NQ0KU0VUIERPU0tVVFNVX1RB
U19BVVRPX0VYSVRfVElDSz0zMDAwMA0KRUNITy4NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT0NCkVDSE8gIFs0LzRdIENFTEwgWDAgIFRSVUUgYXVkaW8g
Zmxvb3IgKEFVRElPX09GRj0xKQ0KRUNITyAgdGFnICVRQU0lWDANCkVDSE8gIERldmljZS1sZXZl
bCBvZmYuIE5PVCB0aGUgc2FtZSBhcyBTSUxFTlQuQ0ZHLg0KRUNITyA9PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0KRE9TS1VUU1UuRVhFDQpFQ0hPLg0KRUNITyA9
PT0gR0FQIENFTExTIERPTkUuIExvZ3MgJVFBTSVYMCAlUUFNJVg4ICVRQU0lWEgxICVRQU0lWEgy
ID09PQ0KRUNITy4NCjpFTkQNCg==
'
_bat_write GAP.BAT "${_B64_GAP}"

_B64_EAR='
QEVDSE8gT0ZGDQpJRiAiJVFBTSUiPT0iIiBHT1RPIE5PTUFDSA0KSUYgIiUxIj09IkdPIiBHT1RP
IEdPDQpDT01NQU5EIC9FOjIwNDggL0MgJTAgR08NCkdPVE8gRU5EDQo6Tk9NQUNIDQpFQ0hPIEVS
Uk9SOiBtYWNoaW5lIG5vdCBzZXQuIFJ1biBRQS5CQVQgZmlyc3QgKHBpY2sgeW91ciBDUFUpLCB0
aGVuIHJlLXJ1bi4NCkdPVE8gRU5EDQo6R08NClJFTSAtLS0gUUEtSU5TVFJVTUVOVCB2MSAtLS0N
CklGICIlUUFNJSI9PSJHIiBTRVQgUUFDUFU9UGVudGl1bSBPdmVyRHJpdmUgODMNCklGICIlUUFN
JSI9PSJBIiBTRVQgUUFDUFU9QW01eDg2LTEzMw0KSUYgIiVRQU0lIj09IjYiIFNFVCBRQUNQVT00
ODZEWDItNjYNCklGICIlUUFNJSI9PSI1IiBTRVQgUUFDUFU9NDg2RFgyLTUwDQpJRiBOT1QgRVhJ
U1QgTE9HU1xOVUwgTUtESVIgTE9HUw0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkVDSE8gIFNXRUVQIDogQlktRUFSIHBh
aXJzIC0tIFRIRVNFIE5FRUQgWU9VUiBFQVJTDQpFQ0hPICBDRUxMUyA6IDMgY2VsbHMsIH44IG1p
bg0KRUNITyAgQ1BVICAgOiAlUUFDUFUlDQpFQ0hPICBTT1VORCA6IFBpY29HVVMgUkVRVUlSRUQg
LS0gc3dpdGNoZXMgc2IgLT4gYWRsaWIgZm9yIHRoZSBBZExpYiBjZWxsDQpFQ0hPICBWSURFTyA6
IFMzIFZpUkdFDQpFQ0hPICBMT0dTICA6IHRhZ2dlZCAlUUFNJS4uICBpbiBMT0dTXA0KRUNITyA9
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT0NCkVDSE8gIFExIFRIRSBTSEFDSzogbXVzaWMgZm9yIHRoZSBXSE9MRSB2aXNpdCwgbGF0ZSwg
b3IgbmV2ZXI/IChFNCB2cyBFMykNCkVDSE8gIFEyIEFETElCIHZzIE9QTDM6IGFjY2VwdGFibGUg
YXMgdGhlIDQ4NiBkZWZhdWx0PyAoRUEgdnMgRTQpDQpFQ0hPICBBZExpYiBpcyBNVVNJQy1PTkxZ
IC0tIFBDLXNwZWFrZXIgU0ZYLCBubyBEQUMgU0ZYLg0KRUNITyA9PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkVDSE8gRE9TS1VUU1Ug
UUEgbGFuZSBtYW5pZmVzdCA+IExPR1NcJVFBTSVFQVIuTkZPDQpFQ0hPIHN3ZWVwPUVBUiA+PiBM
T0dTXCVRQU0lRUFSLk5GTw0KRUNITyBjZWxscz0zID4+IExPR1NcJVFBTSVFQVIuTkZPDQpFQ0hP
IGNwdT0lUUFDUFUlID4+IExPR1NcJVFBTSVFQVIuTkZPDQpFQ0hPIGxvZ190YWdfcHJlZml4PSVR
QU0lID4+IExPR1NcJVFBTSVFQVIuTkZPDQpFQ0hPIHNvdW5kPVBpY29HVVMgUkVRVUlSRUQgLS0g
c3dpdGNoZXMgc2IgLT4gYWRsaWIgZm9yIHRoZSBBZExpYiBjZWxsID4+IExPR1NcJVFBTSVFQVIu
TkZPDQpFQ0hPIHZpZGVvPVMzIFZpUkdFID4+IExPR1NcJVFBTSVFQVIuTkZPDQpFQ0hPIG5vdGU9
cnVuIG9yZGVyIGlzIHJlY292ZXJhYmxlIGZyb20gdGhlIFtISDpNTTpTU10gcHJlZml4ZXMgaW4g
dGhlIGNlbGwgbG9ncyA+PiBMT0dTXCVRQU0lRUFSLk5GTw0KU0VUIFNETF9ISU5UX0RPU0tVVFNV
X0JBTk5FUl9ERUxBWV9NUz0xMDAwMA0KUEFVU0UNCkVDSE8uDQpFQ0hPICMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMNCkVDSE8gIHN3aXRj
aGluZyBQaWNvR1VTIC0+IFNCIE1PREUgKGNlbGxzIG5lZWRpbmcgYSBsaXZlIFNCIERBQykNCkVD
SE8gIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIw0KcGd1c2luaXQgL21vZGUgc2INCnBndXNpbml0IC9zYmVudg0KU0VUIEJMQVNURVI9QTIy
MCBJNyBEMyBQMzMwIFQzDQpDQUxMIENMUkVOVg0KSUYgRVhJU1QgUFJPRklMRS5EQVQgREVMIFBS
T0ZJTEUuREFUDQpDT1BZIENGR1NcT1BMMy5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQgRE9T
S1VUU1VfTE9HX1RBRz0lUUFNJUU0DQpTRVQgRE9TS1VUU1VfVEFTX1JFUExBWT1RQS5UQVMNClNF
VCBET1NLVVRTVV9UQVNfUFJOR19TRUVEPTEyMzQ1DQpTRVQgRE9TS1VUU1VfVEFTX0FVVE9fRVhJ
VF9USUNLPTMwMDAwDQpFQ0hPLg0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PQ0KRUNITyAgWzEvM10gQ0VMTCBFNCAgIHRhZyAlUUFNJUU0DQpFQ0hPICBP
UEwzIC0tIFNoYWNrIHJlZmVyZW5jZQ0KRUNITyAgTElTVEVOIGF0IHRoZSBTaGFjay4gTXVzaWMg
cHJlc2VudCB0aGUgd2hvbGUgdmlzaXQ/DQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09DQpET1NLVVRTVS5FWEUNCkNBTEwgQ0xSRU5WDQpJRiBFWElTVCBQ
Uk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkgQ0ZHU1xPUkdBTllBLkNGRyBET1NLVVRT
VS5DRkcgPiBOVUwNClNFVCBET1NLVVRTVV9MT0dfVEFHPSVRQU0lRTMNClNFVCBET1NLVVRTVV9U
QVNfUkVQTEFZPVFBLlRBUw0KU0VUIERPU0tVVFNVX1RBU19QUk5HX1NFRUQ9MTIzNDUNClNFVCBE
T1NLVVRTVV9UQVNfQVVUT19FWElUX1RJQ0s9MzAwMDANCkVDSE8uDQpFQ0hPID09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09DQpFQ0hPICBbMi8zXSBDRUxMIEUzICAg
dGFnICVRQU0lRTMNCkVDSE8gIE9yZ2FueWEgLS0gU2hhY2sgc3VzcGVjdA0KRUNITyAgTElTVEVO
IGF0IHRoZSBTaGFjay4gU2lsZW50PyBMYXRlPyBXaG9sZSB2aXNpdD8NCkVDSE8gPT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0NCkRPU0tVVFNVLkVYRQ0KRUNITy4N
CkVDSE8gIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIw0KRUNITyAgc3dpdGNoaW5nIFBpY29HVVMgLT4gQURMSUIgTU9ERSAobm8gU0IsIHNv
IHRoZSBQSVQgcHVtcCBjYW4gb3duIGNoMCkNCkVDSE8gIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMj
IyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIw0KcGd1c2luaXQgL21vZGUgYWRsaWIN
CkNBTEwgQ0xSRU5WDQpJRiBFWElTVCBQUk9GSUxFLkRBVCBERUwgUFJPRklMRS5EQVQNCkNPUFkg
Q0ZHU1xBRExJQi5DRkcgRE9TS1VUU1UuQ0ZHID4gTlVMDQpTRVQgRE9TS1VUU1VfTE9HX1RBRz0l
UUFNJUVBDQpTRVQgRE9TS1VUU1VfVEFTX1JFUExBWT1RQS5UQVMNClNFVCBET1NLVVRTVV9UQVNf
UFJOR19TRUVEPTEyMzQ1DQpTRVQgRE9TS1VUU1VfVEFTX0FVVE9fRVhJVF9USUNLPTMwMDAwDQpF
Q0hPLg0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQ0K
RUNITyAgWzMvM10gQ0VMTCBFQSAgIHRhZyAlUUFNJUVBDQpFQ0hPICBBZExpYiAtLSA0ODYgZGVm
YXVsdCBjYW5kaWRhdGUNCkVDSE8gIENvbXBhcmUgYWdhaW5zdCBFNCBieSBlYXIuIEdvb2QgZW5v
dWdoIHRvIHJlY29tbWVuZD8NCkVDSE8gPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT0NCkRPU0tVVFNVLkVYRQ0KRUNITy4NCkVDSE8gPT09IFdyaXRlIHRoZSBhbnN3
ZXJzIGRvd24gTk9XLCB3aGlsZSBmcmVzaDogPT09DQpFQ0hPID09PSAgRTQgU2hhY2sgbXVzaWM6
ICB3aG9sZSB2aXNpdCAvIGxhdGUgLyBuZXZlciA9PT0NCkVDSE8gPT09ICBFMyBTaGFjayBtdXNp
YzogIHdob2xlIHZpc2l0IC8gbGF0ZSAvIG5ldmVyID09PQ0KRUNITyA9PT0gIEVBIHZzIEU0OiAg
ICAgICAgYWNjZXB0YWJsZSAvIG5vdCBhY2NlcHRhYmxlIGFzIDQ4NiBkZWZhdWx0ID09PQ0KRUNI
TyA9PT0gUGljb0dVUyBsZWZ0IGluIEFETElCIG1vZGUuID09PQ0KRUNITy4NCjpFTkQNCg==
'
_bat_write EAR.BAT "${_B64_EAR}"

_B64_QA='
QEVDSE8gT0ZGDQpJRiAiJTEiPT0iR08iIEdPVE8gR08NCkNPTU1BTkQgL0U6MjA0OCAvSyAlMCBH
TyAlMQ0KR09UTyBFTkQNCjpHTw0KSUYgIiUyIj09IjEiIFNFVCBRQU09Rw0KSUYgIiUyIj09IjIi
IFNFVCBRQU09QQ0KSUYgIiUyIj09IjMiIFNFVCBRQU09Ng0KSUYgIiUyIj09IjQiIFNFVCBRQU09
NQ0KQ0xTDQpFQ0hPID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PQ0KRUNITyAgIGRvc2t1dHN1IFFBICAgLS0gICB3aGF0IHRvIHJ1bg0K
RUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT0NCkVDSE8gICBST1VORCAyIC0tIHJ1biB0aGVzZSBub3cgKHNlZSBkb2NzL1FBLVJV
Ti1TSEVFVC5tZCkNCkVDSE8gICAgIEdBUCAgICA0IGNlbGxzICBYOCBjYXJkLXZzLURNQSAoVklC
UkEpLCBhdWRpbyBmbG9vciwgT1JHSFENCkVDSE8gICAgICAgICAgICAgIG9uIGEgUElDT0dVUyBi
b3ggdHlwZSAgR0FQIFBHICBpbnN0ZWFkDQpFQ0hPICAgICBSQiAgICAgNCBjZWxscyAgcmUtYmFz
ZWxpbmUgKyBub2lzZSBmbG9vciAgICAgIChQaWNvR1VTKQ0KRUNITyAgICAgRUFSICAgIDMgY2Vs
bHMgIEJZIEVBUjogU2hhY2sgbXVzaWMsIEFkTGliICAgICAoUGljb0dVUykNCkVDSE8gICAgIFBS
T1ZFICA2IGNlbGxzICBwcm92ZS1vdXQgYSBORVcgYmluYXJ5ICAgICAgICAgKFBpY29HVVMpDQpF
Q0hPICAgVGhlc2UgdGFrZSBOTyBudW1iZXIgLS0gdGhleSB1c2UgdGhlIG1hY2hpbmUgc2V0IGFi
b3ZlLg0KRUNITy4NCkVDSE8gICBST1VORCAxIFNXRUVQUyAoY29tcGxldGU7IG4gPSB0aGUgQ1BV
IG51bWJlcikNCkVDSE8gICAgIFBHIG4gICBQaWNvR1VTOiAxMCBjZWxscywgc2IgKyBndXMgKyBh
ZGxpYiArIFdhdmVCbGFzdGVyDQpFQ0hPICAgICBWQiBuICAgVmlicmExNjogIDYgY2VsbHMsIEFV
VE8gKyBXQiArIE9QTDMgKyBPcmdhbnlhDQpFQ0hPLg0KRUNITyAgIG4gPSB0aGUgQ1BVIHRoYXQg
aXMgaW4gdGhlIGJveCBSSUdIVCBOT1c6DQpFQ0hPICAgICAxIFBPRC04MyAgICAgIDIgQW01eDg2
LTEzMyAgICAgMyBEWDItNjYgICAgICA0IERYMi01MA0KRUNITyAgIEl0IHNldHMgdGhlIGxvZyB0
YWcsIHNvIGEgd3JvbmcgbiBmaWxlcyByZXN1bHRzIHVuZGVyIHRoZQ0KRUNITyAgIHdyb25nIG1h
Y2hpbmUuIE5vdGhpbmcgZWxzZSBkZWNpZGVzIGl0Lg0KRUNITy4NCkVDSE8gICBIQU5EUy1PTiAo
VmlicmEgcGhhc2UpDQpFQ0hPICAgICBHMTEgRzEyIEcxMyBHMTQgRzE1ICAgU0VUVVAgd2Fsaw0K
RUNITyAgICAgRzExNSBqb3lzdGljayAgIEcxMTYgc2F2ZS9sb2FkICAgRzExNyBmcmVlIHBsYXkN
CkVDSE8uDQpFQ0hPICAgT1RIRVI6ICBDMSBlbnYgcHJvYmUgICAgUkVDT1JEIHJlLXJlY29yZCB0
aGUgcmVlbCAoRE8gTk9UKQ0KRUNITyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09
PT09PT09PT09PT09PT09PT09PT09PT09PT0NCklGICIlUUFNJSI9PSIiIEdPVE8gTk9NQUNIDQpF
Q0hPICAgTWFjaGluZSBzZXQ6IFFBTT0lUUFNJSAgIChsb2dzIHRhZyAlUUFNJS4uKSAgIFR5cGUg
RVhJVCB0byBsZWF2ZS4NCkdPVE8gRU5EDQo6Tk9NQUNIDQpFQ0hPICAgTm8gQ1BVIHNldCB5ZXQu
IEVpdGhlciBydW4gIFFBIG4gLCBvciAgU0VUIFFBTT1HICAoRy9BLzYvNSkuDQo6RU5EDQo=
'
_bat_write QA.BAT "${_B64_QA}"

echo "[5d/8] install the ROUND-1 BENCHMARK reel as QA.TAS"
# The kit payload historically shipped a 492-byte FALLBACK reel, and no round-1
# cell ever used it -- every banked number came from an operator RECORD.BAT take
# that happened to already be on the card. Populating a blank or replaced card
# therefore installed a DIFFERENT reel while producing logs that look entirely
# normal: [fps-true] valid, routes complete, no gate anywhere catches it. The
# comparison it silently invalidates is the whole campaign. Shipping the real
# reel makes a blank card correct by construction.
_B64_REEL='
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
AAAGFAAAAAAAABQUAAAAAAAI
'
# Guarded, because an unconditional write silently undid step [3]'s stash and
# made steps [3]/[4] print "preserved" about a reel that was then overwritten.
# The right question is not "is there an operator take" -- preserving a take
# that is NOT the benchmark reel is exactly the failure this whole block
# exists to prevent -- it is "is the reel on the card the benchmark reel".
_cur=""
[ -f "${CF_GAME_DIR}/QA.TAS" ] && _cur=$(sha256sum "${CF_GAME_DIR}/QA.TAS" | cut -d' ' -f1)
if [ "${_cur}" = "${EXP_TAS_SHA}" ]; then
  echo "  QA.TAS already IS the benchmark reel -- left alone"
elif [ "${KEEP_REEL:-0}" = "1" ]; then
  echo "  KEEP_REEL=1 -- leaving the card's reel in place (sha ${_cur:0:12})"
  echo "  WARNING: that reel is NOT the benchmark reel. Results will not be"
  echo "           comparable to any banked round."
else
  if [ -n "${_cur}" ] && [ "${_cur}" != "${EXP_TAS_SHA}" ]; then
    echo "  REPLACING a different reel (sha ${_cur:0:12}) with the benchmark reel."
    echo "  A copy of what was there is at ${OPTAKE:-${STAGING}/QA-REPLACED.TAS}"
    [ -n "${OPTAKE}" ] || cp "${CF_GAME_DIR}/QA.TAS" "${STAGING}/QA-REPLACED.TAS" 2>/dev/null || true
    echo "  Pass KEEP_REEL=1 to keep your own reel instead."
  fi
  printf '%s' "${_B64_REEL}" | base64 -d > "${CF_GAME_DIR}/QA.TAS"
fi
_rsha=$(sha256sum "${CF_GAME_DIR}/QA.TAS" | cut -d' ' -f1)
_rsz=$(stat -c%s "${CF_GAME_DIR}/QA.TAS" 2>/dev/null || echo 0)
if [ "${KEEP_REEL:-0}" = "1" ] && [ "${_rsha}" != "${EXP_TAS_SHA}" ]; then
  # Deliberate keep: the verification below would otherwise report an ERROR
  # about a reel that was never written. Operators read confusing output as
  # breakage, which is most of how tonight went.
  echo "  QA.TAS left as the operator's own reel by request (${_rsz} B, sha ${_rsha:0:12})"
elif [ "${_rsha}" = "${EXP_TAS_SHA}" ]; then
  echo "  installed QA.TAS (${_rsz} B, sha ${_rsha:0:12}...) -- matches the round-1 benchmark reel"
else
  echo "  ERROR: QA.TAS wrote but sha does not match the expected benchmark reel!"
  echo "         got ${_rsha}"
  echo "         want ${EXP_TAS_SHA}"
fi

echo "[5e/8] record which binary is on the card + top up CLRENV"
# No engine log records the binary's sha: the runmanifest field named
# binary_sha12 carries the Organya cache key instead. So the card must say,
# or a result cannot be attributed to a build afterwards.
_bsha=$(sha256sum "${CF_GAME_DIR}/DOSKUTSU.EXE" | cut -d' ' -f1)
_R1SHA="09e449c5a81ddbdc405c7fc07c0964685db8e1c5925edf3eb9086fb97e35cc42"
{ echo "binary_sha256=${_bsha}"
  echo "binary_sha12=${_bsha:0:12}"
  echo "reel_sha12=$(sha256sum "${CF_GAME_DIR}/QA.TAS" | cut -c1-12)"
  echo "tarball=${TARBALL}"
} > "${CF_GAME_DIR}/BINARY.NFO"
if [ "${_bsha}" = "${_R1SHA}" ]; then
  rm -f "${CF_GAME_DIR}/ROUND2.OK"
  echo "  binary is the ROUND-1 build ${_bsha:0:12} -- ROUND2.OK NOT written"
  echo "  RB will refuse: re-baselining round 1 against round 1 agrees perfectly"
  echo "  and proves nothing. PROVE / GAP / EAR are unaffected."
else
  echo "round2" > "${CF_GAME_DIR}/ROUND2.OK"
  echo "  binary ${_bsha:0:12} is NOT the round-1 build -- ROUND2.OK written, RB enabled"
fi
# CLRENV is the choke point every cell CALLs but predates PUMP_TIMEBASE, so a
# killswitch set by one cell survived into the next. Per-BAT clears fix today's
# BATs; this fixes the mechanism.
if ! grep -q "SDL_HINT_DOSKUTSU_PUMP_TIMEBASE" "${CF_GAME_DIR}/CLRENV.BAT" 2>/dev/null; then
  printf 'SET DOSKUTSU_PUMP_TIMEBASE=\r\nSET SDL_HINT_DOSKUTSU_PUMP_TIMEBASE=\r\n' >> "${CF_GAME_DIR}/CLRENV.BAT"
  echo "  appended PUMP_TIMEBASE clears to CLRENV.BAT"
else
  echo "  CLRENV.BAT already clears PUMP_TIMEBASE"
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
# Byte-level, not `file`-output-level: `file` phrases CRLF differently across
# versions and this gate ABORTS the whole install on a string mismatch.
for b in "${CF_GAME_DIR}"/*.BAT; do
  _tot=$(wc -l < "$b"); _crlf=$(grep -c $'\r$' "$b" || true)
  [ "${_tot}" = "${_crlf}" ] || { echo "  FAIL: $(basename "$b") not CRLF (${_crlf}/${_tot} lines)"; badbat=1; }
  if grep -q $'\r\r' "$b" 2>/dev/null; then echo "  FAIL: $(basename "$b") has double-CR"; badbat=1; fi
  if LC_ALL=C grep -qP '[^\x00-\x7F]' "$b"; then echo "  FAIL: $(basename "$b") has non-ASCII"; badbat=1; fi
done
[ $badbat -eq 0 ] && echo "  PASS: all $(ls "${CF_GAME_DIR}"/*.BAT | wc -l) BATs CRLF + ASCII" || { echo "  ABORT: BAT gate failed"; exit 1; }
sync
echo "  sync done"
if [ "${DRY_RUN}" = "1" ]; then
  echo "  DRY_RUN=1 -- skipping unmount"
elif mountpoint -q "${CF_MOUNT}"; then
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
