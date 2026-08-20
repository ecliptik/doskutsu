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
REM  Usage:  QA [n]   then   TAB [n]      (switches the PicoGUS itself)
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
IF "%QAVID%"=="" SET QAVID=UNDECLARED
IF "%QASND%"=="" SET QASND=UNDECLARED
IF "%QAIRQ%"=="" SET QAIRQ=UNDECLARED
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%TAB.NFO
ECHO schema=harness-1 >> LOGS\%QAM%TAB.NFO
ECHO sweep=TAB >> LOGS\%QAM%TAB.NFO
ECHO cells=3 >> LOGS\%QAM%TAB.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%TAB.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%TAB.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%TAB.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%TAB.NFO
ECHO video_declared=%QAVID% >> LOGS\%QAM%TAB.NFO
ECHO sound_declared=%QASND% >> LOGS\%QAM%TAB.NFO
ECHO irq_source=%QAIRQ% >> LOGS\%QAM%TAB.NFO
ECHO dktcap=%DKTCAP% >> LOGS\%QAM%TAB.NFO
ECHO cell=%QAM%T0 class=perf >> LOGS\%QAM%TAB.NFO
ECHO cell=%QAM%TB class=perf >> LOGS\%QAM%TAB.NFO
ECHO cell=%QAM%TC class=perf >> LOGS\%QAM%TAB.NFO
ECHO sound_note=PicoGUS switched to SB mode by this sweep >> LOGS\%QAM%TAB.NFO
ECHO video=whatever is in the box >> LOGS\%QAM%TAB.NFO
ECHO note=tile A/B -- ALL THREE cells are fps rows >> LOGS\%QAM%TAB.NFO
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
ECHO  TILE A/B -- all three cells ARE fps rows
ECHO  machine %QAM%   3 cells   tags %QAM%T0 %QAM%TB %QAM%TC
ECHO ============================================================
ECHO  1 control   2 bbox-skip   3 tilemap-cache
ECHO.
ECHO  Cell 3 coming back level with cell 1 is the EXPECTED result.
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
ECHO pgusmode_readback=begin >> LOGS\%QAM%TAB.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%TAB.NFO
ECHO pgusmode_readback=end >> LOGS\%QAM%TAB.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
pgusinit /sbenv > PGOUT.TMP
TYPE PGOUT.TMP
ECHO sbenv_readback=begin >> LOGS\%QAM%TAB.NFO
TYPE PGOUT.TMP >> LOGS\%QAM%TAB.NFO
ECHO sbenv_readback=end >> LOGS\%QAM%TAB.NFO
IF EXIST PGOUT.TMP DEL PGOUT.TMP
SET BLASTER=A220 I7 D3 P330 T3
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
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
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET SDL_HINT_DOSKUTSU_TILE_BBOX_SKIP=
SET SDL_HINT_DOSKUTSU_TILEMAP_CACHE=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  TAB DONE -- logs %QAM%T0 %QAM%TB %QAM%TC
ECHO  Both hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
