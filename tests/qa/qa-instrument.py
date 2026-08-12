#!/usr/bin/env python3
"""Instrument the QA sweep BATs: hardware banner, lane manifest, unique WB tag.
Idempotent. Preserves CRLF. Usage: qa-instrument.py <dir-containing-BATs>"""
import sys, os, re

SWEEPS = {
 'PG.BAT':   dict(name='PicoGUS sweep (sb + gus + adlib)', cells='13', mins='~50',
                  snd='PicoGUS -- DreamBlaster on the PicoGUS header',
                  vid='S3 ViRGE', nfo='PG', wb='17P'),
 'VB.BAT':   dict(name='Vibra16 sweep (AUTO + WB + OPL3 + Organya)', cells='6', mins='~13',
                  snd='Vibra16 -- DreamBlaster on the Vibra header',
                  vid='S3 ViRGE', nfo='VB', wb='17V'),
 'VIDV.BAT': dict(name='Video lane -- S3 ViRGE', cells='2', mins='~10',
                  snd='PicoGUS in SB mode', vid='S3 ViRGE', nfo='VIDV', wb=None),
 'VIDC.BAT': dict(name='Video lane -- Cirrus CL-GD5430', cells='2', mins='~10',
                  snd='PicoGUS in SB mode', vid='Cirrus CL-GD5430', nfo='VIDC', wb=None),
 'VIDM.BAT': dict(name='Video lane -- ATI Mach64', cells='2', mins='~10',
                  snd='PicoGUS in SB mode', vid='ATI Mach64', nfo='VIDM', wb=None),
}
CPU = [('1','Pentium OverDrive 83','G'), ('2','Am5x86-133','A'),
       ('3','486DX2-66','6'), ('4','486DX2-50','5')]
MARK = 'REM --- QA-INSTRUMENT v1 ---'

def crlf(lines): return "".join(l + "\r\n" for l in lines)

def patch(path, cfg):
    raw = open(path, 'rb').read().decode('ascii')
    lines = raw.split("\r\n")
    if lines and lines[-1] == '': lines.pop()
    if any(MARK in l for l in lines):
        return 'skip (already instrumented)'
    out, done_cpu, done_start = [], False, False
    for l in lines:
        # unique WaveBlaster tag so PG and VB stop colliding on <M>17
        if cfg['wb']:
            l = re.sub(r'^SET DOSKUTSU_LOG_TAG=%QAM%17$',
                       'SET DOSKUTSU_LOG_TAG=%QAM%' + cfg['wb'], l)
            l = re.sub(r'log %QAM%17$', 'log %QAM%' + cfg['wb'], l)
        out.append(l)
        if not done_cpu and re.match(r'^IF "%2"=="4" SET QAM=5$', l):
            out.append(MARK)
            for n, name, _ in CPU:
                out.append(f'IF "%2"=="{n}" SET QACPU={name}')
            done_cpu = True
        if not done_start and l.strip() == ':START':
            n = cfg['nfo']
            out += [
              MARK,
              'IF NOT EXIST LOGS\\NUL MKDIR LOGS',
              'ECHO ============================================================',
              f'ECHO  SWEEP : {cfg["name"]}',
              f'ECHO  CELLS : {cfg["cells"]} cells, {cfg["mins"]} min',
              'ECHO  CPU   : %QACPU%',
              f'ECHO  SOUND : {cfg["snd"]}',
              f'ECHO  VIDEO : {cfg["vid"]}',
              'ECHO  LOGS  : tagged %QAM%..  in LOGS\\',
              'ECHO ============================================================',
              'ECHO  Check the CPU and cards above match the box before starting.',
              'ECHO ============================================================',
              f'ECHO DOSKUTSU QA lane manifest > LOGS\\%QAM%{n}.NFO',
              f'ECHO sweep={n} >> LOGS\\%QAM%{n}.NFO',
              f'ECHO cells={cfg["cells"]} >> LOGS\\%QAM%{n}.NFO',
              f'ECHO cpu=%QACPU% >> LOGS\\%QAM%{n}.NFO',
              f'ECHO cpu_arg=%2 >> LOGS\\%QAM%{n}.NFO',
              f'ECHO log_tag_prefix=%QAM% >> LOGS\\%QAM%{n}.NFO',
              f'ECHO sound={cfg["snd"]} >> LOGS\\%QAM%{n}.NFO',
              f'ECHO video={cfg["vid"]} >> LOGS\\%QAM%{n}.NFO',
              f'ECHO note=run order is recoverable from the [HH:MM:SS] prefixes in the cell logs >> LOGS\\%QAM%{n}.NFO',
              MARK,
            ]
            done_start = True
    if not (done_cpu and done_start):
        return f'FAIL (cpu_arm={done_cpu} start={done_start}) -- unexpected BAT shape'
    body = crlf(out)
    if "\r\r" in body: return 'FAIL double-CR'
    open(path + '.BAK', 'wb').write(raw.encode('ascii'))
    open(path, 'wb').write(body.encode('ascii'))
    return 'instrumented'

d = sys.argv[1]
rc = 0
for b, cfg in SWEEPS.items():
    p = os.path.join(d, b)
    if not os.path.exists(p):
        print(f'  {b:10s} not present, skipped'); continue
    r = patch(p, cfg)
    print(f'  {b:10s} {r}')
    if r.startswith('FAIL'): rc = 1
sys.exit(rc)
