@ECHO OFF
REM ===================================================================
REM  LEV -- two shipped levers that have NEVER been A/B'd on hardware.
REM
REM  Both are compiled into the binary already on this card and both are
REM  default-OFF. Neither needs a rebuild. This is the same shape as
REM  TILE_BBOX_SKIP, which sat default-OFF and unmeasured for eleven
REM  waves and turned out to be a free +0.6 fps.
REM
REM    cell 1  control     both OFF, current behaviour
REM    cell 2  ASM_BLIT=1  hand-written blit inner loop (patch 0145)
REM    cell 3  PERF_MODE=1 drops DECORATIVE fg tiles (patch 0151 cut B2)
REM
REM  ALL THREE ARE FPS ROWS. Run them against each other, not against
REM  another lane. Do NOT stack the hints -- one lever per cell or the
REM  attribution is gone.
REM
REM  CELL 2 background: wave-51 retired ASM_BLIT at +0.40 fps, but that
REM  decision used a POOLED opacity histogram. Round J's census splits
REM  it by caller and the pool was hiding the population the lever was
REM  built for -- the bg caller is 86% opaque pixels with 77% of runs
REM  16 px or longer, against 45%/51% for the other caller, which is
REM  what wave-51 actually measured. Same lever, better-matched data.
REM
REM  CELL 3 IS A FIDELITY TRADE, NOT A FREE WIN. It removes decorative
REM  foreground tiles and keeps collision tiles, so the frame rate is
REM  only half the answer. WATCH THE SCREEN and say whether anything
REM  looks wrong or missing -- an fps gain that makes the game look
REM  worse is a product decision, not a perf win, and that call is
REM  yours. Measured once at +0.52 fps on the POD-83, never here.
REM
REM  Usage:  QA [n]   then   LEV [n]      (switches the PicoGUS itself)
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
ECHO  LEV -- shipped-lever A/B lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  LEV 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%LEV.NFO
ECHO sweep=LEV >> LOGS\%QAM%LEV.NFO
ECHO cells=3 >> LOGS\%QAM%LEV.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%LEV.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%LEV.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%LEV.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%LEV.NFO
ECHO sound=PicoGUS forced to SB mode by this sweep >> LOGS\%QAM%LEV.NFO
ECHO note=shipped-lever A/B -- ALL THREE cells are fps rows >> LOGS\%QAM%LEV.NFO
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
ECHO  SHIPPED-LEVER A/B -- all three cells ARE fps rows
ECHO  machine %QAM%   3 cells   tags %QAM%L0 %QAM%LA %QAM%LP
ECHO ============================================================
ECHO   1 control      2 ASM_BLIT      3 PERF_MODE
ECHO.
ECHO  CELL 3 CHANGES WHAT YOU SEE -- decorative fg tiles are dropped.
ECHO  Watch it and report whether the picture looks wrong.
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
pgusinit /mode sb
pgusinit /sbenv
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%L0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/3]  CONTROL   log %QAM%L0
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_ASM_BLIT=1
SET DOSKUTSU_LOG_TAG=%QAM%LA
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/3]  ASM_BLIT=1   log %QAM%LA
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_PERF_MODE=1
SET DOSKUTSU_LOG_TAG=%QAM%LP
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/3]  PERF_MODE=1   log %QAM%LP
ECHO  WATCH THE PICTURE -- fidelity trade, not a free win
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET SDL_HINT_DOSKUTSU_ASM_BLIT=
SET SDL_HINT_DOSKUTSU_PERF_MODE=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  LEV DONE -- logs %QAM%L0 %QAM%LA %QAM%LP
ECHO  Both hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
