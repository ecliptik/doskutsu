#!/usr/bin/env bash
# 86box-probe-run.sh -- generic single-probe boot harness for 86Box.
#
# Companion to:
#   - tools/86box-run.sh            (HWINV smoke; hardwired to the image's
#                                    pre-installed :HWINV AUTOEXEC label)
#   - tools/86box-multi-cell-run.sh (DOSKUTSU PLAY-matrix; scratch image +
#                                    AUTOEXEC hook + per-cell NEXT.BAT shim)
#
# Why this script exists: 86box-run.sh can only boot HWINV.EXE (the image
# has a dedicated :HWINV menu/AUTOEXEC slot). A HAZARD-class probe such as
# blttile.c (direct Cirrus 5434 BitBLT register programming) MUST run its
# real code path under 86Box before real-HW -- 86Box emulates the Cirrus
# 5434, so the probe does NOT take its SKIP_NOT_5434 path; DOSBox-X cannot
# exercise the chip path at all. This script gives any <NAME>.EXE probe
# the same scratch-image + AUTOEXEC-hook + NEXT.BAT-shim boot that
# 86box-multi-cell-run.sh uses for PLAY cells, without the DOSKUTSU
# PLAY-matrix coupling (no DOSKUTSU.EXE, no PLAY.TAS, no SDL log).
#
# Mechanism (identical scratch-image discipline to the multi-cell harness):
#   1. Snapshot ~/g2k-cf.img to a scratch image (canonical NEVER mutated).
#   2. mcopy <NAME>.EXE into C:\DOSKUTSU\ (CWSDPMI.EXE is already there in
#      the operator's CF state). The probe's .bat, if given, is pushed too
#      for manual use but is NOT on the harness boot path -- see step 4.
#   3. Append `CALL C:\NEXT.BAT` to AUTOEXEC.BAT after the :END label.
#   4. Write C:\NEXT.BAT: CD C:\DOSKUTSU + run <NAME>.EXE DIRECTLY + drop
#      sentinel. The probe EXE is invoked directly, NOT via `CALL
#      <NAME>.BAT`: the extra batch-nesting level of a CALL'd probe .bat
#      (AUTOEXEC -> NEXT.BAT -> probe.bat -> EXE) exhausts MS-DOS file
#      handles on the fully-driver-loaded g2k boot, so the probe's
#      fopen() of its own log silently fails. Running the EXE directly
#      (AUTOEXEC -> NEXT.BAT -> EXE) keeps a handle free. The probe .bat
#      is operator UX (banner + TYPE of the log); the harness extracts
#      the LOG file itself so the .bat adds nothing on the harness path.
#   5. Boot 86Box; poll for C:\<NAME>.DON; SIGTERM on sentinel or timeout.
#   6. mcopy <NAME>.LOG out (C:\DOSKUTSU\ primary, C:\ fallback -- matches
#      the CWD-primary / C:\-fallback fopen pattern probes use).
#   7. Discard scratch image; append a G2K-CF-IMAGE-CHANGES.md entry.
#
# This is a functional-no-hang gate, NOT a perf measurement -- 86Box
# timing is not a real-HW proxy ([[dosbox_not_proxy]]). The verdict is
# "the probe's chip path ran to SUITE_DONE without hanging the VM".
#
# 86Box binary / ROM / NVR / conf requirements: identical to
# tools/86box-multi-cell-run.sh -- see that file's header.
#
# Exit codes:
#   0  -- probe ran, <NAME>.LOG captured
#   2  -- bootstrap prerequisite missing (binary / ROMs / image / NVR / mtools)
#   4  -- <NAME>.LOG not produced (probe crashed, or VM never reached it)
#   5  -- invocation error (bad CLI args)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Bootstrap-overridable paths (same defaults as 86box-multi-cell-run.sh).
BOX86_BINARY="${BOX86_BINARY:-$HOME/emulators/86box/86Box-doskutsu}"
BOX86_ROMPATH="${BOX86_ROMPATH:-$HOME/emulators/86box/roms}"
BOX86_IMG="${BOX86_IMG:-$HOME/g2k-cf.img}"
BOX86_NVR_SRC="${BOX86_NVR_SRC:-/tmp/86box-PLAY0/nvr/ap5s.nvr}"
BOX86_DISPLAY="${BOX86_DISPLAY:-:0}"
BOX86_CONF="$SCRIPT_DIR/86box-x.conf"

IMG_FAT_OFFSET=32256

# CLI
PROBE_EXE=""
PROBE_BAT=""
NAME=""
LOG_PATH=""
TIMEOUT_S=300
KEEP_SCRATCH=0

