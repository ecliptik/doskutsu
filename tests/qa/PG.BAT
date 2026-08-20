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
ECHO  PicoGUS sweep (sb + gus + adlib)
ECHO ============================================================
ECHO  Tell it which CPU is in the box:
ECHO      PG 3     486DX2-66   (logs tagged 6..)
ECHO      PG 1     POD-83      (logs tagged G..)
ECHO      PG 2     Am5x86-133  (logs tagged A..)
ECHO      PG 4     486DX2-50   (logs tagged 5..)
ECHO  Nothing ran. Re-run with the number.
GOTO END
:START
REM --- QA-INSTRUMENT v1 ---
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : PicoGUS sweep (sb + gus + adlib)
ECHO  CELLS : 13 cells, ~50 min
ECHO  CPU   : %QACPU%
ECHO  SOUND : PicoGUS -- DreamBlaster on the PicoGUS header
ECHO  VIDEO : S3 ViRGE
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Check the CPU and cards above match the box before starting.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%PG.NFO
ECHO sweep=PG >> LOGS\%QAM%PG.NFO
ECHO cells=13 >> LOGS\%QAM%PG.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%PG.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%PG.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%PG.NFO
ECHO sound=PicoGUS -- DreamBlaster on the PicoGUS header >> LOGS\%QAM%PG.NFO
ECHO video=S3 ViRGE >> LOGS\%QAM%PG.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%PG.NFO
REM --- QA-INSTRUMENT v1 ---
ECHO ============================================================
ECHO  PicoGUS sweep (sb + gus + adlib) -- machine %QAM%  13 cells  about 50 min
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  Unattended after this prompt: each cell exits itself when
ECHO  the reel ends. Just WATCH and LISTEN. Ctrl-C aborts.
ECHO ============================================================
PAUSE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS to SB MODE
ECHO ##########################################################
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ORGANYA.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%C3
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/13]  CELL C3   log %QAM%C3
ECHO  Organya music. Cave-transition stutter? Music continuity?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%C4
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/13]  CELL C4   log %QAM%C4
ECHO  OPL3 FM  *** THE fps ANCHOR ROW ***  full GM arrangement?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\PGSB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%31
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/13]  CELL G31   log %QAM%31
ECHO  PicoGUS-SB 8-bit DMA. Same config as C4 -- judge by EAR: mono SFX, cave screech.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%51
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/13]  CELL G51   log %QAM%51
ECHO  S3 ViRGE render witness: boot, title, backdrop scroll.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\SILENT.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%60
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  CELL G60   log %QAM%60
ECHO  RENDER CEILING: no music, no SFX. Everything else is measured against this.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3NS.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%61
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  CELL G61   log %QAM%61
ECHO  OPL3 music, SFX OFF. Isolates the SB-DAC SFX cost.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\WB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%17P
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [5/13]  CELL G17   log %QAM%17P
ECHO  WaveBlaster MIDI via the DreamBlaster on the PicoGUS header. Rich wavetable, or thin FM fallback?
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS to GUS MODE
ECHO ##########################################################
pgusinit /mode gus
SET ULTRASND=240,3,3,7,7
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\GUS.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%22
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [6/13]  CELL G22   log %QAM%22
ECHO  GF1 music + SFX, 20 voices, PPL patch set.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\GUS14.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%23A
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [7/13]  CELL G23A   log %QAM%23A
ECHO  GF1 voices = 14 on a busy song. Instruments dropping?
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\GUS32.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%23B
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [8/13]  CELL G23B   log %QAM%23B
ECHO  GF1 voices = 32 on the same song. Better or worse than 14?
ECHO ============================================
DOSKUTSU.EXE
ECHO.
ECHO ##########################################################
ECHO  switching PicoGUS to ADLIB MODE
ECHO ##########################################################
pgusinit /mode adlib
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%41
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [9/13]  CELL G41   log %QAM%41
ECHO  OPL-only: FM music + PC-speaker SFX.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIB.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%C5
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [10/13]  CELL C5   log %QAM%C5
ECHO  Same config as G41 -- second sample / repeatability.
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\ADLIBNS.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%62
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  CELL G62   log %QAM%62
ECHO  AdLib music, SFX OFF. Pairs with G61: if the gap to OPL3 closes here, the PC-speaker SFX path is why AdLib leads.
ECHO ============================================
DOSKUTSU.EXE
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  SWEEP COMPLETE -- 13 logs in LOGS\ (tag %QAM%..)
ECHO  Back to the laptop wizard for feedback.
ECHO ============================================================
:END
