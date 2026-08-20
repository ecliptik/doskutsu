@ECHO OFF
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO %1
GOTO END
:GO
IF "%2"=="1" SET QAM=G
IF "%2"=="3" SET QAM=6
IF "%2"=="2" SET QAM=A
IF "%2"=="4" SET QAM=5
REM --- QA-INSTRUMENT v1 ---
IF "%2"=="1" SET QACPU=Pentium OverDrive 83
IF "%2"=="2" SET QACPU=Am5x86-133
IF "%2"=="3" SET QACPU=486DX2-66
IF "%2"=="4" SET QACPU=486DX2-50
IF NOT "%QAM%"=="" GOTO START
ECHO ============================================================
ECHO  Vibra16 sweep (AUTO + WB + OPL3 + Organya)
ECHO ============================================================
ECHO  Tell it which CPU is in the box:
ECHO      VB 3     486DX2-66   (logs tagged 6..)
ECHO      VB 1     POD-83      (logs tagged G..)
ECHO      VB 2     Am5x86-133  (logs tagged A..)
ECHO      VB 4     486DX2-50   (logs tagged 5..)
ECHO  Nothing ran. Re-run with the number.
GOTO END
:START
REM --- QA-INSTRUMENT v1 ---
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : Vibra16 sweep (AUTO + WB + OPL3 + Organya)
ECHO  CELLS : 6 cells, ~13 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : Vibra16 -- DreamBlaster on the Vibra header
ECHO  VIDEO : S3 ViRGE
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Check the CPU and cards above match the box before starting.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%VB.NFO
ECHO sweep=VB >> LOGS\%QAM%VB.NFO
ECHO cells=6 >> LOGS\%QAM%VB.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%VB.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%VB.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%VB.NFO
ECHO sound=Vibra16 -- DreamBlaster on the Vibra header >> LOGS\%QAM%VB.NFO
ECHO video=S3 ViRGE >> LOGS\%QAM%VB.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%VB.NFO
REM --- QA-INSTRUMENT v1 ---
ECHO ============================================================
ECHO  Vibra16 sweep (AUTO + WB + OPL3 + Organya) -- machine %QAM%  6 cells  about 35 min
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  Unattended after this prompt: each cell exits itself when
ECHO  the reel ends. Just WATCH and LISTEN. Ctrl-C aborts.
ECHO ============================================================
PAUSE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%02
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/6]  CELL G02   log %QAM%02
ECHO  Cold-boot baseline to title, daily-driver config.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%16
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/6]  CELL G16   log %QAM%16
ECHO  AUTO detect -- should pick the WaveBlaster. Does it?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\WB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%17V
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/6]  CELL G17   log %QAM%17V
ECHO  WaveBlaster MIDI (orgmid2) via the DreamBlaster.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%18
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/6]  CELL G18   log %QAM%18
ECHO  OPL3 FM on the SB16's own OPL3 -- the orgmid2 witness.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%111
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [5/6]  CELL G111   log %QAM%111
ECHO  Organya 11025 cache-hit. Cave-transition stutter severity?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGHQ.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%112
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [6/6]  CELL G112   log %QAM%112
ECHO  Organya-HQ 22050 stereo. Sound + feel on heavy scenes.
ECHO ============================================
DOSKUTSU.EXE
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  SWEEP COMPLETE -- 6 logs in LOGS\ (tag %QAM%..)
ECHO  Back to the laptop wizard for feedback.
ECHO ============================================================
:END
