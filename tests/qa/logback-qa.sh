#!/usr/bin/env bash
# logback-qa.sh -- ONE campaign-end command for the doskutsu v1.6.3 FULL-QA pass.
# Pulls ALL CPU-tagged logs (engine <TAG>.LOG + SDL <TAG>SDL.LOG), env dumps
# (<TAG>.TXT) and CFG snapshots (<TAG>.CFG) off the CF LOGS dir, ships them to
# claude:/tmp/qa-v163/<MACH>/, content-gates (non-empty; reports the per-machine
# tag set present), prints a LOUD UPLOAD_OK / UPLOAD_FAILED_REASON line, and
# LEAVES THE CF MOUNTED (operator preference 2026-07-07).
#
#   Operator runs (once, after the last round):
#     scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh
#
# Not set -e: continue on individual missing files (partial rounds are legit).
set -u

CF_MOUNT="/media/micheal/DOS"
CF_LOGS="${CF_MOUNT}/doskutsu/LOGS"
LOCAL="/tmp/qa-v163-pull"
REMOTE_BASE="/tmp/qa-v163"
# Every logback lands in its OWN directory. A fixed destination let a later
# round silently overwrite an earlier one -- same cell names, same machine
# code, scp merges and the older file is gone. Label the run on the command
# line, or a timestamp is used.
RUN_LABEL="${1:-}"
if [ -z "${RUN_LABEL}" ]; then RUN_LABEL="run-$(date +%Y%m%d-%H%M%S)"; fi
RUN_LABEL=$(echo "${RUN_LABEL}" | tr -c 'A-Za-z0-9._-' '-')
REMOTE="${REMOTE_BASE}/${RUN_LABEL}"

echo "=== doskutsu FULL-QA :: campaign logback ==="
echo "    destination: claude:${REMOTE}"

fail() { echo; echo "UPLOAD_FAILED_REASON: $1"; echo "  fix: $2"; exit 1; }

[ -d "${CF_LOGS}" ] || fail "CF LOGS dir ${CF_LOGS} not found (CF not mounted, or no round ran)" \
  "mount the CF and re-run: scp claude:/tmp/logback-qa.sh /tmp/ && bash /tmp/logback-qa.sh"

