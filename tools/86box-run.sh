#!/usr/bin/env bash
# 86box-run.sh -- run HWINV.EXE under 86Box against the operator's actual
# g2k CF image and capture HWINV.LOG.
#
# Companion to tools/dosbox-run.sh, but for 86Box (the closer-to-real-HW
# emulator per docs/internal/WAVE-41-DOSBOX-PROFILING-PLAN.md sec. 5.1).
# Used by `make hwinv-86box-smoke` to validate HWINV.EXE against a
# second emulator -- if hwinv passes DOSBox-X but fails 86Box (or vice
# versa), that disagreement is signal worth documenting.
#
# Architectural shape:
#
# - The hard disk is the operator's actual g2k CF image at ~/g2k-cf.img
#   (MS-DOS 6.22 + CWSDPMI + UniVBE 6.70; byte-for-byte the g2k boot
#   state). All modifications to that image MUST be logged per
#   docs/internal/G2K-CF-IMAGE-CHANGES.md.
#
# - The image has been pre-modified by build-qa task #11 to add an
#   HWINV menu choice + :HWINV AUTOEXEC label that runs C:\DOSKUTSU\
#   HWINV.EXE on boot. Operator's daily-driver default boot path
#   (UNIVBE menu choice) is unchanged.
#
# - 86Box renders to the host X display (DISPLAY=:0 by default). Xvfb
#   is NOT used -- prior attempts under Xvfb hit a Qt/focus-event
#   blocker (see archive in earlier revisions of this file's header).
#   With a real X display available, 86Box's Qt6 GUI initializes
#   correctly and the emulation thread auto-unpauses.
#
# - At boot, the harness uses xdotool to send the keypress sequence
#   `H` `Return` to the 86Box window during the 5-second menu prompt.
#   That selects the HWINV menu choice -> CONFIG.SYS [HWINV] section
#   (HIMEM only; no driver loads) -> AUTOEXEC.BAT :HWINV label
#   (CD C:\DOSKUTSU + HWINV.EXE + write HWINV.DONE sentinel).
#
# - After waiting for the C:\HWINV.DONE sentinel to appear in the
#   image (polled via mcopy), or after a fixed timeout, the harness
#   pkills 86Box and mcopies HWINV.LOG out of C:\DOSKUTSU\.
#
# Typical use (once bootstrap is complete):
#   tools/86box-run.sh --exe build/probes/hwinv.exe --log /tmp/hwinv-86box.log
#
# The --exe parameter is informational: the actual binary that runs
# inside 86Box is whatever is at C:\DOSKUTSU\HWINV.EXE in the image
# (refreshed from the host --exe path on each invocation via mcopy --
# so the smoke always tests the host's latest build).
#
# Exit codes:
#   0  -- ran successfully, HWINV.LOG captured
#   2  -- bootstrap prerequisite missing (AppImage / ROMs / image / mtools)
#   3  -- 86Box failed to start (X display / Qt / VM cfg error)
#   4  -- HWINV.LOG not produced (probe may have crashed; or DONE.TXT
#         marker check shows VM never reached AUTOEXEC.BAT)
#   5  -- invocation error (bad CLI args)

set -uo pipefail
# Deliberately NOT `set -e`: we want diagnostic messages for each
# failure mode, not abort on first non-zero.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Bootstrap-overridable paths. Search order for the 86Box binary:
#   1. BOX86_APPIMAGE env override (operator's explicit choice)
#   2. Locally-built binary at ~/emulators/86box/86Box-doskutsu
#      (matches tools/86box-patches/0001-write-through-ide-cache.patch
#      applied to v5.3 source; required for headless smoke per the
#      patches README. Vanilla AppImage's write-back IDE cache breaks
#      log-capture-via-mcopy.)
#   3. Pre-extracted AppRun at ~/emulators/86box/squashfs-root/AppRun
#      (vanilla AppImage; works for non-log-capture use but smoke gate
#      will fail to extract HWINV.LOG)
#   4. ~/emulators/86box/86Box.AppImage (vanilla self-extracting; same
#      vanilla caveat as #3)
BOX86_APPIMAGE="${BOX86_APPIMAGE:-}"
if [[ -z "$BOX86_APPIMAGE" ]]; then
    if [[ -x "$HOME/emulators/86box/86Box-doskutsu" ]]; then
        BOX86_APPIMAGE="$HOME/emulators/86box/86Box-doskutsu"
    elif [[ -x "$HOME/emulators/86box/squashfs-root/AppRun" ]]; then
        BOX86_APPIMAGE="$HOME/emulators/86box/squashfs-root/AppRun"
    else
        BOX86_APPIMAGE="$HOME/emulators/86box/86Box.AppImage"
    fi
