#!/bin/bash
# Canonical round-2 cell extraction. Regenerate: bash this from qa-results/.
REF="72 20 11 17 11 15 11 19 11 14 11"
echo "cell,cpu,dataset,backend,music_pump,flips,render_s,per_loop_fps,overhead_s,median_fps,route"
emit(){ local d="$1" cpu="$2" name="${3:-$(basename "$1")}"
  for L in "$d"/*.LOG; do
    b=$(basename "$L" .LOG); case "$b" in *SDL) continue;; esac
    ft=$(grep -a -m1 "fps-true" "$L") || true
    fl=$(echo "$ft"|grep -o "flips=[0-9]*"|cut -d= -f2); rs=$(echo "$ft"|grep -o "render_s=[0-9]*"|cut -d= -f2)
    [ -z "$fl" ] && continue
    be=$(grep -a -m1 -o "audio backend: [a-z0-9]*" "$L"|sed 's/.*: //')
    pu=$(grep -ac "PIT/IRQ-0 pump\|OPL timer pump STARTED" "$L"); [ "$pu" -gt 0 ] && pump=1 || pump=0
    rt=$(grep -a -o "Entering stage [0-9]*" "$L"|sed 's/Entering stage //'|tr '\n' ' '|sed 's/ $//')
    [ "$rt" = "$REF" ] && route=full || route=truncated
    if [ "$pump" -eq 0 ]; then
      med=$(grep -ao "inter_flip_ms=[0-9]*" "$L"|sed 's/.*=//'|sort -n|awk '{a[NR]=$1}END{if(NR)printf "%.1f",1000/a[int(NR/2)+1]}')
    else med="NA_pump"; fi
    awk -v c="$b" -v cpu="$cpu" -v ds="$name" -v be="$be" -v p="$pump" -v f="$fl" -v r="$rs" -v m="$med" -v ro="$route" \
      'BEGIN{printf "%s,%s,%s,%s,%d,%d,%d,%.2f,%d,%s,%s\n",c,cpu,ds,be,p,f,r,f/102,r-102,m,ro}'
  done }
emit 2026-08-13-r2-DX250   486DX2-50
emit 2026-08-13-r2-DX266   486DX2-66
emit 2026-08-13-r2-Am5x86  Am5x86-133
emit 2026-08-13-r2-POD83   POD-83
emit 2026-08-13-r2-mach64  486DX2-50
