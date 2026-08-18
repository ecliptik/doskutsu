@ECHO OFF
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
REM --- round-2 guard: re-baselining round 1 against round 1 agrees
REM --- perfectly and proves nothing. ROUND2.OK is written by the
REM --- installer ONLY when the card carries a non-round-1 binary.
IF EXIST ROUND2.OK GOTO R2OK
ECHO ============================================================
ECHO  RB REFUSES TO RUN
ECHO ============================================================
ECHO  This card carries the ROUND-1 binary. Re-baselining it against
ECHO  round-1 numbers agrees perfectly and tests nothing.
ECHO.
ECHO  See BINARY.NFO on this card for what is installed.
ECHO  Populate with a round-2 payload, then re-run RB.
ECHO.
ECHO  PROVE / GAP / EAR do not need round 2 and run fine now.
ECHO ============================================================
GOTO END
:R2OK
REM --- QA-INSTRUMENT v1 ---
IF "%QAM%"=="G" SET QACPU=Pentium OverDrive 83
IF "%QAM%"=="A" SET QACPU=Am5x86-133
IF "%QAM%"=="6" SET QACPU=486DX2-66
IF "%QAM%"=="5" SET QACPU=486DX2-50
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : RE-BASELINE anchor set (run after ANY new binary)
ECHO  CELLS : 4 cells, ~10 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS REQUIRED -- this sweep switches it sb -> adlib
ECHO  VIDEO : S3 ViRGE (match the banked anchors)
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Each cell covers a DIFFERENT thing the new binary changed.
ECHO  R4/R4B are ADJACENT on purpose: that spread IS the noise floor.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%RB.NFO
ECHO sweep=RB >> LOGS\%QAM%RB.NFO
ECHO cells=4 >> LOGS\%QAM%RB.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%RB.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%RB.NFO
ECHO sound=PicoGUS REQUIRED -- this sweep switches it sb -> adlib >> LOGS\%QAM%RB.NFO
ECHO video=S3 ViRGE (match the banked anchors) >> LOGS\%QAM%RB.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%RB.NFO
REM  DKTCAP=1 marks an UNATTENDED CAPTURE session. Default (unset) is
REM  byte-identical to every banked round: the 10 s banner delay stays, and
REM  no mode switch happens. Only an explicit SET DKTCAP=1 changes anything.
IF NOT "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
IF "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=0
IF "%DKTCAP%"=="1" ECHO  [DKTCAP=1] capture session: banner delay off, console
IF "%DKTCAP%"=="1" ECHO  returns to mode 12h between cells (text will be SLOW).
IF "%DKTCAP%"=="1" ECHO  SET DKTCAP= to turn off; VGACAP\MODE03 restores text.
PAUSE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS -> SB MODE (cells needing a live SB DAC)
ECHO ##########################################################
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/4] CELL R4   tag %QAM%R4
ECHO  OPL3 -- the control
ECHO  Non-pump. Must match the banked C4 anchor.
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R4B
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/4] CELL R4B   tag %QAM%R4B
ECHO  OPL3 repeat -- the noise floor
ECHO  Back-to-back with R4. The spread IS the noise floor.
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R3
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/4] CELL R3   tag %QAM%R3
ECHO  Organya -- the re-rendered PCM cache
ECHO  Catches a silently different re-render.
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS -> ADLIB MODE (no SB, so the PIT pump can own ch0)
ECHO ##########################################################
pgusinit /mode adlib
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%R5
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/4] CELL R5   tag %QAM%R5
ECHO  AdLib -- the pump path
ECHO  The timebase fix touches ONLY gus/adlib. Movement here is a FINDING.
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
ECHO.
ECHO === RE-BASELINE DONE. Logs %QAM%R4 %QAM%R4B %QAM%R3 %QAM%R5 ===
ECHO === R4 vs banked C4 = the re-baseline; R4 vs R4B = the noise floor. ===
ECHO === PicoGUS left in ADLIB mode. ===
ECHO.
:END
