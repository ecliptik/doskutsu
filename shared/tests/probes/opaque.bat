@ECHO OFF
REM OPAQUE.BAT - opaque-tile bitmask audit (wave-25 / iter J).
REM Walks data/Stage/PrtCave.pbm and counts colorkey pixels per 16x16
REM tile. Decides FPS-DEEPDIVE candidate #2 ship/no-ship for iter K.
REM Output: OPAQUE.LOG (per-tile counts + opaque bitmask + verdict).
REM Runtime: under 1 sec on PODP83 (pure file parse, no hardware-IO).
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running OPAQUE probe (PrtCave.pbm opaque-tile audit)...
OPAQUE.EXE
ECHO ----
ECHO Probe finished. OPAQUE.LOG contents:
TYPE OPAQUE.LOG
