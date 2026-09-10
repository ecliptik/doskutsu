@ECHO OFF
REM GUSDET.BAT - GF1 read-only detect/report (GUS Campaign 3 #39 task #4).
REM probe-suite. Pure DJGPP; CWSDPMI.EXE same dir. SAFE: read-only peek/poke,
REM NO voice activity (same ops the driver already runs non-wedging on g2k).
REM Confirms the card answers at the ULTRASND base + sizes DRAM before GUSTONE.
ECHO Running GUSDET GF1 detect (writes GUSDET.LOG)...
GUSDET.EXE
ECHO ----
TYPE GUSDET.LOG
