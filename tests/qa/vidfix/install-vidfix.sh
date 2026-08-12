#!/bin/bash
# Refresh just the three video-lane BATs on the CF. No re-populate needed.
set -u
CF="${1:-/media/micheal/DOS}"
D="$CF/doskutsu"; [ -d "$D" ] || D="$CF/DOSKUTSU"
[ -d "$D" ] || { echo "FIX_FAILED_REASON: no doskutsu dir under $CF"; exit 1; }
SRC="$(cd "$(dirname "$0")" && pwd)"
for b in VIDV.BAT VIDC.BAT VIDM.BAT; do
  [ -f "$SRC/$b" ] || { echo "FIX_FAILED_REASON: $b missing beside this script"; exit 1; }
  if cmp -s "$D/$b" "$SRC/$b"; then echo "  $b already current"; continue; fi
  [ -f "$D/$b" ] && cp "$D/$b" "$D/$b.BAK"
  cp "$SRC/$b" "$D/$b"
  t=$(wc -l < "$D/$b"); c=$(grep -c $'\r$' "$D/$b")
  [ "$t" = "$c" ] || { echo "FIX_FAILED_REASON: $b not CRLF ($c/$t)"; exit 1; }
  echo "  updated $b ($t lines, CRLF)"
done
sync
if mountpoint -q "$CF"; then
  udisksctl unmount -b "$(findmnt -no SOURCE "$CF")" 2>/dev/null || umount "$CF" 2>/dev/null \
    || echo "WARN: could not unmount $CF"
fi
echo "FIX_OK: video lanes no longer assume a PicoGUS."
echo "        VIDM 4      -> uses the SB in the box (Vibra)"
echo "        VIDM 4 PG   -> switches a PicoGUS to SB mode"
