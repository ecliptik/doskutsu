@ECHO OFF
REM TILEPROB.BAT - visit-loop overhead probe (wave-22.5 / iter H).
REM Measures cost of iterating 30000 tiles vs 1900 tiles in tight loop.
REM Output: TILEPROB.LOG (RDTSC cycle counts + ms-converted summary).
REM Runtime: ~1-3 sec on PODP83. No hang risk (pure RAM access).
REM No env vars required. Standalone DJGPP probe; CWSDPMI.EXE same dir.
ECHO Running TILEPROB visit-loop overhead probe (writes TILEPROB.LOG, ~1-3 sec)...
TILEPROB.EXE
ECHO ----
ECHO Probe finished. TILEPROB.LOG contents:
TYPE TILEPROB.LOG
