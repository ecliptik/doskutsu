#!/usr/bin/env bash
# harness-lint.sh -- conformance gate for HARNESS-STANDARD.md + the doskutsu
# profile. Runs over sweep BATs and reports violations by requirement id.
#
# The standard requires this to run BEFORE packaging: a check that runs after
# an artifact reaches hardware finds defects one round too late.
#
# Usage:  tests/qa/harness-lint.sh <dir-of-BATs> [--level L1|L2|L3]
# Exit:   0 clean, 1 violations found.

set -uo pipefail
DIR="${1:-}"; LEVEL="L3"
[ "${2:-}" = "--level" ] && LEVEL="${3:-L3}"
[ -d "$DIR" ] || { echo "usage: $0 <dir-of-BATs> [--level L1|L2|L3]"; exit 2; }

VIOL=0
note() { printf '%-9s %-13s %s\n' "$1" "$2" "$3"; VIOL=$((VIOL+1)); }

# ------------------------------------------------------------- L0 is DELEGATED
# The platform layer (CRLF / ASCII / 8.3 / REM-ECHO redirects / LOG_TAG length
# / external commands / bundled files) is ALREADY covered by
# tests/dos-bat-audit.sh, which runs on the extracted tarball -- the immutable
# shipped artifact -- rather than on a staging dir a host linter can still
# rewrite. Duplicating those checks here would create two gates that drift,
# and the drifting one is always the one nobody is watching.
#
# Run BOTH before packaging:
#   tests/dos-bat-audit.sh <package.tar.gz>     # L0 platform
#   tests/qa/harness-lint.sh <dir> --level L3   # L1/L2/L3 run contract
lint_platform() { :; }

