@ECHO OFF
REM MODE13H.BAT - Mode 13h packed-pixel bandwidth probe (wave-36 ceiling-bust B).
REM Tests whether bank-switch overhead OR per-byte CPU PIO is the dominant
REM dosmemput-bandwidth ceiling on g2k. Mode 13h's single-bank 320x200x8
REM FB at 0xA000:0 isolates the bank-switch axis.
REM Output: MODE13H.LOG.
REM Runtime: under 5 sec on PODP83 (3 scenarios x 100 reps each, 64000 B/op).
REM HAZARD: enters Mode 13h; screen shows garbage during scenarios; restores
REM text mode before exit.
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running MODE13H probe (Mode 13h vs banked dosmemput)...
MODE13H.EXE
ECHO ----
ECHO Probe finished. MODE13H.LOG tail:
TYPE MODE13H.LOG
