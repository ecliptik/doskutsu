#!/usr/bin/env bash
# cf-clean.sh -- remove accumulated iter debris from the doskutsu CF card.
#
# Two years of diagnostic iters left 147 files and directories on the card
# that no current sweep references: superseded game binaries, one-off probe
# EXEs, retired reels, and the BAT cells of campaigns that closed long ago.
# The list below was DERIVED, not guessed -- it is exactly the set present on
# the card that the kit tarball does not install, minus an explicit keep-list.
#
#   bash cf-clean.sh                 # clean (prints every removal)
#   bash cf-clean.sh --dry-run       # show what WOULD go, touch nothing
#   CF_GAME_DIR=/path bash cf-clean.sh
#
# KEPT deliberately, though also not in the kit:
#   LICENSE.TXT GPLV3.TXT 3RDPARTY.TXT README.TXT
#       The shipped binary is GPLv3. Its licence text stays with it.
#   DOSKUTSU.CFG SETTINGS.DAT
#       Runtime state. DOSKUTSU.CFG is overwritten per cell by the sweep's
#       CFGS copy; SETTINGS.DAT is written by the game.
#
# Anything the kit installs is untouched by construction -- this list contains
# no kit file. Re-run after any populate; it is idempotent.

set -u
CF_GAME_DIR="${CF_GAME_DIR:-/media/micheal/DOS/doskutsu}"
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

[ -d "$CF_GAME_DIR" ] || { echo "cf-clean: no such dir: $CF_GAME_DIR" >&2; exit 1; }

# Sanity gate: refuse to run against a directory that is not the game dir.
# Without this a mistyped CF_GAME_DIR would delete 141 names from wherever it
# happened to point.
if [ ! -f "$CF_GAME_DIR/DOSKUTSU.EXE" ] && [ ! -f "$CF_GAME_DIR/doskutsu.exe" ]; then
  echo "cf-clean: REFUSING -- $CF_GAME_DIR has no DOSKUTSU.EXE, so it is not the game dir." >&2
  exit 1
fi

STALE="
  ADLB.BAT
  ADLIB
  ADLIB.BAT
  BEEP.BAT
  CAVEA.BAT
  CAVE.BAT
  CAVEB.BAT
  CFG
  CFGTEMPO.CFG
  CFGWB.CFG
  CORE.BAT
  DK281.EXE
  DK282.EXE
  DK283.EXE
  DKSTOCK.EXE
  DKWBWIN.EXE
  DMAB.BAT
  DMAC.BAT
  DMAK.BAT
  DOSKUT_O.EXE
  DOSKUTSU.BAK
  DPAD.BAT
  ESCNOTE.BAT
  FBOTH.BAT
  FCANON.BAT
  FMUL.BAT
  FOFF.BAT
  FREL.BAT
  FULL
  FXOFF.BAT
  FXON.BAT
  G4428.BAT
  G4432.BAT
  GMUI.BAT
  GOLD.EXE
  GUS
  GUS17.TXT
  GUS5A.BAT
  GUS68.BAT
  GUS6D.BAT
  GUS7N.BAT
  GUS8B0.BAT
  GUS8B.BAT
  GUS8C.BAT
  GUSAB.BAT
  GUS.BAT
  GUSDET.BAT
  GUSDET.EXE
  GUSDMA.BAT
  GUSDUMP.BAT
  GUSDUMP.EXE
  GUSG1.BAT
  GUSG2.BAT
  GUSIN44.BAT
  GUSINIT.BAT
  GUSM.BAT
  GUSNAME.BAT
  GUSPROBE.TXT
  GUSRSV.BAT
  GUSS.BAT
  GUSSFX.BAT
  GUSSFX.EXE
  GUSTEST.BAT
  GUSTONE.BAT
  GUSTONE.EXE
  GV14.BAT
  GV32.BAT
  MDEMO.BAT
  MFIX.BAT
  MIDIO.BAT
  MIDIW.BAT
  MOFF.BAT
  OPNOTES.TXT
  ORG.BAT
  ORGD0.BAT
  ORGD1.BAT
  ORGPRB.BAT
  ORGRV.BAT
  PCSPK.BAT
  PCSPKPWM.BAT
  PCSPKPWM.EXE
  PCTONE.BAT
  PLAY0.BAT
  PLAY10.BAT
  PLAY11.BAT
  _PLAY1.BAT
  PLAY1.BAT
  _PLAY2.BAT
  PLAY2.BAT
  _PLAY3.BAT
  PLAY3.BAT
  _PLAY4.BAT
  PLAY4.BAT
  PLAY5.BAT
  PLAY6.BAT
  PLAY7.BAT
  PLAY8.BAT
  PLAY9.BAT
  PLAYC14.BAT
  PLAYC28.BAT
  PLAYG44.BAT
  PLAYGUS.BAT
  PRECACHE.BAT
  PROFILE3.DAT
  PROFILE5.DAT
  PSPK0.BAT
  QA281.TAS
  QA282.TAS
  READADL.TXT
  READV161.TXT
  READWBG.TXT
  REC281.BAT
  REEL
  RP283.BAT
  RT282.BAT
  RUN
  RUN.BAT
  SB
  SCRA.BAT
  SCRB.BAT
  SDESC.BAT
  SETA.BAT
  SETGUS.BAT
  SETSB.BAT
  SETV.BAT
  TAS0.BAT
  TAS1.BAT
  TONE16.BAT
  TONE8.BAT
  TONEFIX.BAT
  V14.BAT
  V16.BAT
  V20.BAT
  V24.BAT
  V27.BAT
  V28.BAT
  V29.BAT
  V32.BAT
  WB.BAT
  WBHOT.EXE
  WGUS18.TXT
"

n=0; bytes=0
for f in $STALE; do
  p="$CF_GAME_DIR/$f"
  [ -e "$p" ] || continue
  sz=$(du -sk "$p" 2>/dev/null | cut -f1); sz=${sz:-0}
  bytes=$((bytes + sz)); n=$((n + 1))
  if [ "$DRY" = "1" ]; then
    printf '  would remove  %-16s %6s KB\n' "$f" "$sz"
  else
    rm -rf -- "$p" && printf '  removed       %-16s %6s KB\n' "$f" "$sz"
  fi
done

echo
if [ "$DRY" = "1" ]; then
  echo "cf-clean: DRY RUN -- $n entries, $((bytes / 1024)) MB would be freed. Nothing changed."
else
  echo "cf-clean: removed $n entries, freed $((bytes / 1024)) MB from $CF_GAME_DIR"
fi
echo "cf-clean: kit files and LICENSE/GPLV3/3RDPARTY/README/DOSKUTSU.CFG/SETTINGS.DAT untouched."
