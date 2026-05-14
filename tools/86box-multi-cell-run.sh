#!/usr/bin/env bash
# 86box-multi-cell-run.sh -- multi-cell PLAY-matrix harness for 86Box per
# team-lead directive #11 (tri-env testing standard from wave-45+).
#
# Companion to:
#   - tools/86box-run.sh           (single-binary HWINV smokes; preserved)
#   - tools/dosbox-launch.sh       (visible DOSBox-X for DOSBox-X cells)
#   - realhw's iter wrapper        (real-HW cells; real CF)
#
# Mechanism (per team-lead's spec):
#   1. Snapshot ~/g2k-cf.img to a wave-NN scratch image (canonical never mutated)
#   2. mcopy in: DOSKUTSU.EXE, PLAY*.BAT/_PLAY*.BAT, PLAY.TAS into C:\TAS\,
#      append a hook line `CALL C:\NEXT.BAT` to AUTOEXEC.BAT (only at the
#      :END label; operator's [menu] driver-init path untouched).
#   3. For each cell in --cells: write C:\NEXT.BAT shim that runs that cell,
#      boot 86Box VM, wait for clean shutdown (TAS auto-exits) or timeout,
#      mcopy LOG files out, clean sentinel.
#   4. Cleanup: discard scratch image (or keep with --keep-scratch).
#   5. Append summary entry to docs/internal/G2K-CF-IMAGE-CHANGES.md.
#
# Why scratch image not canonical (~/g2k-cf.img):
#   - Operator's CF state is the source-of-truth; mutating it for each
#     multi-cell run risks state drift across iters.
#   - Snapshot+cleanup pattern fits the wave-NN cycle: one scratch per wave.
#   - Canonical wave-37 binary at C:\DOSKUTSU\DOSKUTSU.EXE preserved.
#
# 86Box binary requirements:
#   - Source-built 86Box at ~/emulators/86box/86Box-doskutsu (or
#     tools/86box if symlinked) with tools/86box-patches/0001-write-
#     through-ide-cache.patch applied. The vanilla AppImage's write-back
#     IDE cache breaks mcopy log-extraction; the patch makes fsync-per-
#     write so guest writes are visible to host mcopy after pkill.
#   - 86Box ROMs at ~/emulators/86box/roms.
#   - tools/86box-x.conf (g2k parity machine config).
#   - Persistent NVR with LBA-mode IDE CMOS at /tmp/86box-PLAY0/nvr/ap5s.nvr
#     (created during task #11; LBA auto-detect saves manual SETUP scripting).
#
# Per-cell flow:
#   T+0s     : 86Box launched; BIOS POST + ROM init
#   T+~5s    : BIOS reads CMOS; CMOS-OK because NVR persisted; no F1 prompt
#   T+~15s   : MS-DOS [menu] auto-times-out (default UNIVBE, 5sec)
#   T+~25s   : UNIVBE driver-init complete (UniVBE TSR + SB16 PnP +
#              CuteMouse + UniSound load with their normal errors)
#   T+~27s   : AUTOEXEC :END appends CALL C:\NEXT.BAT -> our cell shim
#   T+~28s   : Cell BAT runs SET env vars + DOSKUTSU.EXE
#   T+~30s   : DOSKUTSU loads + TAS replay opens + replay begins
#   T+~30s..~150s : TAS replay runs (depending on AUTO_EXIT_TICK or EOF)
#   T+~150s  : Engine clean-exit (or shutdown-hang for pre-wave-33 backports)
#   T+~155s  : C:\<CELL>.DONE sentinel written by shim
#   T+~155s  : Harness polls + finds sentinel, SIGTERMs 86Box
#   T+~158s  : Harness mcopies LOGs out
#
# Total budget per cell: 300s default (--timeout-per-cell SECS). Slack
# above the ~155s expected timeline handles slow boot + long replays.
#
# Cleanup discipline (per realhw #11 + [[never_commit_internal_plans]]):
#   - df -h /tmp BEFORE start + AFTER end logged to stdout
#   - Scratch image deleted on exit unless --keep-scratch
#   - G2K-CF-IMAGE-CHANGES.md gets one summary entry per run

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Bootstrap-overridable paths
BOX86_BINARY="${BOX86_BINARY:-$HOME/emulators/86box/86Box-doskutsu}"
BOX86_ROMPATH="${BOX86_ROMPATH:-$HOME/emulators/86box/roms}"
BOX86_IMG="${BOX86_IMG:-$HOME/g2k-cf.img}"
BOX86_NVR_SRC="${BOX86_NVR_SRC:-/tmp/86box-PLAY0/nvr/ap5s.nvr}"
BOX86_DISPLAY="${BOX86_DISPLAY:-:0}"
BOX86_CONF="$SCRIPT_DIR/86box-x.conf"

