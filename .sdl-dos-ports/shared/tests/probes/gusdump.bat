@ECHO OFF
REM GUSDUMP.BAT - GF1 bounded register snapshot (GUS Campaign 3 #39 task #4).
REM probe-suite. Pure DJGPP; CWSDPMI.EXE same dir.
REM Own-init mode (default): brings up the GF1 with NO voice started, then reads
REM each register ONCE (bounded single pass) == safe under the diag-wedge rule.
REM Diff GUSDUMP.LOG against research-supplied known-good values.
REM
REM NOINIT mode (GUSDUMP.EXE NOINIT=1): snapshots prior GF1 state. ONLY run that
REM AFTER MIDIDEMO has fully EXITED and the card is idle -- never while a note
REM is sustaining (read-concurrent-with-active-voice == the PicoGUS wedge).
ECHO Running GUSDUMP GF1 register snapshot (writes GUSDUMP.LOG)...
GUSDUMP.EXE
ECHO ----
TYPE GUSDUMP.LOG
