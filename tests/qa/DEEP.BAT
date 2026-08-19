@ECHO OFF
REM ===================================================================
REM  DEEP -- full tile-draw decomposition. NO NEW BINARY.
REM
REM  Every hint below is already compiled into DOSKUTSU.EXE
REM  c1729d2fe065 -- the binary already on this card. All of them
REM  shipped OFF, so nothing has ever measured what is inside the
REM  24 ms the two tile layers cost. Four SET lines buy that.
REM
REM  SEVEN HINTS, ONE CHAIN. The decomposition is a chain of
REM  independent layer toggles and setting only the top yields a split
REM  with nothing under it:
REM
REM    FINE_INSTR             coarse audio vs engine split
REM    MODE_TICK_INSTR        mode_tick_render vs mode_tick_state
REM    MODE_DRAW_SCENE_INSTR  the mds_* leaves
REM    MDS_BG_INSTR           inside mds_tilemap_bg
REM    MDS_TFG_INSTR          inside mds_tilemap_fg
REM    MDS_DECOMP             per-pass byte counts + cache-blit timing
REM    BLIT_DECOMP            the _blit_indexed inner-loop arms
REM
REM  All set AFTER each CLRENV, which does not clear them today -- but
REM  if it ever does, setting first would silently give a cell with no
REM  instrumentation that still looks like it ran.
REM
REM  NOT AN FPS ROW. Seven layers of timers cost real time. Read the
REM  decomposition, do not bank the frame rate.
REM
REM  Usage:  QA [n]   then   DEEP [n] [PG]
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
ECHO  DEEP -- tile-draw decomposition lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  DEEP 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%DEEP.NFO
ECHO sweep=DEEP >> LOGS\%QAM%DEEP.NFO
ECHO cells=2 >> LOGS\%QAM%DEEP.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%DEEP.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%DEEP.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%DEEP.NFO
ECHO config=%config% >> LOGS\%QAM%DEEP.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%DEEP.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%DEEP.NFO
ECHO cell=%QAM%C4D class=diag >> LOGS\%QAM%DEEP.NFO
ECHO cell=%QAM%51D class=diag >> LOGS\%QAM%DEEP.NFO
IF "%3"=="PG" ECHO sound_note=PicoGUS in SB mode >> LOGS\%QAM%DEEP.NFO
IF NOT "%3"=="PG" ECHO sound=SB already in the box >> LOGS\%QAM%DEEP.NFO
ECHO video_note=whatever is in the box -- card-independent cell >> LOGS\%QAM%DEEP.NFO
ECHO note=7 instrumentation layers -- NOT an fps row >> LOGS\%QAM%DEEP.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  TILE DECOMPOSITION -- breakdown cells, NOT fps rows
ECHO  machine %QAM%   2 cells   logs tag %QAM%C4D and %QAM%51D
ECHO ============================================================
ECHO  Afterwards each log should carry, repeatedly:
ECHO.
ECHO    [engine-tick-stat .. phase=mds_tilemap_bg ..]
ECHO    [blit-decomp .. colorkey= .. memcpy= ..]
ECHO    [mds-decomp .. pass=backdrop bytes= ..]
ECHO.
ECHO  If the blit-decomp lines are absent the cell FAILED -- say so.
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
SET SDL_HINT_DOSKUTSU_FINE_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=1
SET SDL_HINT_DOSKUTSU_MDS_BG_INSTR=1
SET SDL_HINT_DOSKUTSU_MDS_TFG_INSTR=1
SET SDL_HINT_DOSKUTSU_MDS_DECOMP=1
SET SDL_HINT_DOSKUTSU_BLIT_DECOMP=1
SET DOSKUTSU_LOG_TAG=%QAM%C4D
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/2]  CELL C4 decomposed   log %QAM%C4D
ECHO  same OPL3 config as the C4M anchor
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%C4D
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FINE_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=1
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=1
SET SDL_HINT_DOSKUTSU_MDS_BG_INSTR=1
SET SDL_HINT_DOSKUTSU_MDS_TFG_INSTR=1
SET SDL_HINT_DOSKUTSU_MDS_DECOMP=1
SET SDL_HINT_DOSKUTSU_BLIT_DECOMP=1
SET DOSKUTSU_LOG_TAG=%QAM%51D
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/2]  CELL G51 decomposed   log %QAM%51D
ECHO  auto-detect audio, same reel
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%51D
SET SDL_HINT_DOSKUTSU_FINE_INSTR=
SET SDL_HINT_DOSKUTSU_MODE_TICK_INSTR=
SET SDL_HINT_DOSKUTSU_MODE_DRAW_SCENE_INSTR=
SET SDL_HINT_DOSKUTSU_MDS_BG_INSTR=
SET SDL_HINT_DOSKUTSU_MDS_TFG_INSTR=
SET SDL_HINT_DOSKUTSU_MDS_DECOMP=
SET SDL_HINT_DOSKUTSU_BLIT_DECOMP=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  DEEP DONE -- logs %QAM%C4D and %QAM%51D
ECHO  All 7 hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