IMG_FAT_OFFSET=32256

# CLI
TARBALL=""
BAT_DIR=""
TAS_PATH=""
CELLS=""
OUT_DIR=""
TIMEOUT_PER_CELL=300
KEEP_SCRATCH=0

usage() {
    cat <<'USAGE'
Usage: 86box-multi-cell-run.sh [options]
  --tarball PATH       tarball with DOSKUTSU/ subdir (mutually exclusive with --bat-dir)
  --bat-dir PATH       pre-extracted DOSKUTSU dir (mutually exclusive with --tarball)
  --tas PATH           PLAY.TAS file (operator-recorded canonical scene)
  --cells LIST         comma-separated PLAY tags (e.g. PLAY0,PLAY1,PLAY2,PLAY3,PLAY4)
  --out DIR            output dir for LOG files (created if absent)
  --timeout-per-cell N seconds per cell (default 300)
  --keep-scratch       preserve scratch image post-run (debug)
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tarball)           TARBALL="$2"; shift 2 ;;
        --bat-dir)           BAT_DIR="$2"; shift 2 ;;
        --tas)               TAS_PATH="$2"; shift 2 ;;
        --cells)             CELLS="$2"; shift 2 ;;
        --out)               OUT_DIR="$2"; shift 2 ;;
        --timeout-per-cell)  TIMEOUT_PER_CELL="$2"; shift 2 ;;
        --keep-scratch)      KEEP_SCRATCH=1; shift ;;
        -h|--help)           usage; exit 0 ;;
        *) echo "86box-multi-cell-run.sh: unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

# Preflight
fail=0
if [[ -z "$TAS_PATH" || ! -f "$TAS_PATH" ]]; then
    echo "86box-multi-cell-run.sh: --tas missing or file not found: $TAS_PATH" >&2; fail=1
fi
if [[ -z "$CELLS" ]]; then
    echo "86box-multi-cell-run.sh: --cells required" >&2; fail=1
fi
if [[ -z "$OUT_DIR" ]]; then
    echo "86box-multi-cell-run.sh: --out required" >&2; fail=1
fi
if [[ -z "$TARBALL" && -z "$BAT_DIR" ]]; then
    echo "86box-multi-cell-run.sh: --tarball or --bat-dir required" >&2; fail=1
fi
if [[ -n "$TARBALL" && -n "$BAT_DIR" ]]; then
    echo "86box-multi-cell-run.sh: --tarball and --bat-dir are mutually exclusive" >&2; fail=1
fi
if [[ ! -x "$BOX86_BINARY" ]]; then
    echo "86box-multi-cell-run.sh: BOX86_BINARY not executable: $BOX86_BINARY" >&2; fail=1
fi
if [[ ! -d "$BOX86_ROMPATH" ]]; then
    echo "86box-multi-cell-run.sh: BOX86_ROMPATH missing: $BOX86_ROMPATH" >&2; fail=1
fi
if [[ ! -f "$BOX86_IMG" ]]; then
    echo "86box-multi-cell-run.sh: BOX86_IMG missing: $BOX86_IMG" >&2; fail=1
