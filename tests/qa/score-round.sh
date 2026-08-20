#!/usr/bin/env bash
# score-round.sh -- decide whether a returned sweep is scoreable, from the
# artifacts it returned. Nothing here is a judgement about the NUMBERS; it
# only answers "may these numbers enter RESULTS-MATRIX.md at all".
#
# WHY THIS IS A SCRIPT AND NOT A LIST IN A RUN SHEET. On 2026-08-20 Round P
# ran with UniVBE absent, so the ViRGE answered from its own ROM: no 320x240,
# no LFB, 640x480 banked, and a black screen that read as a hang. The check
# that would have caught it -- the VBE provider assertion -- was written into
# the run sheet that same morning, as prose, to be applied to the returned
# logs. Prose applied by hand after a 14-minute run is not a gate. This is.
#
# Usage:  tests/qa/score-round.sh <dir> <SWEEP> <tag> [tag...]
#   e.g.  tests/qa/score-round.sh ~/doskutsu-netiter/incoming PUMP \
#             GPU0 GPUA GPU0B GPUAB
set -uo pipefail

DIR="${1:?usage: score-round.sh <dir> <SWEEP> <tag>...}"; shift
SWEEP="${1:?missing sweep name}"; shift
TAGS=("$@")
[[ ${#TAGS[@]} -gt 0 ]] || { echo "no tags given"; exit 2; }

FAIL=0
pass() { printf '  \033[32mPASS\033[0m  %s\n' "$1"; }
fail() { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; FAIL=1; }
note() { printf '  ..    %s\n' "$1"; }

# The manifest is named by the sweep and the machine prefix, which is the
# first character of any tag. Derive it rather than asking for it: a
# hand-passed name is one more thing that can disagree with the run.
MPFX="${TAGS[0]:0:1}"
NFO=""
for c in "$DIR/${MPFX}${SWEEP}.NFO" "$DIR/${MPFX}${SWEEP}.nfo"; do
  [[ -f "$c" ]] && NFO="$c" && break
done

echo "== envelope (profile sec. 3.5) =="
if [[ -z "$NFO" ]]; then
  fail "sweep manifest ${MPFX}${SWEEP}.NFO not returned -- PUT.BAT sends only the LOG pair; pull it with CHK.BAT"
else
  pass "manifest $(basename "$NFO")"
fi
for t in "${TAGS[@]}"; do
  for f in "$t.LOG" "${t}SDL.LOG"; do
    [[ -s "$DIR/$f" ]] && pass "$f ($(stat -c%s "$DIR/$f") B)" || fail "$f missing or empty"
  done
done

if [[ -n "$NFO" ]]; then
  echo
  echo "== run contract (harness-1) =="
  grep -qa "schema=harness-1" "$NFO" && pass "schema=harness-1" \
    || fail "no schema= line -- pre-standard sweep"
  n=$(grep -ac "^cell=" "$NFO" || true)
  [[ "$n" -eq "${#TAGS[@]}" ]] && pass "$n cell= lines, matching $n tags" \
    || fail "$n cell= lines for ${#TAGS[@]} tags"
  grep -qa "^cell=.*class=" "$NFO" && pass "cells carry class=" \
    || fail "no class= on any cell (profile sec. 4.1)"

  echo
  echo "== declared hardware (profile sec. 3.6) =="
  for f in video_declared sound_declared; do
    v=$(grep -a "^$f=" "$NFO" | head -1 | cut -d= -f2- | tr -d '\r' | sed 's/[[:space:]]*$//')
    case "${v:-}" in
      ""|UNDECLARED) fail "$f is ${v:-absent} -- nobody said what was fitted";;
      *) pass "$f=$v";;
    esac
  done
  v=$(grep -a "^config=" "$NFO" | head -1 | cut -d= -f2- | tr -d '\r' | sed 's/[[:space:]]*$//')
  [[ -n "${v:-}" ]] && pass "config=$v (boot profile, detected not declared)" \
    || fail "config= empty -- boot profile unattested"
  grep -qa "pgusmode_readback=" "$NFO" && pass "PicoGUS mode read back" \
    || fail "no pgusmode_readback -- the requested mode is not the achieved mode"
fi

echo
echo "== VBE provider, per cell (profile sec. 5 precondition 2) =="
# This is the one that cost a round. A cell failing either grep is not
# scoreable no matter how plausible its fps looks -- it measured a
# different graphics stack.
for t in "${TAGS[@]}"; do
  f="$DIR/${t}SDL.LOG"
  [[ -s "$f" ]] || { fail "$t: no SDL log to check"; continue; }
  oem=$(grep -a "oem_string=" "$f" | head -1 | sed "s/.*oem_string=//" | tr -d "'\r")
  if [[ "$oem" == "Universal VESA VBE 6.70" ]]; then
    pass "$t oem_string=$oem"
  else
    fail "$t oem_string=${oem:-absent} -- UniVBE not resident, card ROM answered"
  fi
  if grep -qa "has_lfb=1 use_lfb=1 banked=0" "$f"; then
    pass "$t LFB engaged, not banked"
  else
    got=$(grep -ao "has_lfb=[01] use_lfb=[01] banked=[01]" "$f" | head -1)
    fail "$t ${got:-no modeset line} -- wanted has_lfb=1 use_lfb=1 banked=0"
  fi
done

echo
if [[ $FAIL -eq 0 ]]; then
  echo "=== SCOREABLE -- may enter RESULTS-MATRIX.md ==="
else
  echo "=== NOT SCOREABLE -- provisional, do not bank ==="
fi
exit $FAIL
