@ECHO OFF
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
REM --- QA-INSTRUMENT v1 (matches PG/VB sweep manifests) ---
IF "%QAM%"=="G" SET QACPU=Pentium OverDrive 83
IF "%QAM%"=="A" SET QACPU=Am5x86-133
IF "%QAM%"=="6" SET QACPU=486DX2-66
IF "%QAM%"=="5" SET QACPU=486DX2-50
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : RE-BASELINE anchor set (run after ANY new binary)
ECHO  CELLS : 4 cells, ~10 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS -- must match the banked C3/C4/C5 anchors
ECHO  VIDEO : S3 ViRGE
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Check the CPU and cards above match the box before starting.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%RB.NFO
ECHO sweep=RB >> LOGS\%QAM%RB.NFO
ECHO cells=4 >> LOGS\%QAM%RB.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%RB.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%RB.NFO
ECHO sound=PicoGUS -- must match the banked C3/C4/C5 anchors >> LOGS\%QAM%RB.NFO
ECHO video=S3 ViRGE >> LOGS\%QAM%RB.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%RB.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
PAUSE
ECHO  Each cell covers a DIFFERENT change; none is redundant:
ECHO    R4  OPL3     control -- non-pump, isolates observer effect
ECHO    R5  AdLib    pump    -- the timebase fix touches ONLY gus/adlib
ECHO    R3  Organya  cache   -- a new binary RE-RENDERS the PCM cache
ECHO    R4B OPL3     repeat  -- back-to-back, measures the noise floor
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/4] CELL R4  OPL3 -- the control
ECHO  tag %QAM%R4
ECHO  Non-pump. Should match the banked C4 anchor.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R5
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/4] CELL R5  AdLib -- the pump cell
ECHO  tag %QAM%R5
ECHO  Timebase fix lands HERE. Movement is a FINDING, not drift.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R3
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/4] CELL R3  Organya -- the re-rendered cache
ECHO  tag %QAM%R3
ECHO  Catches a silently different PCM re-render.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R4B
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/4] CELL R4B  OPL3 repeat -- noise floor
ECHO  tag %QAM%R4B
ECHO  Back-to-back with R4. The spread IS the noise floor.
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO === RE-BASELINE DONE. Logs %QAM%R4 %QAM%R5 %QAM%R3 %QAM%R4B ===
ECHO === Compare R4 against the banked C4 for this machine. ===
ECHO === R4 vs R4B is the noise floor. ===
ECHO.
:END