fi
if [[ ! -f "$BOX86_NVR_SRC" ]]; then
    echo "86box-multi-cell-run.sh: BOX86_NVR_SRC missing: $BOX86_NVR_SRC" >&2
    echo "  (run tools/86box-run.sh once to populate; or override via env)" >&2
    fail=1
fi
if ! command -v mcopy >/dev/null 2>&1 || ! command -v mdir >/dev/null 2>&1; then
    echo "86box-multi-cell-run.sh: mtools missing (apt install mtools)" >&2; fail=1
fi
if [[ "$fail" == "1" ]]; then exit 2; fi

mkdir -p "$OUT_DIR"

# Extract BAT_DIR from tarball if needed
if [[ -n "$TARBALL" ]]; then
    EXTRACT_TMP="$(mktemp -d -t 86box-extract.XXXXXX)"
    trap "rm -rf $EXTRACT_TMP" EXIT
    tar -xzf "$TARBALL" -C "$EXTRACT_TMP" 2>&1 | head -3
    # Find DOSKUTSU/ subdir
    BAT_DIR="$(find "$EXTRACT_TMP" -maxdepth 3 -type d -name DOSKUTSU 2>/dev/null | head -1)"
    if [[ -z "$BAT_DIR" || ! -d "$BAT_DIR" ]]; then
        echo "86box-multi-cell-run.sh: tarball does not contain a DOSKUTSU/ subdir" >&2
        exit 2
    fi
fi

# Verify BAT_DIR has the expected contents
if [[ ! -f "$BAT_DIR/DOSKUTSU.EXE" ]]; then
    echo "86box-multi-cell-run.sh: BAT_DIR missing DOSKUTSU.EXE: $BAT_DIR" >&2; exit 2
fi

# Parse cells
IFS=',' read -ra CELL_ARR <<< "$CELLS"

# Pre-flight disk check
echo "[multi-cell] pre-flight /tmp state:"
df -h /tmp | tail -1

# Snapshot scratch image
SCRATCH_IMG="/tmp/86box-multi-scratch-$$.img"
echo "[multi-cell] snapshotting $BOX86_IMG -> $SCRATCH_IMG"
cp "$BOX86_IMG" "$SCRATCH_IMG"
ORIG_SHA="$(sha256sum "$SCRATCH_IMG" | awk '{print $1}' | head -c 12)"
echo "[multi-cell] scratch image sha12=$ORIG_SHA"

# VMpath dir
VMPATH="$(mktemp -d -t 86box-vmpath.XXXXXX)"
cp "$BOX86_CONF" "$VMPATH/86box.cfg"
ln -s "$SCRATCH_IMG" "$VMPATH/g2k-cf.img"
mkdir -p "$VMPATH/nvr"
cp "$BOX86_NVR_SRC" "$VMPATH/nvr/ap5s.nvr"

cleanup() {
    pkill -9 -f "86Box.*$VMPATH" 2>/dev/null || true
    sleep 1
    if [[ "$KEEP_SCRATCH" == "1" ]]; then
        echo "[multi-cell] scratch preserved at $SCRATCH_IMG (--keep-scratch)" >&2
        echo "[multi-cell] vmpath preserved at $VMPATH (--keep-scratch)" >&2
    else
        rm -f "$SCRATCH_IMG"
        rm -rf "$VMPATH"
    fi
}
trap cleanup EXIT

# --- mcopy bundle into scratch image ---
echo "[multi-cell] copying bundle into scratch image"

# Push DOSKUTSU.EXE
mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$BAT_DIR/DOSKUTSU.EXE" "::/DOSKUTSU/DOSKUTSU.EXE"

# Push all PLAY*.BAT and _PLAY*.BAT (whichever exist in the bundle)
for batfile in "$BAT_DIR"/PLAY*.BAT "$BAT_DIR"/_PLAY*.BAT "$BAT_DIR"/RUN.BAT; do
    if [[ -f "$batfile" ]]; then
        bname="$(basename "$batfile")"
        mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$batfile" "::/DOSKUTSU/$bname" 2>/dev/null || true
    fi
