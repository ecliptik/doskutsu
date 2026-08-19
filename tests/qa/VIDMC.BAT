@ECHO OFF
REM ===================================================================
REM  VIDMC -- Mach64 COLOUR PROBE.  Runs the VIDM cells at 16bpp
REM  (RGB565), which removes the master palette, the index remap and
REM  the VGA DAC from the path entirely.
REM
REM  BY EYE ONLY.  It writes the same log tags as VIDM, so its logs
REM  are disposable -- run the real VIDM after this, not before.
REM
REM  Usage:  QA [n]   then   VIDMC [n] [PG]
REM ===================================================================
IF "%QAM%"=="" GOTO NOMACH
SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=0
ECHO.
ECHO ============================================================
ECHO  COLOUR PROBE -- 16bpp, no palette / no DAC in the path
ECHO ============================================================
ECHO  WATCH THE MAP-NAME TEXT when the stage loads:
ECHO.
ECHO    text WHITE  -- the indexed + colour-mod path is at fault
ECHO    text PINK   -- fault is upstream of the palette
ECHO.
ECHO  Logs are DISPOSABLE. Re-run VIDM afterwards for the real cells.
ECHO ============================================================
CALL VIDM %1 %2
SET SDL_HINT_DOSKUTSU_PIXEL_FORMAT_8=
ECHO.
ECHO ============================================================
ECHO  PROBE DONE.  PIXEL_FORMAT_8 cleared -- safe to run other sweeps.
ECHO ============================================================
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU).
:END
