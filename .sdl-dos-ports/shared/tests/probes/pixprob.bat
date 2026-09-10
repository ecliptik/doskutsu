@ECHO OFF
REM PIXPROB.BAT - SfxSynth synth cost probe (wave-22.5 / iter H).
REM Measures cost of SfxSynth SFX synthesis at M=256/512/1024 sample buffers
REM and K=1/4/8 SFX-per-flip rates. Decides slot 0114 alternate-flip ship.
REM Output: PIXPROB.LOG (RDTSC cycle counts + ms-converted summary).
REM Runtime: ~3-8 sec on PODP83. No hang risk.
ECHO Running PIXPROB SfxSynth synth probe (writes PIXPROB.LOG, ~3-8 sec)...
PIXPROB.EXE
ECHO ----
ECHO Probe finished. PIXPROB.LOG contents:
TYPE PIXPROB.LOG