done

# Push PLAY.TAS into C:\TAS\
mmd -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/TAS" 2>/dev/null || true
mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$TAS_PATH" "::/TAS/PLAY.TAS"

# Pull AUTOEXEC.BAT, add hook line `CALL C:\NEXT.BAT` before :END
mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/AUTOEXEC.BAT" "$VMPATH/AUTOEXEC.ORIG.BAT" 2>/dev/null || true
if [[ ! -f "$VMPATH/AUTOEXEC.ORIG.BAT" ]]; then
    echo "[multi-cell] WARNING: image's AUTOEXEC.BAT not found; falling back to bare AUTOEXEC" >&2
    {
        printf '@ECHO OFF\r\n'
        printf 'SET BLASTER=A220 I5 D1 H5 T6 J200 P330\r\n'
        printf 'CALL C:\\NEXT.BAT\r\n'
    } > "$VMPATH/AUTOEXEC.NEW.BAT"
else
    # Insert "CALL C:\NEXT.BAT" line right AFTER the :END label (not before -- if
    # we put it before, it's unreachable because the GOTO END statements skip
    # past it directly to the :END label). Putting it AFTER the :END label
    # means every code path that does GOTO END (= every config branch) flows
    # through CALL NEXT.BAT after the label.
    awk '
      BEGIN { hooked=0 }
      { print }
      /^:END/ && !hooked {
          print "CALL C:\\NEXT.BAT"
          hooked=1
      }
      END {
          if (!hooked) {
              print ":END"
              print "CALL C:\\NEXT.BAT"
          }
      }
    ' "$VMPATH/AUTOEXEC.ORIG.BAT" > "$VMPATH/AUTOEXEC.NEW.BAT"
fi
# Ensure CRLF
tr -d '\r' < "$VMPATH/AUTOEXEC.NEW.BAT" | sed 's/$/\r/' > "$VMPATH/AUTOEXEC.NEW.BAT.crlf"
mv "$VMPATH/AUTOEXEC.NEW.BAT.crlf" "$VMPATH/AUTOEXEC.NEW.BAT"
mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$VMPATH/AUTOEXEC.NEW.BAT" "::/AUTOEXEC.BAT"

echo "[multi-cell] bundle staged into scratch image"

