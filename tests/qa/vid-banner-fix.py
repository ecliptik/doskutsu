#!/usr/bin/env python3
"""Make the VID* banner and .NFO tell the truth about which SB will be used.
Idempotent, CRLF-preserving. Usage: vid-banner-fix.py <dir>"""
import sys, os
MARK = "REM --- SOUND-BANNER v1 ---"
def patch(p, tag):
    raw = open(p,'rb').read().decode('ascii')
    if MARK in raw: return "skip (already patched)"
    lines = raw.split("\r\n")
    if lines and lines[-1]=='': lines.pop()
    b = "ECHO  SOUND : PicoGUS in SB mode"
    n = f"ECHO sound=PicoGUS in SB mode >> LOGS\\%QAM%{tag}.NFO"
    if b not in lines or n not in lines: return "FAIL: banner/NFO line not found"
    lines[lines.index(b):lines.index(b)+1] = [
      MARK,
      'IF "%3"=="PG" ECHO  SOUND : PicoGUS -- will switch it to SB mode',
      'IF NOT "%3"=="PG" ECHO  SOUND : the SB already in the box (add PG for a PicoGUS)',
      MARK,
    ]
    i = lines.index(n)
    lines[i:i+1] = [
      MARK,
      f'IF "%3"=="PG" ECHO sound=PicoGUS in SB mode >> LOGS\\%QAM%{tag}.NFO',
      f'IF NOT "%3"=="PG" ECHO sound=SB already in the box >> LOGS\\%QAM%{tag}.NFO',
      MARK,
    ]
    body = "".join(l+"\r\n" for l in lines)
    if "\r\r" in body: return "FAIL double-CR"
    open(p,"wb").write(body.encode('ascii'))
    return "patched"
d = sys.argv[1]
for b, tag in (("VIDV.BAT","VIDV"), ("VIDC.BAT","VIDC"), ("VIDM.BAT","VIDM")):
    p = os.path.join(d,b)
    print(f"  {b:10s} {patch(p,tag) if os.path.exists(p) else 'not present'}")
