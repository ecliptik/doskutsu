#!/bin/bash
# Add a MACH64 boot profile to the CF and install ATI's VESA TSR.
# Keeps all five existing profiles untouched. Idempotent. Backs up.
set -u
CF="${1:-/media/micheal/DOS}"
[ -d "$CF" ] || { echo "FAIL: $CF not mounted"; exit 1; }
SRC="$(cd "$(dirname "$0")" && pwd)"

# 1. ATI VESA TSR + the mode-list tool
D="$CF/ATI/SUPPORT/64VBE221"
mkdir -p "$D"
cp "$SRC/M64VBE.COM" "$SRC/VESATEST.EXE" "$SRC/README.TSC" "$D/"
echo "  installed M64VBE.COM + VESATEST.EXE -> C:\\ATI\\SUPPORT\\64VBE221\\"

# 2. boot files -- back up whatever is there first
VER="MACH64-PROFILE v2"
for f in AUTOEXEC.BAT CONFIG.SYS; do
  if [ -f "$CF/$f" ]; then
    if cmp -s "$CF/$f" "$SRC/$f.new"; then echo "  $f already current, leaving it"; continue; fi
    if grep -qi "MACH64" "$CF/$f"; then
      # A profile is present but is not the current one -- an earlier version,
      # or a hand edit. Updating is the point of re-running this; keep both.
      echo "  $f has an OUTDATED MACH64 profile -- updating it"
      grep -q "$VER" "$CF/$f" 2>/dev/null || echo "    (card has a pre-$VER profile)"
    fi
    # .PREM64 is the pristine pre-Mach64 original -- write it once, never again
    if [ -f "$CF/$f.PREM64" ]; then cp "$CF/$f" "$CF/$f.BAK"; echo "    previous -> $f.BAK"
    else cp "$CF/$f" "$CF/$f.PREM64"; echo "    original -> $f.PREM64"; fi
  fi
  cp "$SRC/$f.new" "$CF/$f"
  t=$(wc -l < "$CF/$f"); c=$(grep -c $'\r$' "$CF/$f")
  [ "$t" = "$c" ] || { echo "FAIL: $f not CRLF ($c/$t)"; exit 1; }
  echo "  installed $f ($t lines, CRLF)"
done
sync
echo
echo "FIX_OK: boot menu now has a 6th entry -- 'Mach64 video test (M64VBE, no UniVBE)'."
echo "        The other five profiles are unchanged and still load UniVBE."
echo "        Pick entry 6 at boot for Mach64 testing; any other entry is normal operation."
