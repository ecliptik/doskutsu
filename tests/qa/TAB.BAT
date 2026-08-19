@ECHO OFF
REM ===================================================================
REM  TAB -- tile A/B levers that already ship, default-OFF.
REM
REM  Both hints below are compiled into DOSKUTSU.EXE c1729d2fe065 and
REM  neither has ever been measured on hardware. No new binary.
REM
REM    cell 1  control            both OFF, current behaviour
REM    cell 2  TILE_BBOX_SKIP=1   skip wholly-off-screen tile slots
REM    cell 3  TILEMAP_CACHE=1    whole-screen tilemap cache (0097)
REM
REM  ALL THREE ARE FPS ROWS. No instrumentation is set here on purpose
REM  -- the point is a clean wall-clock comparison. Compare them to
REM  each other, not to another lane.
REM
REM  Expect cell 3 to do NOTHING while the camera moves: the 0097 cache
REM  is keyed on pixel scroll, so a scrolling reel cannot hit it. Round
REM  I read bg_state=D fg_state=D with zero hits AND zero misses. If
REM  cell 3 comes back level with cell 1, that is the predicted result
REM  and it CONFIRMS the cache is not the answer -- not a failed run.
REM
REM  Usage:  QA [n]   then   TAB [n] [PG]
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
ECHO  TAB -- tile A/B lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  TAB 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%TAB.NFO
ECHO sweep=TAB >> LOGS\%QAM%TAB.NFO
ECHO cells=3 >> LOGS\%QAM%TAB.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%TAB.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%TAB.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%TAB.NFO
IF "%3"=="PG" ECHO sound=PicoGUS in SB mode >> LOGS\%QAM%TAB.NFO
IF NOT "%3"=="PG" ECHO sound=SB already in the box >> LOGS\%QAM%TAB.NFO
ECHO video=whatever is in the box >> LOGS\%QAM%TAB.NFO
ECHO note=tile A/B -- ALL THREE cells are fps rows >> LOGS\%QAM%TAB.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  TILE A/B -- all three cells ARE fps rows
ECHO  machine %QAM%   3 cells   tags %QAM%T0 %QAM%TB %QAM%TC
ECHO ============================================================
ECHO  1 control   2 bbox-skip   3 tilemap-cache
ECHO.
ECHO  Cell 3 coming back level with cell 1 is the EXPECTED result.
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
SET DOSKUTSU_LOG_TAG=%QAM%T0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/3]  CONTROL   log %QAM%T0
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_TILE_BBOX_SKIP=1
SET DOSKUTSU_LOG_TAG=%QAM%TB
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/3]  TILE_BBOX_SKIP=1   log %QAM%TB
ECHO ============================================
DOSKUTSU.EXE
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_TILEMAP_CACHE=1
SET DOSKUTSU_LOG_TAG=%QAM%TC
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/3]  TILEMAP_CACHE=1   log %QAM%TC
ECHO  expect level with cell 1 -- that is the answer, not a failure
ECHO ============================================
DOSKUTSU.EXE
SET SDL_HINT_DOSKUTSU_TILE_BBOX_SKIP=
SET SDL_HINT_DOSKUTSU_TILEMAP_CACHE=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  TAB DONE -- logs %QAM%T0 %QAM%TB %QAM%TC
ECHO  Both hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
