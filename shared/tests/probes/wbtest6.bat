@ECHO OFF
REM WBTEST6.BAT - WBTEST-006: DOSMID-faithful bit-6 DRR polling probe.
REM v1.0.2 task #16.
REM
REM Mission: test whether MPU-401 status bit 6 (DRR = Data Receive Ready)
REM polling before every write is the missing piece that unlocks audible
REM wavetable MIDI on Vibra16S CT2490 + Dream SAM2695. DOSMID has been
REM polling bit 6 in production on Vibra16S boards for ~20 years.
REM
REM Single lever: bit-6 polling presence/absence.
REM PATH=DOSMID = direct-port + mpu401_waitwrite(bit 6 == 0) before every
REM outportb. Byte-faithful to DOSMID's MPU401.C.
REM
REM 3 within-binary variants:
REM   V1 (control):     PATH=DOSMID polled, prog=piano, with drum
REM   V2 (no-poll ctl): same byte stream, SKIP the poll, with drum
REM   V3:               PATH=DOSMID polled, prog=bass (0x20), no drum
REM
REM Per-variant: ~6 sec audio. 3 variants + 2 gaps = ~20 sec total.
REM
REM Decision tree (full table in WBTEST6.LOG):
REM   V1 piano + bass drum  -> H20 CONFIRMED. Bit-6 polling is the production fix.
REM   V1 silent/boops/non-GM -> polling alone isn't the unlock; campaign closes.
REM   V2 audible            -> some other DOSMID quirk is the unlock, not polling.
REM   V3 distinct from V1   -> prog-change works through polled path.
REM
REM Operator: PicoGUS expected PHYSICALLY OUT (same config as WBTEST-002b+).
REM
REM HAZARD: low. Poll loop has 5ms wallclock cap per call (DOSMID itself uses
REM unbounded but has been stable for ~20 years on this chip family).
REM Post-variant waitwrite stats logged; cap_hits>0 = chip bit 6 also-lies
REM (different problem class).
ECHO Running WBTEST-006 (DOSMID-faithful bit-6 polling; 3 variants)...
ECHO   ~0:00         boot diag (BLASTER + DSP + 39-reg mixer dump)
ECHO   ~0:01-0:07   V1   DOSMID polled + piano + drum (control)
ECHO   ~0:08-0:14   V2   no-poll same bytes + piano + drum (discriminator)
ECHO   ~0:15-0:20   V3   DOSMID polled + bass program (prog-change test)
ECHO Listen for: V1 piano+kick = H20 WIN; V2 differs from V1 = polling matters.
WBTEST6.EXE A
ECHO ----
ECHO Probe finished. WBTEST6.LOG tail:
TYPE WBTEST6.LOG
ECHO DONE
