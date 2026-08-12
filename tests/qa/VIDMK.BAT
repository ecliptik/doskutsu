@ECHO OFF
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /C %0 GO %1 %2
GOTO END
:GO
IF "%2"=="1" SET QAM=G
IF "%2"=="2" SET QAM=A
IF "%2"=="3" SET QAM=6
IF "%2"=="4" SET QAM=5
REM --- QA-INSTRUMENT v1 ---
IF "%2"=="1" SET QACPU=Pentium OverDrive 83
IF "%2"=="2" SET QACPU=Am5x86-133
IF "%2"=="3" SET QACPU=486DX2-66
IF "%2"=="4" SET QACPU=486DX2-50
IF NOT "%QAM%"=="" GOTO START
ECHO ============================================================
ECHO  VIDM -- video lane: ATI Mach64
ECHO ============================================================
ECHO  Give it the CPU in the box:  VIDM 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
REM --- QA-INSTRUMENT v1 ---
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO ============================================================
ECHO  SWEEP : Video lane -- ATI Mach64, VBLANK_BOUND=0 A/B
ECHO  CELLS : 2 cells, ~10 min
ECHO  CPU   : %QACPU%
REM --- SOUND-BANNER v1 ---
IF "%3"=="PG" ECHO  SOUND : PicoGUS -- will switch it to SB mode
IF NOT "%3"=="PG" ECHO  SOUND : the SB already in the box (add PG for a PicoGUS)
REM --- SOUND-BANNER v1 ---
ECHO  VIDEO : ATI Mach64
ECHO  LOGS  : tagged %QAM%..  in LOGS\
ECHO ============================================================
ECHO  Check the CPU and cards above match the box before starting.
ECHO ============================================================
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%VIDMK.NFO
ECHO sweep=VIDMK >> LOGS\%QAM%VIDMK.NFO
ECHO cells=2 >> LOGS\%QAM%VIDMK.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%VIDMK.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%VIDMK.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%VIDMK.NFO
REM --- SOUND-BANNER v1 ---
IF "%3"=="PG" ECHO sound=PicoGUS in SB mode >> LOGS\%QAM%VIDMK.NFO
IF NOT "%3"=="PG" ECHO sound=SB already in the box >> LOGS\%QAM%VIDMK.NFO
REM --- SOUND-BANNER v1 ---
ECHO video=ATI Mach64 >> LOGS\%QAM%VIDMK.NFO
ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\%QAM%VIDMK.NFO
REM --- QA-INSTRUMENT v1 ---
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  VIDEO LANE -- ATI Mach64  (VBLANK_BOUND KILLSWITCH)
ECHO  machine %QAM%   2 cells   about 10 min   logs tag ...M
ECHO ============================================================
ECHO  Only the video card should differ from your other lanes.
ECHO  Everything else -- CPU, sound card, reel -- stays put.
ECHO ============================================================
PAUSE
ECHO.
REM --- SOUND-OPT v1 ---
REM PicoGUS setup is OPT-IN. Without it the lane uses whatever SB is
REM already in the box -- a Vibra16 needs its own BLASTER (I5 D1 H5),
REM and the PicoGUS jumper values below would break it.
IF NOT "%3"=="PG" GOTO SNDOK
ECHO  switching PicoGUS -> SB MODE
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
GOTO SNDOK2
:SNDOK
ECHO  using the SB already in the box (BLASTER left as the boot set it)
:SNDOK2
REM --- SOUND-OPT v1 ---
CALL CLRENV
SET SDL_HINT_DOSKUTSU_VBLANK_BOUND=0
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%C4MK
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/2]  CELL C4 on ATI Mach64
ECHO  log %QAM%C4MK   fps ANCHOR -- same OPL3 config as every other lane
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
SET SDL_HINT_DOSKUTSU_VBLANK_BOUND=0
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%51MK
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/2]  CELL G51 on ATI Mach64
ECHO  log %QAM%51MK   render witness: boot, title, backdrop scroll
ECHO ============================================
DOSKUTSU.EXE
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  ATI Mach64 LANE DONE -- 2 logs, tags %QAM%C4MK and %QAM%51MK
ECHO  These do NOT collide with any other lane.
ECHO ============================================================
:END
