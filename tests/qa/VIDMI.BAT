@ECHO OFF
REM ===================================================================
REM  VIDMI -- Mach64 cell with FLIP INSTRUMENTATION on.
REM
REM  The engine already measures the flip breakdown -- drain, dirty,
REM  present, program, housekeeping -- but the emit is gated behind
REM  SDL_HINT_DOSKUTSU_FLIP_INSTR and defaults OFF, so every Mach64
REM  cell so far carried ZERO flip-stat lines. Nothing needed building
REM  to get this; it only needed switching on.
REM
REM  What it answers: how much of flip_inner is the FLUSH versus the
REM  renderer DRAIN. That split has been treated as unrecoverable in
REM  the analysis and it is not.
REM
REM  NOT AN FPS ROW. The extra timer reads per flip cost time. Read the
REM  breakdown, do not bank the frame rate.
REM
REM  Usage:  QA <n>   then   VIDMI <n> [PG]
REM ===================================================================
IF "%QAM%"=="" GOTO NOMACH
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=1
ECHO.
ECHO ============================================================
ECHO  FLIP INSTRUMENTATION -- breakdown cell, NOT an fps row
ECHO ============================================================
ECHO  Afterwards the log carries, once per phase:
ECHO.
ECHO    [flip-stat n=.. phase=drain    min=.. med=.. mean=.. p95=..]
ECHO    [flip-stat n=.. phase=dirty    ...]
ECHO    [flip-stat n=.. phase=present  ...]
ECHO.
ECHO  This cell runs SLOWER than a normal one by design.
ECHO ============================================================
CALL VIDM %1 %2
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=
ECHO.
ECHO ============================================================
ECHO  DONE. FLIP_INSTR cleared -- safe to run other sweeps.
ECHO ============================================================
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU).
:END