# --- Per-cell loop ---
for cell in "${CELL_ARR[@]}"; do
    echo ""
    echo "=== [multi-cell] CELL: $cell ==="
    # Author per-cell C:\NEXT.BAT shim
    {
        printf '@ECHO OFF\r\n'
        printf 'REM auto-generated by tools/86box-multi-cell-run.sh\r\n'
        printf 'CD C:\\DOSKUTSU\r\n'
        printf 'CALL %s.BAT\r\n' "$cell"
        printf 'ECHO CELL_DONE > C:\\%s.DONE\r\n' "$cell"
    } > "$VMPATH/NEXT.BAT"
    mcopy -o -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "$VMPATH/NEXT.BAT" "::/NEXT.BAT"

    # Clear any stale sentinel from a prior cell
    mdel -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/${cell}.DONE" 2>/dev/null || true
    # Clear stale logs (so we measure THIS cell's output, not last)
    mdel -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/${cell}.LOG" 2>/dev/null || true
    mdel -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/${cell}SDL.LOG" 2>/dev/null || true

    # Launch 86Box headless (DISPLAY=:0; X-side WM handles Qt-focus)
    echo "[multi-cell] launching 86Box for $cell (timeout ${TIMEOUT_PER_CELL}s)"
    DISPLAY="$BOX86_DISPLAY" "$BOX86_BINARY" \
        -P "$VMPATH" \
        -R "$BOX86_ROMPATH" \
        -V "doskutsu-g2k-parity" \
        -N \
        -L "$VMPATH/${cell}-86box.log" \
        </dev/null >"$VMPATH/${cell}-stdout.log" 2>"$VMPATH/${cell}-stderr.log" &
    BOX_PID=$!

    # Poll for sentinel
    poll_start=$(date +%s)
    sentinel_seen=0
    while true; do
        now=$(date +%s)
        elapsed=$((now - poll_start))
        if [[ "$elapsed" -ge "$TIMEOUT_PER_CELL" ]]; then
            echo "[multi-cell] $cell timeout reached without sentinel"
            break
        fi
        if ! kill -0 "$BOX_PID" 2>/dev/null; then
            echo "[multi-cell] $cell 86Box exited (pid $BOX_PID); finalizing"
            break
        fi
        # Poll sentinel via mcopy (fsync patch makes guest writes visible)
        if mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/${cell}.DONE" - 2>/dev/null | grep -q CELL_DONE; then
            echo "[multi-cell] $cell sentinel seen at +${elapsed}s"
            sentinel_seen=1
            # Wait briefly for engine to flush LOG files
            sleep 3
            break
        fi
        sleep 3
    done

    # Kill 86Box for this cell
    if kill -0 "$BOX_PID" 2>/dev/null; then
        echo "[multi-cell] $cell SIGTERM 86Box (pid $BOX_PID)"
        kill -TERM "$BOX_PID" 2>/dev/null || true
        sleep 3
        if kill -0 "$BOX_PID" 2>/dev/null; then
            kill -KILL "$BOX_PID" 2>/dev/null || true
        fi
        wait "$BOX_PID" 2>/dev/null || true
    fi

    # mcopy logs out
    if mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/${cell}.LOG" "$OUT_DIR/${cell}.LOG" 2>/dev/null; then
        log_bytes=$(stat -c%s "$OUT_DIR/${cell}.LOG")
        echo "[multi-cell] $cell captured ${cell}.LOG ($log_bytes bytes)"
    else
        echo "[multi-cell] $cell ${cell}.LOG NOT FOUND in scratch image" >&2
    fi
    if mcopy -i "$SCRATCH_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/${cell}SDL.LOG" "$OUT_DIR/${cell}SDL.LOG" 2>/dev/null; then
        sdl_bytes=$(stat -c%s "$OUT_DIR/${cell}SDL.LOG")
        echo "[multi-cell] $cell captured ${cell}SDL.LOG ($sdl_bytes bytes)"
    else
        echo "[multi-cell] $cell ${cell}SDL.LOG NOT FOUND in scratch image" >&2
    fi
done

# Post-flight disk check
echo ""
echo "[multi-cell] post-flight /tmp state:"
df -h /tmp | tail -1

# Discipline log
NOW="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
CHANGE_LOG="$REPO_ROOT/docs/internal/G2K-CF-IMAGE-CHANGES.md"
if [[ -f "$CHANGE_LOG" ]]; then
    {
        printf '\n'
        printf '### %s -- 86box-multi-cell-run.sh execution\n\n' "$NOW"
        printf -- '- **Who**: build-qa harness (`tools/86box-multi-cell-run.sh`)\n'
        printf -- '- **What**: scratch image %s snapshotted from ~/g2k-cf.img; bundle %s + TAS %s mcopy-injected; cells %s executed sequentially under 86Box; logs captured to %s; scratch discarded post-run.\n' "$SCRATCH_IMG" "${TARBALL:-$BAT_DIR}" "$TAS_PATH" "$CELLS" "$OUT_DIR"
        printf -- '- **Canonical image impact**: NONE -- canonical ~/g2k-cf.img not touched. Scratch-image pattern preserves operator-handoff CF state.\n'
        printf -- '- **Pre-snapshot SHA**: `%s` (canonical)\n' "$ORIG_SHA"
        printf -- '- **Reversible**: N/A; canonical untouched.\n'
    } >> "$CHANGE_LOG"
    echo "[multi-cell] G2K-CF-IMAGE-CHANGES.md updated"
fi

echo ""
echo "[multi-cell] DONE"
exit 0
