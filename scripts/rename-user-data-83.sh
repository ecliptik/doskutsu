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
# These complement the engine-side renames in patches 0033/0034/0035 and the
# extractor renames in scripts/extract-engine-data.py + scripts/rename-sif.py.
#
# Idempotent: running on an already-renamed tree is a no-op (each rename is
# guarded with a "source exists, destination doesn't" check).
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

echo "rename-user-data-83.sh: $renamed renamed, $skipped already-8.3 in '$DATA'"
