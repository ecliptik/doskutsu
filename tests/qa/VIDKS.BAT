@ECHO OFF
REM ===================================================================
REM  VIDKS -- Mach64 backdrop-placement observation, 0297 fast paths OFF.
REM
REM  Answers one question by eye: with the hand-rolled paths stood down
REM  but 0296 centring still on, does the backdrop draw CENTRED in the
REM  320x240 box, or FILL the whole 512x384 surface?
REM
REM  Own log tag (%QAM%KS) so it cannot overwrite VIDM's C4M/51M rows.
REM  The hint is set AFTER CLRENV on purpose: CLRENV is generated from
REM  the hint list and does not clear this one today, but if it ever
REM  does, setting it first would silently produce a NORMAL run that
REM  looks like a valid killswitch observation.
REM
REM  WATCH-THEN-RESET. Its log is disposable; do not let it finish.
REM
REM  Usage:  QA [n]   then   VIDKS [n] [PG]
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
ECHO  VIDKS -- Mach64 backdrop placement, fast paths OFF
ECHO ============================================================
ECHO  Give it the CPU in the box:  VIDKS 3  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%VIDKS.NFO
ECHO sweep=VIDKS >> LOGS\%QAM%VIDKS.NFO
ECHO cells=1 >> LOGS\%QAM%VIDKS.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%VIDKS.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%VIDKS.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%VIDKS.NFO
ECHO video=ATI Mach64 >> LOGS\%QAM%VIDKS.NFO
ECHO note=FASTPATH_OVERSIZED=0 -- backdrop placement by eye, NOT an fps row >> LOGS\%QAM%VIDKS.NFO
SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
ECHO ============================================================
ECHO  BACKDROP PLACEMENT -- 0297 fast paths OFF, 0296 centring ON
ECHO  machine %QAM%   1 cell   log %QAM%KS
ECHO ============================================================
ECHO  Watch the FIRST map load, then RESET the machine.
ECHO  Do not let it finish -- this is not an fps row.
ECHO.
ECHO  REPORT: is the backdrop CENTRED in the game box, or does it
ECHO          FILL the whole 512x384 surface?
ECHO.
ECHO  Expect pink map-name text and slow rendering -- that is the
ECHO  pre-0297 behaviour and it is correct for this cell.
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
ECHO.
ECHO  WARNING: no PG given, so nothing switched the PicoGUS. If a PicoGUS
ECHO  is the only sound card in this box it is NOT in SB mode -- RB and the
ECHO  other sweeps leave it in ADLIB mode when they finish -- and every cell
ECHO  here will abort at sdl_init with "Not a SoundBlaster at port 0x220".
ECHO  Correct for a real SB/Vibra16 box. Ctrl-C now and re-run with PG if
ECHO  a PicoGUS is what is installed.
:SNDOK2
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\AUTO.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_FASTPATH_OVERSIZED=0
SET DOSKUTSU_LOG_TAG=%QAM%KS
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  CELL KS  backdrop placement   log %QAM%KS
ECHO  RESET once you have seen the first map.
ECHO ============================================
DOSKUTSU.EXE
SET SDL_HINT_DOSKUTSU_FASTPATH_OVERSIZED=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  VIDKS DONE. Killswitch cleared. VIDM rows untouched.
ECHO ============================================================
:END
