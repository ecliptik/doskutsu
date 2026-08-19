@ECHO OFF
REM ===================================================================
REM  VIDMI -- Mach64 cell with FLIP INSTRUMENTATION on.
REM
REM  SELF-CONTAINED as of 2026-08-15. The previous version did
REM  `CALL VIDM`, which meant it inherited VIDM's log tags and
REM  OVERWROTE the clean fps rows with instrumented ones -- you could
REM  never hold both from one sitting. This writes its own tags:
REM
REM      VIDM   gives  [m]C4M  [m]51M   (clean, fps rows)
REM      VIDMI  gives  [m]C4I  [m]51I   (instrumented, NOT fps rows)
REM
REM  FLIP_INSTR is set AFTER each CLRENV on purpose. CLRENV is
REM  generated from the hint list and does not clear FLIP_INSTR today,
REM  but if it ever does, setting it first would silently produce a
REM  cell with no instrumentation that still looks like it ran.
REM
REM  NOT AN FPS ROW. The extra timer reads per flip cost time. Read the
REM  breakdown, do not bank the frame rate.
REM
REM  Usage:  QA [n]   then   VIDMI [n] [PG]
REM ===================================================================
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO %1 %2
GOTO END
:GO
IF "%2"=="1" SET QAM=G
IF "%2"=="2" SET QAM=A
IF "%2"=="3" SET QAM=6
IF "%2"=="4" SET QAM=5
IF "%2"=="1" SET QACPU=Pentium OverDrive 83
IF "%2"=="2" SET QACPU=Am5x86-133
IF "%2"=="3" SET QACPU=486DX2-66
IF "%2"=="4" SET QACPU=486DX2-50
IF NOT "%QAM%"=="" GOTO START
ECHO ============================================================
ECHO  VIDMI -- Mach64 flip-instrumentation lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  VIDMI 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%VIDMI.NFO
ECHO sweep=VIDMI >> LOGS\%QAM%VIDMI.NFO
ECHO cells=2 >> LOGS\%QAM%VIDMI.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%VIDMI.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%VIDMI.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%VIDMI.NFO
ECHO config=%config% >> LOGS\%QAM%VIDMI.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%VIDMI.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%VIDMI.NFO
ECHO cell=%QAM%C4I class=diag >> LOGS\%QAM%VIDMI.NFO
ECHO cell=%QAM%51I class=diag >> LOGS\%QAM%VIDMI.NFO
IF "%3"=="PG" ECHO sound_note=PicoGUS in SB mode >> LOGS\%QAM%VIDMI.NFO
IF NOT "%3"=="PG" ECHO sound=SB already in the box >> LOGS\%QAM%VIDMI.NFO
ECHO video_note=ATI Mach64 >> LOGS\%QAM%VIDMI.NFO
ECHO note=FLIP_INSTR=1 -- breakdown cell, NOT an fps row >> LOGS\%QAM%VIDMI.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  FLIP INSTRUMENTATION -- breakdown cells, NOT fps rows
ECHO  machine %QAM%   2 cells   logs tag %QAM%C4I and %QAM%51I
ECHO ============================================================
ECHO  These do NOT overwrite VIDM's %QAM%C4M / %QAM%51M rows.
ECHO  Afterwards each log carries, once per phase:
ECHO.
ECHO    [flip-stat n=.. phase=drain    min=.. med=.. mean=.. p95=..]
ECHO    [flip-stat n=.. phase=dirty    ...]
ECHO    [flip-stat n=.. phase=present  ...]
ECHO.
ECHO  These cells run SLOWER than a normal one by design.
ECHO ============================================================
PAUSE
ECHO.
IF NOT "%3"=="PG" GOTO SNDOK
ECHO  switching PicoGUS to SB MODE
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
GOTO SNDOK2
:SNDOK
ECHO  using the SB already in the box (BLASTER left as the boot set it)
:SNDOK2
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=1
SET DOSKUTSU_LOG_TAG=%QAM%C4I
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/2]  CELL C4 instrumented   log %QAM%C4I
ECHO  same OPL3 config as VIDM's C4M anchor
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%C4I
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=1
SET DOSKUTSU_LOG_TAG=%QAM%51I
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/2]  CELL G51 instrumented   log %QAM%51I
ECHO  render witness: boot, title, backdrop scroll
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%51I
SET SDL_HINT_DOSKUTSU_FLIP_INSTR=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  VIDMI DONE -- logs %QAM%C4I and %QAM%51I
ECHO  FLIP_INSTR cleared -- safe to run other sweeps.
ECHO  VIDM's %QAM%C4M / %QAM%51M rows are untouched.
ECHO ============================================================
:END
