@ECHO OFF
REM ===================================================================
REM  PUMP -- what does the event pump cost? PAIRED ablation.
REM
REM  Conformance: HARNESS-STANDARD.md, class=perf means L3.
REM  Profile: HARNESS-PROFILE-DOSKUTSU.md.
REM
REM  The POD's frame is 32.8 ms in-loop. Round N accounted for all but
REM  ~3.9 ms of it (tilemap 21.05, flip 4.55, sim 2.39, HUD+post 0.94).
REM  That remaining 12%% has never been instrumented on ANY machine.
REM  Most of it should be input_poll, which runs ~1.67x per frame and is
REM  entirely unbracketed.
REM
REM  DOSKUTSU_NO_INPUT_POLL=1 skips the event pump. The gap between the
REM  arms is its total cost, including the cooperative-scheduler yield
REM  that happens inside it. No patch and no new lever -- the ablation
REM  has been compiled into the binary since patch 0063.
REM
REM    cell 1  P0   control
REM    cell 2  PA   NO_INPUT_POLL=1
REM    cell 3  P0B  control, repeat
REM    cell 4  PAB  NO_INPUT_POLL=1, repeat
REM
REM  ORDER IS LOAD-BEARING and is control-arm-control-arm on purpose.
REM  Interleaving brackets each arm between controls, so a monotonic
REM  session drift shows up in the control pair instead of being
REM  charged to the arm. Do not reorder to group the arms.
REM
REM  BOTH ARMS ARE REPEATED ON PURPOSE. The control pair gives this
REM  round its own noise band and the ablated pair proves the delta is
REM  not a single-sample artifact. The measured floor is 0.2 fps and a
REM  0.6 fps result once failed to reproduce, so an unpaired arm is not
REM  evidence. Control is IN THIS SESSION, not borrowed from round O --
REM  cross-round comparison has been unreliable in this project.
REM
REM  AUDIO WILL PROBABLY MISBEHAVE in cells 2 and 4. The event pump is
REM  where the SDL3-DOS cooperative scheduler yields, so skipping it
REM  starves the audio thread. Expected, not a fault. This is an
REM  ablation to size a cost -- it is NOT a shippable lever and these
REM  are NOT fps rows for the matrix.
REM
REM  The reel drives input under TAS, so the run completes even though
REM  the game is uncontrollable by hand. If a cell hangs or exits early
REM  instead of completing, THAT IS THE RESULT -- report it, do not
REM  retry and present the retry as the result (standard sec. 7.4).
REM
REM  Optional declared-hardware vars, set before running (empty is
REM  honest -- it reads as "nobody said", do not invent a default):
REM    SET QAVID=VIRGE      VIRGE / MACH64 / CIRRUS
REM    SET QASND=PICOGUS    PICOGUS / PICOGUS+VIBRA / VIBRA
REM    SET QAIRQ=NONE       resident interrupt sources armed, or NONE
REM
REM  Usage:  QA [n]   then   PUMP [n]      (switches the PicoGUS itself)
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
ECHO  PUMP -- event-pump ablation, paired
ECHO ============================================================
ECHO  Give it the CPU in the box:  PUMP 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
SET QANFO=LOGS\%QAM%PUMP.NFO
IF "%QAVID%"=="" SET QAVID=UNDECLARED
IF "%QASND%"=="" SET QASND=UNDECLARED
IF "%QAIRQ%"=="" SET QAIRQ=UNDECLARED
ECHO DOSKUTSU QA lane manifest > %QANFO%
ECHO schema=harness-1 >> %QANFO%
ECHO sweep=PUMP >> %QANFO%
ECHO cells=4 >> %QANFO%
ECHO cpu=%QACPU% >> %QANFO%
ECHO cpu_arg=%2 >> %QANFO%
ECHO log_tag_prefix=%QAM% >> %QANFO%
ECHO config=%config% >> %QANFO%
ECHO video_declared=%QAVID% >> %QANFO%
ECHO sound_declared=%QASND% >> %QANFO%
ECHO irq_source=%QAIRQ% >> %QANFO%
ECHO dktcap=%DKTCAP% >> %QANFO%
ECHO order_is_load_bearing=yes >> %QANFO%
ECHO cell=%QAM%PU0 class=perf arm=control >> %QANFO%
ECHO cell=%QAM%PUA class=perf arm=no_input_poll >> %QANFO%
ECHO cell=%QAM%PU0B class=perf arm=control repeat_of=%QAM%PU0 >> %QANFO%
ECHO cell=%QAM%PUAB class=perf arm=no_input_poll repeat_of=%QAM%PUA >> %QANFO%
ECHO note=event-pump ablation -- paired arms, NOT fps rows >> %QANFO%
IF NOT "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=10000
IF "%DKTCAP%"=="1" SET SDL_HINT_DOSKUTSU_BANNER_DELAY_MS=0
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
IF "%DKTCAP%"=="1" ECHO  [DKTCAP=1] capture session: banner delay off, console
IF "%DKTCAP%"=="1" ECHO  returns to mode 12h between cells (text will be SLOW).
IF "%DKTCAP%"=="1" ECHO  SET DKTCAP= to turn off; VGACAP\MODE03 restores text.
ECHO.
ECHO ============================================================
ECHO  PUMP -- event-pump ablation, 4 cells, both arms repeated
ECHO  CPU %QACPU%   tags %QAM%PU0 %QAM%PUA %QAM%PU0B %QAM%PUAB
ECHO ============================================================
ECHO  Order is load-bearing: control, arm, control, arm.
ECHO  Cells 2 and 4 may sound wrong. That is expected.
ECHO  A cell that hangs or exits early IS the result -- report it.
ECHO ============================================================
PAUSE
ECHO.
ECHO  switching PicoGUS to SB MODE
pgusinit /mode sb > PGOUT.TMP
TYPE PGOUT.TMP
ECHO pgusmode_readback=begin >> %QANFO%
TYPE PGOUT.TMP >> %QANFO%
ECHO pgusmode_readback=end >> %QANFO%
pgusinit /sbenv > PGOUT.TMP
TYPE PGOUT.TMP
ECHO sbenv_readback=begin >> %QANFO%
TYPE PGOUT.TMP >> %QANFO%
ECHO sbenv_readback=end >> %QANFO%
IF EXIST PGOUT.TMP DEL PGOUT.TMP
SET BLASTER=A220 I7 D3 P330 T3
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%PU0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/4]  CONTROL   log %QAM%PU0
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%PU0
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_NO_INPUT_POLL=1
SET DOSKUTSU_LOG_TAG=%QAM%PUA
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/4]  PUMP OFF   log %QAM%PUA   (audio may sound wrong)
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%PUA
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET DOSKUTSU_NO_INPUT_POLL=
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_LOG_TAG=%QAM%PU0B
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/4]  CONTROL repeat   log %QAM%PU0B
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%PU0B
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET DOSKUTSU_NO_INPUT_POLL=1
SET DOSKUTSU_LOG_TAG=%QAM%PUAB
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/4]  PUMP OFF repeat   log %QAM%PUAB
ECHO ============================================
DOSKUTSU.EXE
ECHO  CELL DONE %QAM%PUAB
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET DOSKUTSU_NO_INPUT_POLL=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  PUMP DONE. Logs %QAM%PU0 %QAM%PUA %QAM%PU0B %QAM%PUAB
ECHO  Manifest %QANFO%
ECHO  Control pair spread IS this session's noise band.
ECHO  A delta under that band is UNRESOLVED, not zero.
ECHO ============================================================
ECHO.
:END
