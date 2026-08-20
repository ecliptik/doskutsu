@ECHO OFF
REM ===================================================================
REM  ADIAG -- what does audio actually cost? Ablation ladder.
REM
REM  WHY. The ~68%-of-frame bucket has been called "audio+engine" since
REM  the campaign began. Patch 0304 showed most of it is engine, and
REM  round I read audio_mixer_calls=0 -- but that counter brackets
REM  essentially ONE function (runFade). SB16 DMA servicing, Pixtone SFX
REM  synthesis and the IRQ-driven mixer are outside it, and the SDL-side
REM  audio brackets (P12_AUDIO_INSTR) have never been switched on in any
REM  cell of this campaign. So "audio is ~0" was never measured.
REM
REM  ABLATION, NOT BRACKETS, on purpose. A bracket can only see code it
REM  wraps. Audio on this platform runs partly in an IRQ handler and a
REM  DMA refill cycle that no engine-side bracket observes. Turning the
REM  work OFF and reading the frame rate captures all of it, including
REM  the parts nothing is instrumented to see.
REM
REM  ALL FOUR CELLS ARE FPS ROWS. No instrumentation is set -- the whole
REM  point is a clean wall-clock comparison. Compare them to each other.
REM
REM  READ CELL 4 CAREFULLY. Per the archived finding
REM  audio_mix_zero_is_not_audio_off: SDL_InitSubSystem(SDL_INIT_AUDIO)
REM  + Mix_OpenAudio hook the SB16 IRQ at the SDL3-DOS backend level, and
REM  that IRQ fires at the buffer-refill rate (~86 Hz at 22050/256)
REM  whether or not anything is queued. If AUDIO_OFF only silences the
REM  mixer and leaves the device open, cell 4 is a FLOOR on audio cost,
REM  not a ceiling. Check the log's audio-init banner to see which
REM  happened before treating cell 4 as the total.
REM
REM  Usage:  QA [n]   then   ADIAG [n]      (switches the PicoGUS itself)
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
ECHO  ADIAG -- audio ablation lane
ECHO ============================================================
ECHO  Give it the CPU in the box:  ADIAG 1  (1 POD-83  2 Am5x86
ECHO  3 DX2-66  4 DX2-50). Nothing ran.
GOTO END
:START
IF NOT EXIST LOGS\NUL MKDIR LOGS
ECHO DOSKUTSU QA lane manifest > LOGS\%QAM%ADIAG.NFO
ECHO sweep=ADIAG >> LOGS\%QAM%ADIAG.NFO
ECHO cells=4 >> LOGS\%QAM%ADIAG.NFO
ECHO cpu=%QACPU% >> LOGS\%QAM%ADIAG.NFO
ECHO cpu_arg=%2 >> LOGS\%QAM%ADIAG.NFO
ECHO log_tag_prefix=%QAM% >> LOGS\%QAM%ADIAG.NFO
REM  Record the CONFIG.SYS boot profile. Unlike a SET, %config% is
REM  chosen at the boot menu and cannot be retrofitted onto a run that
REM  did not boot that way, so a log PROVES its own provenance rather
REM  than attesting to the absence of something. Catches a cell run on
REM  the wrong profile -- e.g. VIBRAUSB with no Vibra fitted, which
REM  yields no DAC at all and would otherwise look like silent audio
REM  with no record of why.
ECHO config=%config% >> LOGS\%QAM%ADIAG.NFO
ECHO sound=PicoGUS forced to SB mode by this sweep >> LOGS\%QAM%ADIAG.NFO
ECHO note=audio ablation -- ALL FOUR cells are fps rows >> LOGS\%QAM%ADIAG.NFO
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
ECHO  AUDIO ABLATION -- all four cells ARE fps rows
ECHO  machine %QAM%   4 cells   tags %QAM%A0 %QAM%AM %QAM%AS %QAM%AX
ECHO ============================================================
ECHO   1 baseline      everything on
ECHO   2 MUSIC_OFF     music mixing removed
ECHO   3 SFX_OFF       Pixtone SFX removed
ECHO   4 AUDIO_OFF     as much as the hint actually disables
ECHO.
ECHO  Cells 2-4 will SOUND wrong. That is the point.
ECHO  The fps gap from cell 1 is what audio costs.
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
SET DOSKUTSU_LOG_TAG=%QAM%A0
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [1/4]  BASELINE   log %QAM%A0
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_MUSIC_OFF=1
SET DOSKUTSU_LOG_TAG=%QAM%AM
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [2/4]  MUSIC_OFF   log %QAM%AM   (silence is expected)
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_SFX_OFF=1
SET DOSKUTSU_LOG_TAG=%QAM%AS
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [3/4]  SFX_OFF   log %QAM%AS   (music only, no effects)
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
CALL CLRENV
IF EXIST PROFILE.DAT DEL PROFILE.DAT
COPY CFGS\OPL3.CFG DOSKUTSU.CFG > NUL
SET SDL_HINT_DOSKUTSU_AUDIO_OFF=1
SET DOSKUTSU_LOG_TAG=%QAM%AX
SET DOSKUTSU_TAS_REPLAY=QA.TAS
SET DOSKUTSU_TAS_PRNG_SEED=12345
SET DOSKUTSU_TAS_AUTO_EXIT_TICK=30000
ECHO.
ECHO ============================================
ECHO  [4/4]  AUDIO_OFF   log %QAM%AX   (fully silent)
ECHO  check the log: did the audio DEVICE close, or only the mixer?
ECHO ============================================
DOSKUTSU.EXE
IF "%DKTCAP%"=="1" IF EXIST C:\VGACAP\MODE12.COM C:\VGACAP\MODE12.COM
SET SDL_HINT_DOSKUTSU_MUSIC_OFF=
SET SDL_HINT_DOSKUTSU_SFX_OFF=
SET SDL_HINT_DOSKUTSU_AUDIO_OFF=
SET DOSKUTSU_TAS_REPLAY=
ECHO.
ECHO ============================================================
ECHO  ADIAG DONE -- logs %QAM%A0 %QAM%AM %QAM%AS %QAM%AX
ECHO  All three hints cleared -- safe to run other sweeps.
ECHO ============================================================
:END
