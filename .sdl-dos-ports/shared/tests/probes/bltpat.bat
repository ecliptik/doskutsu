@ECHO OFF
REM BLTPAT.BAT - Cirrus 5434 BLT PATTERN_COPY hail-mary re-attempt (wave-38 ride-along).
REM Re-attempts wave-36 V6 with V7-lesson-aware probe authoring (explicit FG/BG
REM color reg setup + expected-vs-got hex for both 1bpp and 8bpp source interps).
REM 4 variants: V_PAT_A baseline / V_PAT_B byte-checker / V_PAT_C reset-pre / V_PAT_D src-far.
REM Output: BLTPAT.LOG (per-variant raw dst hex + auto-classified status).
REM Runtime: under 5 sec on PODP83 (4 variants x ~5 ms BLT each + setup).
REM HAZARD: enters VBE 8bpp mode; screen looks corrupted during variants;
REM restores text mode before exit.
REM No env vars required. Pure DJGPP; CWSDPMI.EXE same dir.
ECHO Running BLTPAT probe (PATTERN_COPY hail-mary)...
BLTPAT.EXE
ECHO ----
ECHO Probe finished. BLTPAT.LOG tail:
TYPE BLTPAT.LOG