fi
BOX86_ROMPATH="${BOX86_ROMPATH:-$HOME/emulators/86box/roms}"
BOX86_IMG="${BOX86_IMG:-$HOME/g2k-cf.img}"
BOX86_DISPLAY="${BOX86_DISPLAY:-:0}"
CONF="$SCRIPT_DIR/86box-x.conf"

# Image-side FAT partition offset (sector 63 * 512 bytes; standard MS-DOS
# MBR layout matching the operator's CF image).
IMG_FAT_OFFSET=32256

# CLI surface.
EXE=""
LOG_PATH=""
KEEP_VMRUN=0
TIMEOUT_S=120

usage() {
    cat <<'USAGE'
Usage: 86box-run.sh --exe PATH --log PATH [--timeout SEC] [--keep-vmrun]

  --exe PATH        Host-side DOS executable to push into C:\DOSKUTSU\
                    in the image before launch. The smoke always tests
                    the host's latest build. Required.
  --log PATH        Where to copy captured HWINV.LOG on success. Required.
  --timeout SEC     Max wall-clock for the whole smoke (default: 120).
                    Includes BIOS POST, menu wait, AUTOEXEC, HWINV exec.
  --keep-vmrun      Don't kill 86Box at smoke end; let it run for
                    interactive debugging. Inhibits log extraction.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)         EXE="$2"; shift 2 ;;
        --log)         LOG_PATH="$2"; shift 2 ;;
        --timeout)     TIMEOUT_S="$2"; shift 2 ;;
        --keep-vmrun)  KEEP_VMRUN=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "86box-run.sh: unknown arg: $1" >&2; usage; exit 5 ;;
    esac
done

if [[ -z "$EXE" || -z "$LOG_PATH" ]]; then
    echo "86box-run.sh: --exe and --log are required" >&2
    usage
    exit 5
fi
if [[ ! -f "$EXE" ]]; then
    echo "86box-run.sh: exe not found: $EXE" >&2
    exit 5
fi

# Preflight prerequisite checks. Each names the specific bootstrap step.
fail=0
if [[ ! -x "$BOX86_APPIMAGE" ]]; then
    echo "86box-run.sh: BOX86_APPIMAGE missing or not executable: $BOX86_APPIMAGE" >&2
    echo "              see tools/86box-x.conf header bootstrap step 1" >&2
    fail=1
fi
if [[ ! -d "$BOX86_ROMPATH" ]]; then
    echo "86box-run.sh: BOX86_ROMPATH missing or not a directory: $BOX86_ROMPATH" >&2
    echo "              see tools/86box-x.conf header bootstrap step 2" >&2
    fail=1
fi
if [[ ! -f "$BOX86_IMG" ]]; then
    echo "86box-run.sh: BOX86_IMG missing: $BOX86_IMG" >&2
    echo "              operator-provided; see docs/internal/G2K-CF-IMAGE-CHANGES.md" >&2
    fail=1
fi
if ! command -v mcopy >/dev/null 2>&1; then
    echo "86box-run.sh: mtools 'mcopy' missing (apt install mtools)" >&2
    fail=1
fi
if ! command -v xdotool >/dev/null 2>&1; then
    echo "86box-run.sh: 'xdotool' missing (apt install xdotool)" >&2
    fail=1
fi
if [[ ! -f "$CONF" ]]; then
    echo "86box-run.sh: conf missing: $CONF" >&2
    fail=1
fi
if ! DISPLAY="$BOX86_DISPLAY" xdpyinfo >/dev/null 2>&1; then
    echo "86box-run.sh: no usable X display at \$BOX86_DISPLAY=$BOX86_DISPLAY" >&2
    echo "              expected a real X server (Xvfb is NOT supported; see header)" >&2
    fail=1
fi
if [[ "$fail" == "1" ]]; then
    exit 2
fi

# Acquire image write lock (cooperative; see G2K-CF-IMAGE-CHANGES.md
# tracking discipline #4).
IMG_LOCK="/tmp/g2k-cf.img.lock"
if [[ -e "$IMG_LOCK" ]]; then
    # Cooperative: wait up to 60s for another agent to release the lock.
    for _ in $(seq 1 60); do
        [[ -e "$IMG_LOCK" ]] || break
        sleep 1
    done
    if [[ -e "$IMG_LOCK" ]]; then
        echo "86box-run.sh: $IMG_LOCK held by another agent after 60s wait" >&2
        echo "              if stale (age > 1 hr) remove with: rm $IMG_LOCK" >&2
        exit 2
    fi
