#!/bin/bash
# Apply the QA lane instrumentation to the BATs already on the CF.
# BAT-only: no engine source, no Organya cache key, no re-populate needed.
# Idempotent, CRLF-preserving, backs up to .BAK, syncs and unmounts.
set -u
CF="${1:-/media/micheal/DOS}"
D="$CF/doskutsu"; [ -d "$D" ] || D="$CF/DOSKUTSU"
[ -d "$D" ] || { echo "FIX_FAILED_REASON: no doskutsu dir under $CF"; exit 1; }
command -v python3 >/dev/null || { echo "FIX_FAILED_REASON: python3 not found on this laptop"; exit 1; }
[ -f /tmp/qa-instrument.py ] || { echo "FIX_FAILED_REASON: /tmp/qa-instrument.py missing; scp it first"; exit 1; }
python3 /tmp/qa-instrument.py "$D" || { echo "FIX_FAILED_REASON: instrumentation failed, files left as-is"; exit 1; }
echo "--- verify ---"
for b in PG VB VIDV VIDC VIDM; do
  [ -f "$D/$b.BAT" ] || continue
  n=$(grep -c "QA-INSTRUMENT" "$D/$b.BAT")
  d=$(grep -c $'\r\r' "$D/$b.BAT")
  echo "  $b.BAT markers=$n doubleCR=$d"
  [ "$d" -eq 0 ] || { echo "FIX_FAILED_REASON: $b.BAT double-CR gate tripped"; exit 1; }
done
sync
if mountpoint -q "$CF"; then
  udisksctl unmount -b "$(findmnt -no SOURCE "$CF")" 2>/dev/null || umount "$CF" 2>/dev/null \
    || echo "WARN: could not unmount $CF -- unmount manually before pulling the card"
fi
echo "FIX_OK: sweeps now print a CPU/hardware banner and write LOGS\\<M><SWEEP>.NFO"
echo "        WaveBlaster cell is 17P in PG and 17V in VB -- they no longer collide."