usage() {
    cat <<'USAGE'
Usage: 86box-probe-run.sh --probe-exe PATH --log PATH [options]
  --probe-exe PATH   host-side DOS probe .exe -> C:\DOSKUTSU\<NAME>.EXE
                     (required; the harness runs this EXE directly)
  --probe-bat PATH   optional host-side probe .bat -> C:\DOSKUTSU\<NAME>.BAT
                     (pushed for manual use only; NOT on the boot path)
  --name NAME        probe 8.3 basename (default: from --probe-exe, uppercased)
  --log PATH         where to copy the captured <NAME>.LOG
  --timeout SEC      max wall-clock for the probe run (default 300)
  --keep-scratch     preserve scratch image + vmpath post-run (debug)
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --probe-exe)   PROBE_EXE="$2"; shift 2 ;;
        --probe-bat)   PROBE_BAT="$2"; shift 2 ;;
        --name)        NAME="$2"; shift 2 ;;
        --log)         LOG_PATH="$2"; shift 2 ;;
        --timeout)     TIMEOUT_S="$2"; shift 2 ;;
        --keep-scratch) KEEP_SCRATCH=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "86box-probe-run.sh: unknown arg: $1" >&2; usage; exit 5 ;;
    esac
done

# Invocation checks
if [[ -z "$PROBE_EXE" || -z "$LOG_PATH" ]]; then
    echo "86box-probe-run.sh: --probe-exe and --log are required" >&2
    usage; exit 5
fi
if [[ ! -f "$PROBE_EXE" ]]; then
    echo "86box-probe-run.sh: --probe-exe not found: $PROBE_EXE" >&2; exit 5
fi
if [[ -n "$PROBE_BAT" && ! -f "$PROBE_BAT" ]]; then
    echo "86box-probe-run.sh: --probe-bat not found: $PROBE_BAT" >&2; exit 5
fi

# Derive NAME from the probe exe basename if not given; uppercase, strip
# extension. The DOS 8.3 name on the image is <NAME>.EXE / <NAME>.BAT.
if [[ -z "$NAME" ]]; then
    NAME="$(basename "$PROBE_EXE")"
    NAME="${NAME%.*}"
fi
NAME="$(echo "$NAME" | tr '[:lower:]' '[:upper:]')"
if [[ ! "$NAME" =~ ^[A-Z0-9_]{1,8}$ ]]; then
    echo "86box-probe-run.sh: --name '$NAME' is not a valid 8.3 basename" >&2
    exit 5
fi

# Preflight prerequisites
fail=0
if [[ ! -x "$BOX86_BINARY" ]]; then
    echo "86box-probe-run.sh: BOX86_BINARY not executable: $BOX86_BINARY" >&2; fail=1
fi
if [[ ! -d "$BOX86_ROMPATH" ]]; then
    echo "86box-probe-run.sh: BOX86_ROMPATH missing: $BOX86_ROMPATH" >&2; fail=1
fi
if [[ ! -f "$BOX86_IMG" ]]; then
    echo "86box-probe-run.sh: BOX86_IMG missing: $BOX86_IMG" >&2; fail=1
fi
if [[ ! -f "$BOX86_NVR_SRC" ]]; then
    echo "86box-probe-run.sh: BOX86_NVR_SRC missing: $BOX86_NVR_SRC" >&2
    echo "  (run tools/86box-run.sh once to populate; or override via env)" >&2
    fail=1
fi
if [[ ! -f "$BOX86_CONF" ]]; then
    echo "86box-probe-run.sh: conf missing: $BOX86_CONF" >&2; fail=1
fi
if ! command -v mcopy >/dev/null 2>&1 || ! command -v mdir >/dev/null 2>&1; then
    echo "86box-probe-run.sh: mtools missing (apt install mtools)" >&2; fail=1
fi
if [[ "$fail" == "1" ]]; then exit 2; fi

mkdir -p "$(dirname "$LOG_PATH")"

echo "[probe-run] probe=$NAME exe=$PROBE_EXE bat=$PROBE_BAT"
echo "[probe-run] pre-flight /tmp state:"
df -h /tmp | tail -1

# Snapshot scratch image (canonical ~/g2k-cf.img is NEVER mutated).
SCRATCH_IMG="/tmp/86box-probe-scratch-$$.img"
echo "[probe-run] snapshotting $BOX86_IMG -> $SCRATCH_IMG"
cp "$BOX86_IMG" "$SCRATCH_IMG"
ORIG_SHA="$(sha256sum "$SCRATCH_IMG" | awk '{print $1}' | head -c 12)"
echo "[probe-run] scratch image sha12=$ORIG_SHA"

