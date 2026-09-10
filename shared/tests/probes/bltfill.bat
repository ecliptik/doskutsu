@ECHO OFF
REM BLTFILL.BAT - Cirrus 5434 BLT solid-fill vs dosmemput v2 (wave-27/iter K).
REM v2 corrects 3 register-encoding bugs from iter J v1; tries 3-mode
REM fallback ladder COLOR_EXPAND then PATTERN_COPY then BULK_COPY.
REM Output: BLTFILL.LOG. Companion: CHIPID.EXE forensic dump.
REM Runtime: about 6 sec on PODP83 (mode set + 100 reps each scenario).
REM HAZARD: enters VBE 8bpp mode; screen looks corrupted during BLT
REM scenario; restores text mode before exit.
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running BLTFILL v2 probe (Cirrus BLT vs dosmemput, 3-mode fallback)...
BLTFILL.EXE
ECHO ----
ECHO Probe finished. BLTFILL.LOG contents:
TYPE BLTFILL.LOG
