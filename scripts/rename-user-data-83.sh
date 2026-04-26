#!/usr/bin/env bash
#
# rename-user-data-83.sh — rename user-extracted Cave Story content files
# to fit MS-DOS 8.3.
#
# The user's `data/` tree (typically populated by doukutsu-rs / NXExtract /
# cavestory.one per docs/ASSETS.md) ships several files under canonical
# Cave Story names that exceed DOS 8.3:
#
#   Stage/PrtAlmond.pbm  -> Stage/PrtAlmnd.pbm   (9->8 char stem)
#   Stage/Almond.pxa     -> Stage/Almnd.pxa     (driven by tileset_names[14]
#                                                rename in patch 0033)
#   Npc/NpcBallos.pbm    -> Npc/NpcBalls.pbm
#   Npc/NpcIsland.pbm    -> Npc/NpcIslnd.pbm
#   Npc/NpcPriest.pbm    -> Npc/NpcPrst.pbm
#   Npc/NpcStream.pbm    -> Npc/NpcStrm.pbm
#   ArmsImage.pbm        -> ArmImg.pbm
#   ItemImage.pbm        -> ItmImg.pbm
#   StageImage.pbm       -> StgImg.pbm
#   StageSelect.tsc      -> StgSel.tsc
#
# It also deletes the widescreen-only backdrops that the engine never opens
# on DOS (bk*480fix.pbm — the source path map.cpp:560 is gated on
# `widescreen` which patch 0005-renderer-lock-320x240-fullscreen forces to
# false). Same exclusion that `make dist` and `make install` apply per
# Makefile:706; mirroring it here keeps the dev-side data/ tree consistent
# with the shipping artifacts. These files have long names (e.g.
# bkFog480fix.pbm = 11-char stem) and would otherwise be the only 8.3
# violators left in the user's tree post-rename.
#
# These complement the engine-side renames in patches 0033/0034/0035 and the
# extractor renames in scripts/extract-engine-data.py + scripts/rename-sif.py.
#
# Idempotent: running on an already-renamed tree is a no-op (each rename is
# guarded with a "source exists, destination doesn't" check; deletes are
# guarded with `-f` and "file exists" check).
#
# Usage:
#   scripts/rename-user-data-83.sh [<data-dir>]
#
#   <data-dir>: defaults to ./data/ in the repo root. Pass an absolute path
#               (or the staged DATA path on a CF card) to rename in place.

set -euo pipefail

DATA="${1:-data}"

if [[ ! -d "$DATA" ]]; then
    echo "rename-user-data-83.sh: data dir '$DATA' does not exist" >&2
    echo "  (extract Cave Story content per docs/ASSETS.md first)" >&2
    exit 1
fi

renamed=0
skipped=0
deleted=0

delete_dead_widescreen() {
    # Remove bk*480fix.pbm widescreen backdrops; never opened on DOS per
    # patch 0005's widescreen-locked-false. Mirrors Makefile:706's dist-target
    # exclusion so dev-side `make stage` matches shipping artifact contents.
    local pattern
    for pattern in "$DATA"/bk*480fix.pbm; do
        if [[ -f "$pattern" ]]; then
            rm -f -- "$pattern"
            echo "  deleted $(basename "$pattern") (widescreen-only, dead code on DOS)"
            deleted=$((deleted + 1))
        fi
    done
}

rename_one() {
    local src="$1"
    local dst="$2"
    if [[ -f "$DATA/$src" && ! -e "$DATA/$dst" ]]; then
        mv -- "$DATA/$src" "$DATA/$dst"
        echo "  $src -> $dst"
        renamed=$((renamed + 1))
    elif [[ -f "$DATA/$dst" ]]; then
        skipped=$((skipped + 1))
    fi
    # neither exists: silently skip (e.g. user has incomplete extract)
}

rename_one "wavetable.dat"         "wavetbl.dat"
rename_one "Stage/PrtAlmond.pbm"   "Stage/PrtAlmnd.pbm"
rename_one "Stage/Almond.pxa"      "Stage/Almnd.pxa"
rename_one "Npc/NpcBallos.pbm"     "Npc/NpcBalls.pbm"
rename_one "Npc/NpcIsland.pbm"     "Npc/NpcIslnd.pbm"
rename_one "Npc/NpcPriest.pbm"     "Npc/NpcPrst.pbm"
rename_one "Npc/NpcStream.pbm"     "Npc/NpcStrm.pbm"
rename_one "ArmsImage.pbm"         "ArmImg.pbm"
rename_one "ItemImage.pbm"         "ItmImg.pbm"
rename_one "StageImage.pbm"        "StgImg.pbm"
rename_one "StageSelect.tsc"       "StgSel.tsc"

delete_dead_widescreen

echo "rename-user-data-83.sh: $renamed renamed, $skipped already-8.3, $deleted deleted (widescreen) in '$DATA'"