# ----------------------------------------------------------- L1/L2/L3 contract
lint_contract() {
  local f="$1" b; b=$(basename "$f")
  local body; body=$(tr -d '\r' < "$f")
  local tags; tags=$(printf '%s\n' "$body" | grep -oE 'SET DOSKUTSU_LOG_TAG=[^ ]+' | sed 's/.*=//')
  local ncells; ncells=$(printf '%s\n' "$tags" | grep -c . || true)
  [ "$ncells" -eq 0 ] && return 0   # not a sweep

  # -- 6.3 envelope: a manifest must exist
  printf '%s\n' "$body" | grep -q '\.NFO' \
    || note "L1-MANIF" "$b" "sweep emits no .NFO manifest"

  # -- 6.3 required manifest fields
  for fld in sweep= cells= log_tag_prefix= config=; do
    printf '%s\n' "$body" | grep -q "$fld" \
      || note "L1-FIELD" "$b" "manifest missing field: ${fld%=}"
  done

  # -- 6.4 declared hardware MUST be parameterised, never hardcoded
  printf '%s\n' "$body" | grep -nE '^ECHO (video|sound)(_declared)?=' \
    | grep -vE '%[A-Za-z_]+%' | while read -r l; do
      note "L2-DECL" "$b" "declared hardware hardcoded, not parameterised: ${l%% *}"
    done
  if [ "$LEVEL" != "L1" ]; then
    printf '%s\n' "$body" | grep -q 'video_declared=' \
      || note "L2-DECL" "$b" "no video_declared field"
    printf '%s\n' "$body" | grep -q 'sound_declared=' \
      || note "L2-DECL" "$b" "no sound_declared field"
  fi

  # -- 6.5 tag uniqueness inside the sweep
  local dupes; dupes=$(printf '%s\n' "$tags" | sort | uniq -d)
  [ -n "$dupes" ] && note "L1-TAG" "$b" "duplicate log tags: $(echo $dupes)"

  # -- P3 start and end banner per cell (L2+)
  if [ "$LEVEL" != "L1" ]; then
    local nruns nend
    nruns=$(printf '%s\n' "$body" | grep -ciE '^DOSKUTSU\.EXE' || true)
    nend=$(printf '%s\n' "$body" | grep -ciE '^ECHO .*CELL (DONE|END)' || true)
    [ "$nend" -lt "$nruns" ] \
      && note "L2-BANNER" "$b" "$nruns cell run(s), $nend cell-end banner(s)"
  fi

  # -- 6.1 every cell preceded by an environment clear
  local nclr; nclr=$(printf '%s\n' "$body" | grep -c 'CALL CLRENV' || true)
  local nrun; nrun=$(printf '%s\n' "$body" | grep -ciE '^DOSKUTSU\.EXE' || true)
  [ "$nclr" -lt "$nrun" ] \
    && note "L1-CLEAR" "$b" "$nrun cell(s) but only $nclr env clear(s)"

  # -- 6.4 (L2) a sweep that switches the sound card MUST record a readback,
  # regardless of cell class. config= cannot distinguish the sound lanes once
  # the boot profiles merge, and a diag run needs to know what the card was
  # doing as much as a perf run does.
  if [ "$LEVEL" != "L1" ] && printf '%s\n' "$body" | grep -qE '^pgusinit'; then
    printf '%s\n' "$body" | grep -qE 'pgus[a-z_]*readback=' \
      || note "L2-READBK" "$b" "switches the PicoGUS but records no readback"
  fi

  # -- 4.1 result class. Levels attach to CLASSES, not files: a sweep may mix
  # a paired perf comparison with diag cells comparable to nothing. Absent a
  # machine-readable class, L3 rules below cannot be applied honestly.
  local nclass; nclass=$(printf '%s\n' "$body" | grep -c 'class=' || true)
  local has_perf=0
  printf '%s\n' "$body" | grep -q 'class=perf' && has_perf=1
  if [ "$nclass" -eq 0 ]; then
    note "L2-CLASS" "$b" "$ncells cell(s), no machine-readable class= declared"
    return 0     # cannot judge L3 rules without it; do not guess
  fi

  # -- profile 5: preconditions recorded (perf cells only)
  if [ "$LEVEL" = "L3" ] && [ "$has_perf" = 1 ]; then
    printf '%s\n' "$body" | grep -q 'irq_source' \
      || note "L3-PRECOND" "$b" "no resident-interrupt-source precondition field"
    printf '%s\n' "$body" | grep -q 'dktcap=' \
      || note "L3-PRECOND" "$b" "capture state (dktcap=) not recorded"
  fi

  # -- 10 (L3) arms paired. Pairing comes from the explicit repeat_of= field,
  # never from a name convention: TAB's TB is an ARM (TILE_BBOX_SKIP), and a
  # `${t}B` heuristic read it as a repeat of a tag that does not exist.
  if [ "$LEVEL" = "L3" ] && [ "$has_perf" = 1 ]; then
    local perftags unpaired=""
    perftags=$(printf '%s\n' "$body" | grep -oE 'cell=[^ ]+ class=perf' | sed 's/cell=//;s/ class=perf//')
    while read -r t; do
      [ -z "$t" ] && continue
      printf '%s\n' "$body" | grep -q "repeat_of=$t" && continue     # something repeats it
      printf '%s\n' "$body" | grep -qE "cell=$t .*repeat_of=" && continue        # it repeats something
      unpaired="$unpaired $t"
    done <<< "$perftags"
    [ -n "$unpaired" ] && note "L3-PAIR" "$b" "perf arm(s) with no repeat:$unpaired"
  fi
}

echo "harness-lint -- level $LEVEL -- $DIR"
echo "========================================================================"
for f in "$DIR"/*.BAT; do
  [ -e "$f" ] || continue
  lint_platform "$f"
  lint_contract "$f"
done

# ------------------------------------------- 6.5 tag uniqueness ACROSS sweeps
echo "------------------------------------------------------------------------"
allsweeps=$(for f in "$DIR"/*.BAT; do
  tr -d '\r' < "$f" | grep -oE 'SET DOSKUTSU_LOG_TAG=[^ ]+' \
    | sed "s|.*=|$(basename "$f")\t|"
done)
cross=$(printf '%s\n' "$allsweeps" | awk -F'\t' 'NF==2{print $2}' | sort | uniq -d)
if [ -n "$cross" ]; then
  for t in $cross; do
    owners=$(printf '%s\n' "$allsweeps" | awk -F'\t' -v T="$t" '$2==T{printf "%s ",$1}')
    note "L1-TAGX" "(cross)" "tag $t used by: $owners"
  done
fi

echo "========================================================================"
if [ "$VIOL" -eq 0 ]; then echo "CLEAN -- $LEVEL conformance"; exit 0; fi
echo "$VIOL violation(s)"; exit 1
