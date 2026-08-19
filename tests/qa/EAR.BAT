@ECHO OFF
IF "%QAM%"=="" GOTO NOMACH
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO
GOTO END
:NOMACH
ECHO ERROR: machine not set. Run QA.BAT first (pick your CPU), then re-run.
GOTO END
:GO
REM --- QA-INSTRUMENT v1 ---
IF "%QAM%"=="G" SET QACPU=Pentium OverDrive 83
IF "%QAM%"=="A" SET QACPU=Am5x86-133
IF "%QAM%"=="6" SET QACPU=486DX2-66
IF "%QAM%"=="5" SET QACPU=486DX2-50
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : BY-EAR pairs -- THESE NEED YOUR EARS
ECHO  CELLS : 3 cells, ~8 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS REQUIRED -- switches sb to adlib for the AdLib cell
ECHO  VIDEO : S3 ViRGE
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Q1 THE SHACK: music for the WHOLE visit, late, or never? (E4 vs E3)
ECHO  Q2 ADLIB vs OPL3: acceptable as the 486 default? (EA vs E4)
ECHO  AdLib is MUSIC-ONLY -- PC-speaker SFX, no DAC SFX.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%EAR.NFO
ECHO sweep=EAR >> LOGS\%QAM%EAR.NFO
ECHO cells=3 >> LOGS\%QAM%EAR.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%EAR.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%EAR.NFO
ECHO config=%config% >> LOGS\%QAM%EAR.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%EAR.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%EAR.NFO
ECHO cell=%QAM%E4 class=probe >> LOGS\%QAM%EAR.NFO
ECHO cell=%QAM%E3 class=probe >> LOGS\%QAM%EAR.NFO
ECHO cell=%QAM%EA class=probe >> LOGS\%QAM%EAR.NFO
ECHO sound_note=PicoGUS REQUIRED -- switches sb to adlib for the AdLib cell >> LOGS\%QAM%EAR.NFO
ECHO video_note=S3 ViRGE >> LOGS\%QAM%EAR.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%EAR.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
PAUSE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS to SB MODE (cells needing a live SB DAC)
ECHO ##########################################################
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%E4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/3] CELL E4   tag %QAM%E4
ECHO  OPL3 -- Shack reference
ECHO  LISTEN at the Shack. Music present the whole visit?
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%E4
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%E3
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/3] CELL E3   tag %QAM%E3
ECHO  Organya -- Shack suspect
ECHO  LISTEN at the Shack. Silent? Late? Whole visit?
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%E3
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS to ADLIB MODE (no SB, so the PIT pump can own ch0)
ECHO ##########################################################
pgusinit /mode adlib
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%EA
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/3] CELL EA   tag %QAM%EA
ECHO  AdLib -- 486 default candidate
ECHO  Compare against E4 by ear. Good enough to recommend?
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%EA
ECHO.
ECHO === Write the answers down NOW, while fresh: ===
ECHO ===  E4 Shack music:  whole visit / late / never ===
ECHO ===  E3 Shack music:  whole visit / late / never ===
ECHO ===  EA vs E4:        acceptable / not acceptable as 486 default ===
ECHO === PicoGUS left in ADLIB mode. ===
ECHO.
:END