fi
touch "$IMG_LOCK"

# Stage: assemble a VM-run directory. 86Box's --vmpath points here.
# We DO NOT copy the canonical image; we symlink it so 86Box reads/writes
# directly against ~/g2k-cf.img. Per the user's "modify freely" posture
# + change-tracking discipline, the image IS the canonical state.
VMRUN_DIR="$(mktemp -d -t 86box-vmrun.XXXXXX)"
cleanup() {
    rm -f "$IMG_LOCK"
    if [[ "$KEEP_VMRUN" == "1" ]]; then
        echo "86box-run.sh: VM run dir kept at $VMRUN_DIR" >&2
    else
        rm -rf "$VMRUN_DIR"
    fi
}
trap cleanup EXIT

# 86Box looks for the config at <vmpath>/86box.cfg by convention. Copy
# (not symlink -- 86Box may rewrite on first run) the parity conf there.
cp "$CONF" "$VMRUN_DIR/86box.cfg"

# Symlink the canonical image into the vmpath dir so 86Box reads/writes
# the real ~/g2k-cf.img file (NOT a copy).
ln -s "$BOX86_IMG" "$VMRUN_DIR/g2k-cf.img"

# Refresh HWINV.EXE in C:\DOSKUTSU\ from the host's latest build. The
# AUTOEXEC.BAT :HWINV label (added by build-qa task #11; see
# G2K-CF-IMAGE-CHANGES.md) runs whatever HWINV.EXE is at that path.
EXE_BASENAME="$(basename "$EXE")"
echo "[86box-run] refreshing C:\\DOSKUTSU\\HWINV.EXE from host $EXE"
if ! mcopy -o -i "$BOX86_IMG@@$IMG_FAT_OFFSET" "$EXE" "::/DOSKUTSU/HWINV.EXE" 2>"$VMRUN_DIR/mcopy.err"; then
    echo "86box-run.sh: mcopy failed to push HWINV.EXE into image" >&2
    cat "$VMRUN_DIR/mcopy.err" >&2
    exit 2
fi

# Clear any stale HWINV.LOG + HWINV.DONE from a prior run so the
# sentinel/log check below is hermetic to this run only.
mdel -i "$BOX86_IMG@@$IMG_FAT_OFFSET" "::/DOSKUTSU/HWINV.LOG" 2>/dev/null || true
mdel -i "$BOX86_IMG@@$IMG_FAT_OFFSET" "::/HWINV.DONE" 2>/dev/null || true

# Launch 86Box. Render to the host X display (no Xvfb).
echo "[86box-run] launching 86Box (vmpath=$VMRUN_DIR, display=$BOX86_DISPLAY, timeout=${TIMEOUT_S}s)"
DISPLAY="$BOX86_DISPLAY" "$BOX86_APPIMAGE" \
    -P "$VMRUN_DIR" \
    -R "$BOX86_ROMPATH" \
    -V "doskutsu-g2k-parity" \
    -N \
    -L "$VMRUN_DIR/86box-internal.log" \
    >"$VMRUN_DIR/stdout.log" 2>"$VMRUN_DIR/stderr.log" &
BOX_PID=$!

# Wait for 86Box's window to appear in X. Then send the menu-selection
# keypresses to pick the HWINV menu choice during the 5-sec timeout.
# xdotool's `search --name "86Box"` finds the QMainWindow; `key` sends
# X KeySym events to it.
window_id=""
for _ in $(seq 1 30); do
    window_id="$(DISPLAY="$BOX86_DISPLAY" xdotool search --name '86Box' 2>/dev/null | head -1 || true)"
    [[ -n "$window_id" ]] && break
    sleep 0.5
done
if [[ -z "$window_id" ]]; then
    echo "86box-run.sh: 86Box window did not appear within 15s" >&2
    kill -TERM "$BOX_PID" 2>/dev/null
    wait "$BOX_PID" 2>/dev/null
    exit 3
fi
echo "[86box-run] 86Box window detected (id=$window_id)"

