#!/usr/bin/env bash
# cf-update-bats.sh -- put the round-2 QA BATs on an already-populated CF.
#
# The round-2 BATs (PROVE / RB / GAP / EAR) are NEWER than the kit tarball, so
# a card populated by install-qa-v163.sh does not have them. This copies them
# on without a repack, which is the whole point: a full payload rebuild costs
# a ~40 min Organya re-render and a CF re-populate, and none of that is needed
# to fix a BAT.
#
#   Operator runs:  scp claude:/tmp/cf-update-bats.sh /tmp/ && bash /tmp/cf-update-bats.sh
#
# Self-fetches claude:/tmp/qa-bats.tar.gz, same pattern as install-qa-v163.sh.
# Idempotent -- re-run it after any BAT change; it just overwrites.
#
# WHAT THIS DOES NOT DO: it does not change the game binary. If the card holds
# the round-1 binary, these BATs will happily run and measure the OLD engine.
# PROVE.BAT exists to prove round-2 changes, so that result would be green and
# meaningless. Check the binary before trusting a prove-out.

set -u
CF_MOUNT="${CF_MOUNT:-/media/micheal/DOS}"
CF_GAME_DIR="${CF_GAME_DIR:-${CF_MOUNT}/doskutsu}"
SRC_HOST="${SRC_HOST:-claude}"
TARBALL="qa-bats.tar.gz"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "[1/5] verify the CF is mounted and is the game dir"
if [ ! -d "$CF_GAME_DIR" ]; then
  echo "  ERROR: no such directory: $CF_GAME_DIR"
  echo "  Is the CF mounted? Expected mount: $CF_MOUNT"
  exit 1
fi
if [ ! -f "$CF_GAME_DIR/DOSKUTSU.EXE" ]; then
  echo "  ERROR: $CF_GAME_DIR has no DOSKUTSU.EXE -- refusing to write to it."
  exit 1
fi
echo "  ok: $CF_GAME_DIR"

echo "[2/5] fetch the BAT bundle"
# SRC_TARBALL lets an operator who already has the bundle skip the fetch --
# also the only way to run this with no ssh to the build host.
if [ -n "${SRC_TARBALL:-}" ]; then
  [ -f "$SRC_TARBALL" ] || { echo "  ERROR: SRC_TARBALL not found: $SRC_TARBALL"; exit 1; }
  cp -f "$SRC_TARBALL" "$TMP/${TARBALL}"
  echo "  using local bundle: $SRC_TARBALL"
elif ! scp -q "${SRC_HOST}:/tmp/${TARBALL}" "$TMP/"; then
  echo "  ERROR: could not fetch ${SRC_HOST}:/tmp/${TARBALL}"
  echo "  (or pass SRC_TARBALL=/path/to/${TARBALL} to use a local copy)"
  exit 1
fi
tar xzf "$TMP/${TARBALL}" -C "$TMP" || { echo "  ERROR: bundle did not extract"; exit 1; }
echo "  ok: $(cd "$TMP" && ls *.BAT | tr '\n' ' ')"

echo "[3/5] CRLF + ASCII gate (a stray LF-only BAT fails on DOS 6.22)"
bad=0
for f in "$TMP"/*.BAT; do
  b=$(basename "$f")
  cr=$(tr -dc '\r' < "$f" | wc -c); lf=$(tr -dc '\n' < "$f" | wc -c)
  dcr=$(grep -c $'\r\r' "$f" 2>/dev/null || true)
  # Count NON-ASCII BYTES by deletion, not by regex: a bracket expression like
  # [^\x00-\x7F] is interpreted differently by GNU grep, BSD grep and ugrep,
  # and this runs on the operator's laptop rather than the build host. Deleting
  # the ASCII range and measuring what survives is unambiguous everywhere.
  na=$(LC_ALL=C tr -d '\000-\177' < "$f" | wc -c)
  if [ "$cr" -ne "$lf" ] || [ "${dcr:-0}" -ne 0 ] || [ "${na:-0}" -ne 0 ]; then
    echo "  FAIL $b: cr=$cr lf=$lf double-cr=$dcr non-ascii=$na"; bad=1
  else
    echo "  ok   $b: $cr lines, CRLF clean, ASCII clean"
  fi
done
[ "$bad" -eq 0 ] || { echo "  ERROR: refusing to copy a malformed BAT to the card."; exit 1; }

echo "[4/5] copy onto the CF"
for f in "$TMP"/*.BAT; do
  cp -f "$f" "$CF_GAME_DIR/" && echo "  wrote $(basename "$f")"
done

echo "[5/5] verify on-card copies match"
fail=0
for f in "$TMP"/*.BAT; do
  b=$(basename "$f")
  if cmp -s "$f" "$CF_GAME_DIR/$b"; then echo "  ok   $b"; else echo "  FAIL $b differs after copy"; fail=1; fi
done
sync

echo
if [ "$fail" -eq 0 ]; then
  echo "UPDATE_OK: round-2 BATs are on the card at $CF_GAME_DIR"
  echo "  Run order on the box:  QA n   then   PROVE   then   EXIT"
  echo "  n = 1 POD-83 / 2 Am5x86-133 / 3 DX2-66 / 4 DX2-50"
  echo
  echo "  PROVE needs the PicoGUS -- it switches the card sb -> adlib itself,"
  echo "  because the music pump cannot start while a Sound Blaster is hot."
  echo
  echo "  The CF is left MOUNTED. Unmount when you are ready to move it."
else
  echo "UPDATE_FAILED_REASON: at least one file did not match after copying."
  exit 1
fi
