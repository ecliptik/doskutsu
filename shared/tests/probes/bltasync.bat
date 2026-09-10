@ECHO OFF
REM BLTASYNC.BAT - Cirrus 5434 BLT async parallelism probe (wave-36 task #10).
REM Gates hail-mary slot 0133: does the BLT engine let the CPU do parallel work?
REM Output: BLTASYNC.LOG. Runtime: about 30-60 sec on PODP83 (100 reps * 3 scenarios).
REM HAZARD: enters VBE 8bpp mode; screen looks corrupted during BLT scenarios;
REM restores text mode before exit.
REM No env vars required. Pure DJGPP; CWSDPMI.EXE must be in same dir.
REM Decision gate at end of log: VERDICT=SHIP|DEFER|CANCEL|REFUTE_*.
ECHO Running BLTASYNC probe (Cirrus BLT async parallelism)...
BLTASYNC.EXE
ECHO ----
ECHO Probe finished. BLTASYNC.LOG tail:
TYPE BLTASYNC.LOG