# VMpath dir
VMPATH="$(mktemp -d -t 86box-probe-vmpath.XXXXXX)"
cp "$BOX86_CONF" "$VMPATH/86box.cfg"
ln -s "$SCRATCH_IMG" "$VMPATH/g2k-cf.img"
mkdir -p "$VMPATH/nvr"
cp "$BOX86_NVR_SRC" "$VMPATH/nvr/ap5s.nvr"

cleanup() {
    pkill -9 -f "86Box.*$VMPATH" 2>/dev/null || true
    sleep 1
    if [[ "$KEEP_SCRATCH" == "1" ]]; then
        echo "[probe-run] scratch preserved at $SCRATCH_IMG (--keep-scratch)" >&2
        echo "[probe-run] vmpath preserved at $VMPATH (--keep-scratch)" >&2
    else
        rm -f "$SCRATCH_IMG"
        rm -rf "$VMPATH"
    fi
}
trap cleanup EXIT

# --- mcopy the probe into C:\DOSKUTSU\ ---
echo "[probe-run] copying $NAME.EXE into scratch image"
mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$PROBE_EXE" "::/DOSKUTSU/$NAME.EXE"
if [[ -n "$PROBE_BAT" ]]; then
    echo "[probe-run] copying $NAME.BAT into scratch image (manual-use only)"
    mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$PROBE_BAT" "::/DOSKUTSU/$NAME.BAT"
fi

# --- AUTOEXEC.BAT hook: CALL C:\NEXT.BAT after :END (same as multi-cell) ---
mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/AUTOEXEC.BAT" "$VMPATH/AUTOEXEC.ORIG.BAT" 2>/dev/null || true
if [[ ! -f "$VMPATH/AUTOEXEC.ORIG.BAT" ]]; then
    echo "[probe-run] WARNING: image AUTOEXEC.BAT not found; using bare AUTOEXEC" >&2
    {
        printf '@ECHO OFF\r\n'
        printf 'SET BLASTER=A220 I5 D1 H5 T6 J200 P330\r\n'
        printf 'CALL C:\\NEXT.BAT\r\n'
    } > "$VMPATH/AUTOEXEC.NEW.BAT"
else
    awk '
      BEGIN { hooked=0 }
      { print }
      /^:END/ && !hooked { print "CALL C:\\NEXT.BAT"; hooked=1 }
      END {
          if (!hooked) { print ":END"; print "CALL C:\\NEXT.BAT" }
      }
    ' "$VMPATH/AUTOEXEC.ORIG.BAT" > "$VMPATH/AUTOEXEC.NEW.BAT"
fi
tr -d '\r' < "$VMPATH/AUTOEXEC.NEW.BAT" | sed 's/$/\r/' > "$VMPATH/AUTOEXEC.NEW.BAT.crlf"
mv "$VMPATH/AUTOEXEC.NEW.BAT.crlf" "$VMPATH/AUTOEXEC.NEW.BAT"
mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$VMPATH/AUTOEXEC.NEW.BAT" "::/AUTOEXEC.BAT"

# --- C:\NEXT.BAT shim: run the probe EXE directly, drop the sentinel ---
# The probe EXE is run DIRECTLY (not `CALL <NAME>.BAT`) -- the extra
# batch-nesting level of a CALL'd probe .bat exhausts MS-DOS file handles
# on the fully-driver-loaded g2k boot and the probe's log fopen() then
# silently fails. See the header (step 4) for the full rationale.
# The `>` in the ECHO sentinel line is a DELIBERATE redirect (writes the
# sentinel file) -- the only correct use of `>` in a generated BAT.
# Sentinel extension is `.DON` (3 chars) NOT `.DONE`: MS-DOS 8.3 truncates
# a 4-char extension, so `ECHO ... > C:\<NAME>.DONE` actually creates
# C:\<NAME>.DON on the guest -- the poll/clear below must match that.
{
    printf '@ECHO OFF\r\n'
    printf 'REM auto-generated by tools/86box-probe-run.sh\r\n'
    printf 'CD C:\\DOSKUTSU\r\n'
    printf '%s.EXE\r\n' "$NAME"
    printf 'ECHO PROBE_DONE > C:\\%s.DON\r\n' "$NAME"
} > "$VMPATH/NEXT.BAT"
mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$VMPATH/NEXT.BAT" "::/NEXT.BAT"

# Clear stale sentinel + logs so this run is hermetic
mdel -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/${NAME}.DON" 2>/dev/null || true
mdel -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/${NAME}.LOG" 2>/dev/null || true
mdel -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/${NAME}.LOG" 2>/dev/null || true

