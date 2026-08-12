#!/bin/sh
#
# prove-verdict.sh -- adjudicate a PROVE.BAT run.
#
# Collecting logs is not proving anything. This reads a pulled PROVE sweep and
# returns a PASS/FAIL verdict per CHANGE, so the question "is the round-2
# binary good on real hardware" gets an answer instead of a pile of numbers.
#
# Usage:
#   tests/qa/prove-verdict.sh <pulled-log-dir> [banked-round1-csv]
#
# <pulled-log-dir> is what logback-qa.sh produced, e.g.
#   /tmp/qa-v163/prove-G/G/     (cells <M>P4 <M>P4B <M>P5 <M>P5K <M>P3 <M>P0 <M>P8)
# [banked-round1-csv] defaults to qa-results/ROUND1-CELLS.csv and is used for
# the re-baseline check; omit it to skip that one check.
#
# Exit 0 = every claim the logs can settle PASSES. Exit 1 = at least one FAIL.
# A claim that cannot be settled from the given logs reports SKIP, never PASS --
# an absent witness is an absence, not a pass.

set -u
DIR=${1:-}
CSV=${2:-qa-results/ROUND1-CELLS.csv}
[ -n "$DIR" ] && [ -d "$DIR" ] || { echo "usage: $0 <pulled-log-dir> [banked-csv]" >&2; exit 2; }

FAILS=0
pass()  { printf '  PASS  %s\n' "$*"; }
fail()  { printf '  FAIL  %s\n' "$*"; FAILS=$((FAILS+1)); }
skip()  { printf '  SKIP  %s\n' "$*"; }
head_() { printf '\n== %s\n' "$*"; }

