@ECHO OFF
REM ORGSYNTH.BAT - Organya live-synth cost benchmark (wave-38 audio Tier 2).
REM Ports Song::Synth nearest-neighbour per-sample math from Organya.cpp with
REM synthetic wavetable (PIXPROB precedent; measures instruction mix, not output).
REM Informs wave-39 P1 (OPL3 backend) + P2 (WaveBlaster MIDI) candidate dispatch.
REM 4 rate variants × 6 simulated instruments. 200 chunks per scenario.
REM Output: ORGSYNTH.LOG (per-rate RDTSC stats + synth_cost_us_per_sec_output).
REM Runtime: ~10-20 sec total. No env vars required.
REM No HAZARD: pure offline synth; no IRQ/DMA/SDL/audio-device touched.
REM 60-sec operator watchdog: if no DOS prompt after 60 sec, Ctrl-Alt-Del is safe.
ECHO Running ORGSYNTH probe (Organya live-synth cost)...
ORGSYNTH.EXE
ECHO ----
ECHO Probe finished. ORGSYNTH.LOG tail:
TYPE ORGSYNTH.LOG