echo "[probe-run] staged; launching 86Box (timeout ${TIMEOUT_S}s)"
DISPLAY="$BOX86_DISPLAY" "$BOX86_BINARY" \
    -P "$VMPATH" \
    -R "$BOX86_ROMPATH" \
    -V "doskutsu-g2k-parity" \
    -N \
    -L "$VMPATH/${NAME}-86box.log" \
    </dev/null >"$VMPATH/${NAME}-stdout.log" 2>"$VMPATH/${NAME}-stderr.log" &
BOX_PID=$!

# Poll for the sentinel
poll_start=$(date +%s)
sentinel_seen=0
while true; do
    now=$(date +%s)
    elapsed=$((now - poll_start))
    if [[ "$elapsed" -ge "$TIMEOUT_S" ]]; then
        echo "[probe-run] timeout reached without sentinel (probe may have hung)"
        break
    fi
    if ! kill -0 "$BOX_PID" 2>/dev/null; then
        echo "[probe-run] 86Box exited (pid $BOX_PID); finalizing"
        break
    fi
    if mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/${NAME}.DON" - 2>/dev/null | grep -q PROBE_DONE; then
        echo "[probe-run] sentinel seen at +${elapsed}s"
        sentinel_seen=1
        sleep 2
        break
    fi
    sleep 3
done

# Kill 86Box
if kill -0 "$BOX_PID" 2>/dev/null; then
    echo "[probe-run] SIGTERM 86Box (pid $BOX_PID)"
    kill -TERM "$BOX_PID" 2>/dev/null || true
    sleep 3
    if kill -0 "$BOX_PID" 2>/dev/null; then
        kill -KILL "$BOX_PID" 2>/dev/null || true
    fi
    wait "$BOX_PID" 2>/dev/null || true
fi

# Extract <NAME>.LOG -- C:\DOSKUTSU\ primary (probe CWD), C:\ fallback.
captured=0
if mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/${NAME}.LOG" "$LOG_PATH" 2>/dev/null; then
    echo "[probe-run] captured ${NAME}.LOG from C:\\DOSKUTSU\\ ($(wc -l <"$LOG_PATH") lines)"
    captured=1
elif mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/${NAME}.LOG" "$LOG_PATH" 2>/dev/null; then
    echo "[probe-run] captured ${NAME}.LOG from C:\\ root ($(wc -l <"$LOG_PATH") lines)"
    captured=1
fi

# Post-flight disk check
echo "[probe-run] post-flight /tmp state:"
df -h /tmp | tail -1

# Discipline log (canonical image untouched -- scratch only)
NOW="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
CHANGE_LOG="$REPO_ROOT/docs/internal/G2K-CF-IMAGE-CHANGES.md"
if [[ -f "$CHANGE_LOG" ]]; then
    {
        printf '\n'
        printf '### %s -- 86box-probe-run.sh execution\n\n' "$NOW"
        printf -- '- **Who**: realhw harness (`tools/86box-probe-run.sh`)\n'
        printf -- '- **What**: scratch image %s snapshotted from ~/g2k-cf.img; probe %s.EXE + %s.BAT mcopy-injected into C:\\DOSKUTSU\\; AUTOEXEC.BAT hooked to CALL C:\\NEXT.BAT; probe run under 86Box; %s.LOG captured to %s; scratch discarded post-run.\n' "$SCRATCH_IMG" "$NAME" "$NAME" "$NAME" "$LOG_PATH"
        printf -- '- **Canonical image impact**: NONE -- canonical ~/g2k-cf.img not touched. Scratch-image pattern preserves operator-handoff CF state.\n'
        printf -- '- **Pre-snapshot SHA**: `%s` (canonical)\n' "$ORIG_SHA"
        printf -- '- **Reversible**: N/A; canonical untouched.\n'
    } >> "$CHANGE_LOG"
    echo "[probe-run] G2K-CF-IMAGE-CHANGES.md updated"
fi

if [[ "$captured" == "0" ]]; then
    echo "86box-probe-run.sh: ${NAME}.LOG not produced (looked in C:\\DOSKUTSU\\ and C:\\ root)" >&2
    if [[ "$sentinel_seen" == "1" ]]; then
        echo "  sentinel present but log absent -- probe exited before writing its log" >&2
    else
        echo "  sentinel absent -- AUTOEXEC/NEXT.BAT did not complete (probe hang?)" >&2
    fi
    exit 4
fi

echo "[probe-run] DONE -- $LOG_PATH"
exit 0
