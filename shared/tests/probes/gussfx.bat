@ECHO OFF
REM GUSSFX.BAT - GF1 SFX-upload + per-slot trigger probe (GUS #38, iter-1).
REM probe-suite. Pure DJGPP; CWSDPMI.EXE same dir. Writes GUSSFX.LOG (CWD).
REM
REM Operator prereqs (PicoGUS v2.0 firmware picogus-gus, jumpers IRQ7+DMA3):
REM   SET ULTRASND=240,3,3,7,7
REM   pgusinit /mode gus
REM   pgusinit /gusdma 12
REM
REM WHAT THIS DOES: reproduces nx-0239 initGusSfx WITHOUT the engine. Step 1
REM (MAP) emulates the 8-bit SFX DRAM-placement sequence to ~715KB and logs each
REM slot's [start,end) + a 256KB-bank STRADDLE flag (the H1 suspects). Step 2
REM (TRIG) then triggers each uploaded slot one-at-a-time, ~800ms each. The slot
REM that QUITS TO DOS leaves its TRIG line as the LAST line of GUSSFX.LOG = the
REM crasher. A slot that beeps then advances is innocent.
REM
REM H1 (leading): gus_dram_alloc only bank-aligns 16-bit; an 8-bit SFX straddling
REM   a 256KB bank is the suspect. If the crashing slot has straddle=1 then H1.
REM H2 (sibling): engine passes len past buffer, host over-read. This probe is
REM   GF1-side only (generates its bytes), so if NOTHING here crashes -- not even
REM   the STRADDLE A/B cells -- H1 is refuted and the bug is engine-side (H2).
REM
REM NOTE: g2k is the judge -- DOSBox-X will NOT reproduce the PicoGUS crash.
REM Diag-wedge rule: this probe does NO GF1 port-reads (write-only, bounded).
ECHO ===== GUSSFX #38 -- STEP 1: SFX DRAM map (no triggers, safe) =====
GUSSFX.EXE MAP
ECHO ----
ECHO Map written. GUSSFX.LOG per-slot [start,end) + STRADDLE flags:
TYPE GUSSFX.LOG
ECHO ----
ECHO ===== GUSSFX #38 -- STEP 2: per-slot TRIGGER (pin the crasher) =====
ECHO If the machine quits to DOS, GUSSFX.LOG's LAST line names the slot.
ECHO Re-run after a crash with:  GUSSFX TRIG FROM=n TO=m   to bisect.
GUSSFX.EXE TRIG
ECHO ----
ECHO Trigger sweep survived (no crash this run). Full log:
TYPE GUSSFX.LOG
ECHO ----
ECHO Controlled H1 A/B (optional): GUSSFX STRADDLE -- control-below vs straddling
ECHO sample at each 256KB boundary; if straddlers crash and controls play = H1.
