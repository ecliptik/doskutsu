@ECHO OFF
REM BLTVAR.BAT - Cirrus 5434 BLT BULK_COPY variant matrix probe (wave-36 Task A).
REM Root-cause investigation of BLTASYNC v2 REFUTE_VERIFY_FAIL.
REM 8 variants explore BULK_COPY register sequence + alt modes.
REM Output: BLTVAR.LOG (per-variant raw dst hex + auto-classified status).
REM Runtime: under 5 sec on PODP83 (8 variants x ~4 ms BLT each + setup).
REM HAZARD: enters VBE 8bpp mode; screen looks corrupted during variants;
REM restores text mode before exit.
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running BLTVAR probe (BULK_COPY variant matrix)...
BLTVAR.EXE
ECHO ----
ECHO Probe finished. BLTVAR.LOG tail:
TYPE BLTVAR.LOG
