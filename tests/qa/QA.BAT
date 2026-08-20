@ECHO OFF
IF "%1"=="GO" GOTO GO
COMMAND /E:2048 /K %0 GO %1 %2 %3
GOTO END
:GO
IF "%2"=="1" SET QAM=G
IF "%2"=="2" SET QAM=A
IF "%2"=="3" SET QAM=6
IF "%2"=="4" SET QAM=5
IF NOT "%3"=="" SET QAVID=%3
IF NOT "%4"=="" SET QASND=%4
CLS
ECHO ============================================================
ECHO   doskutsu QA   --   what to run
ECHO ============================================================
ECHO   SWEEPS (see docs/QA-CHECKLIST.md for the round)
ECHO     GAP    4 cells  X8 card-vs-DMA (VIBRA), audio floor, ORGHQ
ECHO              on a PICOGUS box type  GAP PG  instead
ECHO     RB     4 cells  re-baseline + noise floor      (PicoGUS)
ECHO     EAR    3 cells  BY EAR: Shack music, AdLib     (PicoGUS)
ECHO     PROVE  6 cells  prove-out a NEW binary         (PicoGUS)
ECHO     PUMP n 4 cells  event-pump ablation, paired    (PicoGUS)
ECHO   These take NO number -- they use the machine set above.
ECHO.
ECHO   ROUND 1 SWEEPS (complete; n = the CPU number)
ECHO     PG n   PicoGUS: 10 cells, sb + gus + adlib + WaveBlaster
ECHO     VB n   Vibra16:  6 cells, AUTO + WB + OPL3 + Organya
ECHO.
ECHO   QA n [VIDEO] [SOUND]   e.g.  QA 1 VIRGE PICOGUS
ECHO   The two names are DECLARED hardware -- they ride into
ECHO   every sweep manifest. Blank reads as "nobody said".
ECHO.
ECHO   n = the CPU that is in the box RIGHT NOW:
ECHO     1 POD-83      2 Am5x86-133     3 DX2-66      4 DX2-50
ECHO   It sets the log tag, so a wrong n files results under the
ECHO   wrong machine. Nothing else decides it.
ECHO.
ECHO   HANDS-ON (Vibra phase)
ECHO     G11 G12 G13 G14 G15   SETUP walk
ECHO     G115 joystick   G116 save/load   G117 free play
ECHO.
ECHO   OTHER:  C1 env probe    RECORD re-record the reel (DO NOT)
ECHO ============================================================
IF "%QAM%"=="" GOTO NOMACH
ECHO   Machine set: QAM=%QAM%   (logs tag %QAM%..)   Type EXIT to leave.
ECHO   Declared:    video=%QAVID%  sound=%QASND%
GOTO END
:NOMACH
ECHO   No CPU set yet. Either run  QA n , or  SET QAM=G  (G/A/6/5).
:END
