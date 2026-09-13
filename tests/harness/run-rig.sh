#!/usr/bin/env bash
# run-rig.sh -- select a g2k boot profile before driving vcctrl-cell.
#
# doskutsu's real-hardware per-cell driver is already vcctrl's own
# harness/vcctrl-cell -- CFG switch, env SET, launch, guards, log and
# screenshot collection, all in one script, because doskutsu's engine is
# env-var+CFG-configured with no CLI args of its own. (dosags's engine
# takes real CLI args and needed a bespoke RUN.BAT-staging run-rig.sh
# instead -- see that repo's script for why the two engines needed
# different approaches; this is not that.)
#
# What vcctrl-cell does NOT do is pick which g2k CONFIG.SYS boot-menu
# entry the rig is running under. It only WITNESSES the active profile
# after the fact (profiles/doskutsu.yaml's `profile_witness`, e.g. reading
# BLASTER back off the screen) -- it assumes the rig already booted into
# the right one. A cold power-on lands on PGSB (the menu's 5s default);
# anything else (PGADLIB/PGGUS/VIBRA) has had to be selected by hand
# before a cell could be trusted to run against it.
#
# This script closes that gap the same way dosags closed the identical one
# in its own run-rig.sh on 2026-09-13: vcctrl's select_boot_profile()
# (bin/vcctrl_common.py) does the actual reboot-and-pick, tested and
# already relied on by vcctrl-collect's NET-profile switch -- it was just
# never wired into a script for a non-NET profile before dosags did it.
# This wraps the same primitive, then hands off to vcctrl-cell unchanged.
#
# Usage:
#   ./run-rig.sh [--boot-profile N] -- <vcctrl-cell args...>
#
#   --boot-profile N   select g2k CONFIG.SYS menu entry N before launching
#                       vcctrl-cell. Per g2k/README.TXT "MENU ENTRIES" (the
#                       authority -- not this comment) on this rig:
#                         1 PGSB     (default, 5s) PicoGUS Sound Blaster
#                         2 PGADLIB  PicoGUS AdLib (OPL2, music only)
#                         3 PGGUS    PicoGUS Ultrasound (GF1)
#                         4 VIBRA    Vibra16S + PicoGUS USB (CD on D:)
#                         5 NET      transfers only -- never a measured run
#                         6 CLEAN    recovery boot -- no sound, no TSRs
#                       Omit to take whatever profile the rig is already
#                       sitting on (no reboot, no selection attempted).
#
# Everything after `--` is passed to vcctrl-cell verbatim. This script does
# not parse or duplicate vcctrl-cell's own argument surface (TAG, --card,
# --set, --forbid, --ticks, --cfg, --shot-at, --hw -- see `vcctrl-cell -h`).
#
# Requires: the vcctrl repo checked out as a sibling of this repo's clone
# (../vcctrl relative to this repo root), or VCCTRL_BIN pointing at its
# bin/vcctrl directly -- same convention dosags's run-rig.sh uses.
#
# NOT YET SMOKE-TESTED against the real rig -- authored from vcctrl-cell's
# and dosags's run-rig.sh's source directly. First real-hardware run of
# this script is a follow-up, not covered by the commit that adds it.

set -euo pipefail

usage() { echo "Usage: $0 [--boot-profile N] -- <vcctrl-cell args...>" >&2; exit 1; }

BOOT_PROFILE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --boot-profile) BOOT_PROFILE="$2"; shift 2 ;;
        --) shift; break ;;
        -h|--help) usage ;;
        *) echo "run-rig.sh: unexpected arg before -- : $1" >&2; usage ;;
    esac
done
[[ $# -ge 1 ]] || usage

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VCCTRL_BIN="${VCCTRL_BIN:-$REPO_ROOT/../vcctrl/bin/vcctrl}"
VCCTRL_DIR="$(dirname "$VCCTRL_BIN")"
VCCTRL_CELL="$VCCTRL_DIR/../harness/vcctrl-cell"

[[ -f "$VCCTRL_DIR/vcctrl_common.py" ]] || {
    echo "run-rig.sh: cannot find vcctrl_common.py beside $VCCTRL_BIN -- set VCCTRL_BIN" >&2
    exit 1
}
[[ -x "$VCCTRL_CELL" ]] || {
    echo "run-rig.sh: cannot find vcctrl-cell at $VCCTRL_CELL -- set VCCTRL_BIN" >&2
    exit 1
}

if [[ -n "$BOOT_PROFILE" ]]; then
    echo "-- selecting boot profile $BOOT_PROFILE --"
    VCCTRL_DIR="$VCCTRL_DIR" BOOT_DIGIT="$BOOT_PROFILE" python3 <<'PYEOF'
import os, sys
sys.path.insert(0, os.environ["VCCTRL_DIR"])
import vcctrl_common as vcc
vcc.set_lock_owner("run-rig-bootprofile")
res = vcc.select_boot_profile(int(os.environ["BOOT_DIGIT"]), ready_timeout=280)
print("select_boot_profile ->", res)
sys.exit(0 if res else 1)
PYEOF
    echo "   boot profile selected -- vcctrl-cell's own profile_witness check still verifies it landed correctly, same as any other run"
fi

echo "-- handing off to vcctrl-cell --"
exec "$VCCTRL_CELL" "$@"