# Collect every log/dump/cfg the campaign writes.
shopt -s nullglob
files=( "${CF_LOGS}"/*.LOG "${CF_LOGS}"/*.TXT "${CF_LOGS}"/*.CFG "${CF_LOGS}"/*.NFO )
shopt -u nullglob
[ "${#files[@]}" -gt 0 ] || fail "no *.LOG / *.TXT / *.CFG / *.NFO in ${CF_LOGS}" \
  "confirm at least one QA cell ran on the test PC (LOGS should be non-empty)"

rm -rf "${LOCAL}"; mkdir -p "${LOCAL}"
declare -A seen_mach
empties=0; total=0
for f in "${files[@]}"; do
  bn=$(basename "$f")
  m="${bn:0:1}"                       # machine code: G / A / 6 / 5
  case "$m" in G|A|6|5) : ;; *) m="_" ;; esac
  mkdir -p "${LOCAL}/${m}"
  cp "$f" "${LOCAL}/${m}/${bn}"
  seen_mach[$m]=1
  total=$((total+1))
  [ -s "$f" ] || { echo "  ZERO-BYTE (suspect): ${m}/${bn}"; empties=$((empties+1)); }
done

echo "--- collected ${total} file(s) from ${CF_LOGS} ---"
declare -A MNAME=( [G]="POD-83/g2k" [A]="Am5x86-133" [6]="DX2-66" [5]="DX2-50" [_]="UNTAGGED" )

# Scope 2026-07-26 (reduced) + re-ordered 2026-08-05: THREE bench phases across
# TWO CPU tags -- DX2-66 (tag 6) = phase 1; POD-83 (tag G) = phases 2 AND 3, so
# the G set is the union of the PicoGUS cells and the Vibra cells. Am5x86 (A)
# and DX2-50 (5) are OUT OF SCOPE, so their absence is EXPECTED and is never an
# error. Coverage below is informational only: a partial phase is legitimate
# and must still upload.
EXPECTED_MACH="G A 6 5"
# phase 1 -- 486DX2-66 + ViRGE + PicoGUS
# Same expected set whatever CPU it ran on -- the sweeps are CPU-agnostic.
EXP_6="C1 C2 C3 C4 C5 C7 C8 22 23A 23B 31 41 51"
EXP_A="${EXP_6}"
EXP_5="${EXP_6}"
# phase 2 (PicoGUS) + phase 3 (Vibra), both on the POD-83
EXP_G="01 C3 C4 C5 22 23A 23B 31 41 51 02 11 12 13 14 15 16 17 18 111 112 115 116 117"

for m in G A 6 5 _; do
  [ -n "${seen_mach[$m]:-}" ] || continue
  tags=$(cd "${LOCAL}/${m}" && ls | sed 's/SDL\.LOG$//; s/\.[A-Z]*$//' | sort -u | tr '\n' ' ')
  n=$(ls "${LOCAL}/${m}" | wc -l)
  echo "  [${m}] ${MNAME[$m]}: ${n} file(s); tags: ${tags}"
done

# Per-round coverage vs the walked cell list (informational, never fatal).
for m in ${EXPECTED_MACH}; do
  if [ -z "${seen_mach[$m]:-}" ]; then
    echo "  NOTE: no [${m}] ${MNAME[$m]} logs present -- that round did not run (or ran unlogged)."
    continue
  fi
  eval "exp=\"\${EXP_${m}}\""
  present=$(cd "${LOCAL}/${m}" && ls | sed 's/SDL\.LOG$//; s/\.[A-Z]*$//' | sort -u)
  miss=""
  for c in ${exp}; do
    printf '%s\n' "${present}" | grep -qx "${m}${c}" || miss="${miss} ${c}"
  done
  if [ -n "${miss}" ]; then
    echo "  coverage [${m}]: MISSING cell log(s):${miss}"
  else
    echo "  coverage [${m}]: all $(set -- ${exp}; echo $#) walked cells produced logs"
  fi
done

# The fps anchor rows the matrix needs. Not fatal -- just called out early so a
# missing anchor is noticed now rather than during analysis.
# The matched cross-CPU pair is GC4 vs 6C4 -- the SAME cell (C4.BAT, OPL3) on
# the SAME card/mode/video, differing only in CPU. G18 is the phase-3 Vibra
# OPL3 row: valuable, but NOT the cross-CPU pair (different sound card).
for pair in "G:GC4:POD-83 OPL3 (PicoGUS-sb) ANCHOR" "6:6C4:DX2-66 OPL3 (PicoGUS-sb) ANCHOR" "G:G18:POD-83 OPL3 (Vibra SB16)"; do
  mm="${pair%%:*}"; rest="${pair#*:}"; tg="${rest%%:*}"; lbl="${rest#*:}"
  if [ -n "${seen_mach[$mm]:-}" ]; then
    [ -s "${LOCAL}/${mm}/${tg}.LOG" ] \
      && echo "  anchor [${lbl}]: ${tg}.LOG present ($(wc -c < "${LOCAL}/${mm}/${tg}.LOG") bytes)" \
      || echo "  anchor [${lbl}]: ${tg}.LOG MISSING or empty -- fps matrix row will be blank"
  fi
done

# LOUD flag for an out-of-scope machine tag. QA.BAT still lists all four CPUs,
# but this campaign only uses [1] POD-83 and [3] DX2-66. A stray A/5 tag almost
# always means the CPU was mis-picked at the QA menu, which would file round-2
# results (incl. the DX2-66 fps anchor) under the wrong machine.
# All four CPUs are in scope as of 2026-08-05 (operator re-opened Am5x86-133
# and DX2-50), so no machine tag is "stray" any more. Absence of a tag simply
# means that CPU was not run; the coverage lines above already say so.
# Also pull the QA.TAS reel that actually drove the campaign (the shipped
# fallback OR the operator's RECORD.BAT take) so build-qa can DOSBox-re-verify
# it. The scene gate is PER-REEL: the shipped fallback is 72->13->18->13 with a
# tolerant ~2520 end tick, but an OPERATOR take has its own route and must be
# checked for coherence + clean auto-exit instead (asserting the fallback gate
# against an operator take is a FALSE desync alarm) -- catches
# a flaky/short operator take that would otherwise have silently skewed every
# watch cell. Tiny file; cheap to always include.
if [ -f "${CF_MOUNT}/doskutsu/QA.TAS" ]; then
  mkdir -p "${LOCAL}/reel"
  cp "${CF_MOUNT}/doskutsu/QA.TAS" "${LOCAL}/reel/QA-USED.TAS"
  sz=$(stat -c%s "${CF_MOUNT}/doskutsu/QA.TAS" 2>/dev/null || echo 0)
  echo "--- reel: QA.TAS on CF = ${sz} bytes -> shipping as reel/QA-USED.TAS for re-verify ---"
  total=$((total+1))
fi

# Ship to claude.
if ssh claude "[ -e ${REMOTE} ]" 2>/dev/null; then
  fail "destination claude:${REMOTE} already exists -- refusing to overwrite a previous pull" \
    "re-run with a distinct label, e.g. bash /tmp/logback-qa.sh round2-pg1"
fi
if ! ssh claude "mkdir -p ${REMOTE}"; then
  fail "cannot reach claude over ssh to create ${REMOTE}" "check ssh claude connectivity, then re-run this script"
fi
if ! scp -r "${LOCAL}"/* "claude:${REMOTE}/" >/dev/null; then
  fail "scp of pulled logs to claude:${REMOTE}/ failed" \
    "re-run, or manually: scp -r ${LOCAL}/* claude:${REMOTE}/"
fi

echo
echo "[final] sync (LEAVE CF MOUNTED)"
sync
echo "  CF left mounted at ${CF_MOUNT} (unmount manually when done:"
echo "    udisksctl unmount -b \$(findmnt -no SOURCE ${CF_MOUNT}) )"
echo

basenames=$(for m in G A 6 5 _; do [ -d "${LOCAL}/${m}" ] && ls "${LOCAL}/${m}"; done | tr '\n' ' ')
if [ "${empties}" -gt 0 ]; then
  echo "UPLOAD_OK: shipped ${total} file(s) to claude:${REMOTE}/ (${empties} ZERO-BYTE -- flag for re-run): ${basenames}"
else
  echo "UPLOAD_OK: shipped ${total} file(s) to claude:${REMOTE}/ : ${basenames}"
fi