# Give BIOS + MBR + CONFIG.SYS [menu] (5-sec UNIVBE default) + AUTOEXEC.BAT
# driver init (PicoGUS errors expected; SB16 PnP init; UniVBE TSR;
# CuteMouse) to reach the DOS C:\> prompt. Total ~20s on 86Box at 90 MHz
# Pentium emulation; budget more if probe section is dense.
sleep 22
echo "[86box-run] setting DOSKUTSU_ENVIRONMENT=86box at master prompt"
# DOS BAT-internal SET silently fails when env block is near-full
# (MS-DOS 6.22 default ~256 bytes; operator's AUTOEXEC.BAT already
# loads BLASTER + MIDI + ULTRASND + TEMP + TMP + ULTRADIR + PROMPT +
# PATH ~ 200 bytes). Master prompt's SET auto-extends the env so the
# override propagates to child processes (HWINV.EXE). Confirmed via
# screen-capture validation 2026-05-13: BAT-internal SET ->
# ENV_VAR_OVERRIDE_SIGNAL=0; master SET -> SIGNAL=1.
DISPLAY="$BOX86_DISPLAY" xdotool type --window "$window_id" --delay 50 "SET DOSKUTSU_ENVIRONMENT=86box"
DISPLAY="$BOX86_DISPLAY" xdotool key --window "$window_id" Return
sleep 0.8
echo "[86box-run] invoking HWBOX from C:\\DOSKUTSU"
DISPLAY="$BOX86_DISPLAY" xdotool type --window "$window_id" --delay 50 "CD C:\\DOSKUTSU"
DISPLAY="$BOX86_DISPLAY" xdotool key --window "$window_id" Return
sleep 0.8
DISPLAY="$BOX86_DISPLAY" xdotool type --window "$window_id" --delay 50 "HWBOX"
DISPLAY="$BOX86_DISPLAY" xdotool key --window "$window_id" Return

# Poll for the HWINV.DONE sentinel in the image. mcopy reads work
# concurrently with 86Box (FAT16 on a single-writer DOS guest is safe
# for host-side reads of files the guest has closed; HWINV.EXE closes
# its log via fclose before AUTOEXEC.BAT writes HWINV.DONE).
poll_start=$(date +%s)
echo "[86box-run] polling for HWINV.DONE sentinel (timeout ${TIMEOUT_S}s)..."
done_seen=0
while true; do
    now=$(date +%s)
    elapsed=$((now - poll_start))
    if [[ "$elapsed" -ge "$TIMEOUT_S" ]]; then
        echo "[86box-run] timeout reached without HWINV.DONE; assuming completion or hang"
        break
    fi
    if ! kill -0 "$BOX_PID" 2>/dev/null; then
        echo "[86box-run] 86Box process exited (PID $BOX_PID); finalizing"
        break
    fi
    if mcopy -i "$BOX86_IMG@@$IMG_FAT_OFFSET" "::/HWINV.DONE" - 2>/dev/null | grep -q HWINV_DONE; then
        echo "[86box-run] HWINV.DONE sentinel detected after ${elapsed}s"
        done_seen=1
        # Give HWINV one more second to flush its log before we read it
        sleep 1
        break
    fi
    sleep 2
done

# Kill 86Box cleanly if it's still running. Use SIGTERM first; Qt's
# event loop should drain. SIGKILL as escalation.
if [[ "$KEEP_VMRUN" == "0" ]] && kill -0 "$BOX_PID" 2>/dev/null; then
    echo "[86box-run] sending SIGTERM to 86Box (PID $BOX_PID)"
    kill -TERM "$BOX_PID" 2>/dev/null
    sleep 3
    if kill -0 "$BOX_PID" 2>/dev/null; then
        echo "[86box-run] SIGTERM ignored; escalating to SIGKILL"
        kill -KILL "$BOX_PID" 2>/dev/null
    fi
    wait "$BOX_PID" 2>/dev/null
fi

# Extract HWINV.LOG from the image.
LOG_DIR="$(dirname "$LOG_PATH")"
mkdir -p "$LOG_DIR"
if ! mcopy -i "$BOX86_IMG@@$IMG_FAT_OFFSET" -o "::/DOSKUTSU/HWINV.LOG" "$LOG_PATH" 2>"$VMRUN_DIR/mcopy-out.err"; then
    echo "86box-run.sh: no HWINV.LOG produced in C:\\DOSKUTSU\\" >&2
    if [[ "$done_seen" == "1" ]]; then
        echo "  HWINV.DONE present but HWINV.LOG absent -- HWINV.EXE may have" >&2
        echo "  exited before writing its log. Investigate $VMRUN_DIR/." >&2
    else
        echo "  HWINV.DONE absent -- AUTOEXEC.BAT :HWINV did not complete." >&2
        echo "  Likely causes: menu selection didn't reach AUTOEXEC; the" >&2
        echo "  HWINV menu choice + :HWINV label missing from the image" >&2
        echo "  (verify per docs/internal/G2K-CF-IMAGE-CHANGES.md)." >&2
    fi
    exit 4
fi

echo "[86box-run] captured: $LOG_PATH ($(wc -l <"$LOG_PATH") lines)"
exit 0
