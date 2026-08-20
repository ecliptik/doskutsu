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
REM  Usage:  QA [n]   then   DEEP [n]      (switches the PicoGUS itself)
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
IF "%QAVID%"=="" SET QAVID=UNDECLARED
IF "%QASND%"=="" SET QASND=UNDECLARED
IF "%QAIRQ%"=="" SET QAIRQ=UNDECLARED
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%DEEP.NFO
ECHO schema=harness-1 >> LOGS\%QAM%DEEP.NFO
ECHO sweep=DEEP >> LOGS\%QAM%DEEP.NFO
ECHO cells=2 >> LOGS\%QAM%DEEP.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%DEEP.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%DEEP.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%DEEP.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%DEEP.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%DEEP.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%DEEP.NFO
ECHO irq_source=%QAIRQ% >> LOGS\%QAM%DEEP.NFO
ECHO dktcap=%DKTCAP% >> LOGS\%QAM%DEEP.NFO
ECHO cell=%QAM%C4D class=diag >> LOGS\%QAM%DEEP.NFO
ECHO cell=%QAM%51D class=diag >> LOGS\%QAM%DEEP.NFO
ECHO sound_note=PicoGUS switched to SB mode by this sweep >> LOGS\%QAM%DEEP.NFO
ECHO video=whatever is in the box -- card-independent cell >> LOGS\%QAM%DEEP.NFO
ECHO note=7 instrumentation layers -- NOT an fps row >> LOGS\%QAM%DEEP.NFO
REM  DKTCAP=1 marks an UNATTENDED CAPTURE session. Default (unset) is
REM  byte-identical to every banked round: the 10 s banner delay stays, and
REM  no mode switch happens. Only an explicit SET DKTCAP=1 changes anything.
IF NOT "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
IF "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=0
REM  Switch to mode 12h HERE, before the banner below, not only after cells.
REM  The banner is the capture harness's positive witness that DKTCAP took --
REM  but printed into text mode 03h (720x400 @70 Hz) no capture device can
REM  lock it, so the witness would be invisible to the only thing that needs
REM  to read it. Firing the switch first makes the banner, the PAUSE and any
REM  error text capturable. Costs a slow planar redraw of the banner, which
REM  is outside every timed bracket.
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
IF "%DKTCAP%"=="1" ECHO  [DKTCAP=1] capture session: banner delay off, console
IF "%DKTCAP%"=="1" ECHO  returns to mode 12h between cells (text will be SLOW).
IF "%DKTCAP%"=="1" ECHO  SET DKTCAP= to turn off; VGACAP\MODE03 restores text.
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
REM  UNCONDITIONAL, deliberately. This was gated on a `PG` argument that in
REM  practice never fired -- every manifest this campaign has written says
REM  "SB already in the box". The cells worked anyway by INHERITING SB mode
REM  from an earlier sweep, which made them hostage to run order. Round M
REM  scheduled MINE straight after RB, whose last act is `pgusinit /mode
REM  adlib`, so no SB DSP answered at 0x220 and both cells died at sdl_init
REM  one second in. RB switches unconditionally at its top; so does this.
ECHO  switching PicoGUS to SB MODE
pgusinit /mode sb > PGOUT.TMP
TYPE PGOUT.TMP
ECHO pgusmode_readback=begin >> LOGS\%QAM%DEEP.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%DEEP.NFO
ECHO pgusmode_readback=end >> LOGS\%QAM%DEEP.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
pgusinit /sbenv > PGOUT.TMP
TYPE PGOUT.TMP
ECHO sbenv_readback=begin >> LOGS\%QAM%DEEP.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%DEEP.NFO
ECHO sbenv_readback=end >> LOGS\%QAM%DEEP.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
SET BLASTER=A220 I7 D3 P330 T3
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
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