# Resolve the machine prefix from whichever P4 cell is present.
P4=$(ls "$DIR"/*P4.LOG 2>/dev/null | head -1)
[ -n "$P4" ] || { echo "no <M>P4.LOG in $DIR -- is this a PROVE pull?" >&2; exit 2; }
M=$(basename "$P4" | sed 's/P4\.LOG$//')
echo "PROVE verdict -- machine prefix '$M', dir $DIR"

cell()    { echo "$DIR/${M}$1.LOG"; }
sdlcell() { echo "$DIR/${M}$1SDL.LOG"; }
have()    { [ -s "$1" ]; }

# inter_flip histogram: the 10-40 ms band is where real frames live on this
# hardware. Prints the count in that band.
band_10_40() {
  grep "flip-probe: flip" "$1" 2>/dev/null \
    | grep -oE 'inter_flip_ms=[0-9]+' | cut -d= -f2 \
    | awk '{ if ($1>=10 && $1<40) n++ } END { print n+0 }'
}
field() { grep -oE "^$2=.*" "$1" 2>/dev/null | head -1 | cut -d= -f2-; }
mfield() { grep -oE "$2=[^ ]+" "$1" 2>/dev/null | head -1 | cut -d= -f2-; }

# ---------------------------------------------------------------- provenance
head_ "0293 RUNMANIFEST schema v2 -- provenance"
C=$(cell P4)
if have "$C"; then
  sv=$(mfield "$C" schema_version)
  [ "$sv" = "2" ] && pass "schema_version=2" || fail "schema_version='$sv' (want 2)"
  b=$(mfield "$C" build_sha12); o=$(mfield "$C" organya_cache_key)
  if [ -n "$b" ] && [ -n "$o" ]; then
    # They are equal BY CONSTRUCTION today (cache key derives from the source
    # fingerprint). What must hold is that BOTH FIELDS EXIST, so a future
    # divergence is representable instead of silently aliased.
    pass "build_sha12 and organya_cache_key both emitted ($b / $o)"
  else
    fail "build_sha12='$b' organya_cache_key='$o' -- both fields must exist"
  fi
  bs=$(mfield "$C" binary_sha12); bsrc=$(mfield "$C" binary_sha12_src)
  case "$bs" in
    ""|UNKNOWN) fail "binary_sha12=$bs -- the kit DOSKUTSU.SHA sidecar has NOT landed; provenance is unproven" ;;
    *)          pass "binary_sha12=$bs (src=$bsrc)" ;;
  esac
  st=$(mfield "$C" started_rtc_local)
  [ -n "$st" ] && pass "started_rtc_local=$st (no false UTC claim)" || fail "started_rtc_local missing"
else
  skip "no P4 cell -- provenance unchecked"
fi

# ------------------------------------------------------------- CPU attestation
head_ "0294 CPU witness -- the gap that made every cross-CPU claim unverifiable"
if have "$C"; then
  cw=$(grep -o '\[cpu-witness\].*' "$C" 2>/dev/null | head -1)
  if [ -n "$cw" ]; then
    pass "$cw"
    NFO=$(ls "$DIR"/*PR.NFO 2>/dev/null | head -1)
    if [ -n "$NFO" ]; then
      dec=$(field "$NFO" cpu)
      mhz=$(mfield "$C" cpu_mhz_est)
      echo "        declared (.NFO): $dec   |   measured: mhz_est=$mhz"
      echo "        ^ compare by hand: a mismatch means the wrong sweep arg was"
      echo "          passed and EVERY number in this run is mislabelled."
    else
      skip "no .NFO manifest -- cannot cross-check declared vs measured CPU"
    fi
  else
    fail "no [cpu-witness] line (INFO-level: is the run tagged?)"
  fi
fi

# ------------------------------------------------------------------ per-stage
head_ "0295 per-stage fps"
if have "$C"; then
  pc=$(mfield "$C" perstage_count)
  if [ -n "$pc" ] && [ "$pc" -ge 11 ] 2>/dev/null; then
    pass "perstage_count=$pc (full 11-stage route)"
  else
    fail "perstage_count='$pc' -- want >=11; a short count means a truncated route"
  fi
fi

# --------------------------------------------------------- MIDI truncation fix
head_ "org2mid loop_reps -- no track may exceed the 16384 ISR buffer"
hits=$(grep -l "ISR buffer is 16384" "$DIR"/*.LOG 2>/dev/null | wc -l)
if [ "$hits" -eq 0 ]; then
  pass "no truncation warning in any cell (the reps=1 sets are deployed)"
else
  fail "$hits cell(s) still warn -- the payload carries OLD reps=4 .mid files."
  echo "        Cause: 'make convert-music' was skipped on the kit rebuild."
  echo "        The .mid sets are gitignored build products; the committed fix"
  echo "        is the converter default, so a pack without that step ships"
  echo "        the old files and NOTHING in the repo reveals it."
fi

# ------------------------------------------------------------------ DAC width
head_ "SDL/0123 DAC palette width -- the Cirrus-vs-S3 question"
DW=$(grep -h -o 'DOSVESA-DACWIDTH:.*' "$DIR"/*SDL.LOG 2>/dev/null | head -1)
if [ -n "$DW" ]; then
  pass "$DW"
  w=$(echo "$DW" | grep -oE 'width_bits=-?[0-9]+' | cut -d= -f2)
  case "$w" in
    6)  echo "        6-bit: matches our 0-63 writes. Palette correct ON THIS CARD." ;;
    8)  echo "        *** 8-bit with 6-bit writes = quarter-range = the washed-out card."
        echo "        Consistency check: this card MUST be the one that looks worse." ;;
    -1) echo "        BIOS lacks 4F08. Unknowable here -- an absence, not a pass." ;;
  esac
  echo "        Run this sweep once per VIDEO CARD; the verdict needs the pair."
else
  fail "no DOSVESA-DACWIDTH line -- 0123 is REQUIRED and fires at every mode set"
fi

# ------------------------------------------------- SDL/0124 identity vs path
head_ "SDL/0124 -- card identity, override and DMA path must be SEPARABLE"
P8=$(cell P8)
if have "$P8"; then
  id=$(mfield "$P8" audio_is_sb16); fo=$(mfield "$P8" audio_force_8bit); dp=$(mfield "$P8" audio_dma_path)
  echo "        P8 (forced 8-bit): is_sb16=$id force_8bit=$fo dma_path=$dp"
  if [ "$fo" = "1" ] && [ "$id" = "1" ]; then
    pass "a real SB16 forced to 8-bit still records AS an SB16 -- identity survived the override"
  elif [ "$fo" = "1" ] && [ "$id" = "0" ]; then
    fail "identity collapsed into the path decision -- this is the exact bug 0124 split apart"
  else
    skip "P8 not on a Vibra (force_8bit=$fo) -- meaningless off that card"
  fi
else
  skip "no P8 cell"
fi

# ------------------------------------------------ 0122 timebase A/B on silicon
head_ "SDL/0122 timebase -- THE A/B, and the only test DOSBox structurally cannot do"
P5=$(cell P5); P5K=$(cell P5K)
if have "$P5" && have "$P5K"; then
  on=$(band_10_40 "$P5"); off=$(band_10_40 "$P5K")
  st_on=$(mfield "$P5" pump_clock_state); st_off=$(mfield "$P5K" pump_clock_state)
  echo "        fix ON  : 10-40ms band = $on   pump_clock_state=$st_on"
  echo "        fix OFF : 10-40ms band = $off   pump_clock_state=$st_off"
  if [ "$off" -eq 0 ] 2>/dev/null && [ "$on" -gt 0 ] 2>/dev/null; then
    pass "band EMPTY with the fix off and POPULATED with it on -- the staircase is real and the fix removes it"
  else
    fail "expected off=0 and on>0; got off=$off on=$on"
  fi
  [ "$st_on" = "pump-timebase-ok" ] && pass "pump_clock_state=pump-timebase-ok" \
    || fail "pump_clock_state='$st_on' (want pump-timebase-ok)"
  [ "$st_off" = "pump-uncorrected" ] && pass "killswitch arm reports pump-uncorrected" \
    || fail "killswitch arm reports '$st_off' (want pump-uncorrected)"
  TD=$(grep -h -o 'pump teardown -- .*' "$(sdlcell P5)" 2>/dev/null | head -1)
  if [ -n "$TD" ]; then
    d=$(echo "$TD" | grep -oE 'delta=-?[0-9]+' | cut -d= -f2)
    if [ "$d" = "0" ]; then
      pass "BIOS-tick chain intact on MODE 2 real silicon: delta=0"
    else
      fail "delta=$d -- the pump is LOSING BIOS ticks on MODE 2; DOS time-of-day drifts"
    fi
  else
    skip "no teardown line -- the cell did not reach a clean shutdown"
  fi
else
  skip "need both P5 and P5K for the A/B"
fi

# -------------------------------------------------- noise floor + re-baseline
head_ "Noise floor and re-baseline"
P4B=$(cell P4B)
per_loop() { f=$(mfield "$1" per_loop_fps); [ -n "$f" ] && echo "$f" || echo ""; }
a=$(per_loop "$C"); b=$(per_loop "$P4B")
if [ -n "$a" ] && [ -n "$b" ]; then
  nf=$(awk -v x="$a" -v y="$b" 'BEGIN{d=x-y; if(d<0)d=-d; printf "%.2f", d}')
  pass "noise floor = |$a - $b| = $nf fps (P4 vs P4B, back-to-back)"
  echo "        Every 'inside the noise' claim from here on cites THIS number,"
  echo "        not the 0.22 that came from an accidental duplicate."
  if [ -f "$CSV" ]; then
    case "$M" in G) cpu="POD-83";; A) cpu="Am5x86-133";; 6) cpu="486DX2-66";; 5) cpu="486DX2-50";; *) cpu="";; esac
    if [ -n "$cpu" ]; then
      old=$(awk -F, -v c="$cpu" '$2==c && $1 ~ /C4$/ {print $8; exit}' "$CSV")
      if [ -n "$old" ]; then
        dl=$(awk -v x="$a" -v y="$old" 'BEGIN{d=x-y; if(d<0)d=-d; printf "%.2f", d}')
        if awk -v d="$dl" -v n="$nf" 'BEGIN{exit !(d<=n+0.5)}'; then
          pass "re-baseline: P4=$a vs banked C4=$old, delta=$dl -- within noise; the 89 banked cells carry forward"
        else
          fail "re-baseline: P4=$a vs banked C4=$old, delta=$dl EXCEEDS the noise floor -- the new binary moved the anchor; quantify before trusting any banked comparison"
        fi
      else
        skip "no banked C4 row for $cpu in $CSV"
      fi
    fi
  else
    skip "no banked CSV at $CSV -- re-baseline unchecked"
  fi
else
  skip "per_loop_fps missing from P4/P4B"
fi

# ----------------------------------------------------------------- route check
head_ "Route completeness (a short route invalidates the cell's route, not its fps)"
for t in P4 P4B P5 P5K P3 P0 P8; do
  L=$(cell "$t"); have "$L" || continue
  n=$(grep -c "Entering stage" "$L" 2>/dev/null)
  if [ "$n" -ge 11 ]; then printf '  PASS  %s%s route %s stages\n' "$M" "$t" "$n"
  else printf '  FAIL  %s%s route only %s stages (truncated)\n' "$M" "$t" "$n"; FAILS=$((FAILS+1)); fi
done

printf '\n============================================================\n'
if [ "$FAILS" -eq 0 ]; then
  echo "VERDICT: PASS -- every claim these logs can settle holds."
  echo "SKIPs above are unsettled, NOT passed. Read them before shipping."
  exit 0
else
  echo "VERDICT: $FAILS FAILURE(S) -- do not ship until each is understood."
  exit 1
fi
