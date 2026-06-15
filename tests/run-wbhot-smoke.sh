#!/usr/bin/env bash
# run-wbhot-smoke.sh -- DOSBox-X correctness-only smoke for WBHOT.EXE
# (T41 / task #16, probe-engineer lane). The WB-on-486 HOT MPU access
# discriminator probe.
#
# Stages WBHOT.EXE + CWSDPMI.EXE and runs all cells (P / M / D / 9) each in
# its own DOSBox-X session (parity conf), then verifies each LOGS\<TAG>PROBE.LOG:
#   - the wbhot header (version + sha + cell)
#   - "init: SDL_Init(AUDIO) ok" / "open: device live, stream bound"
#   - "prime: OK" (ring primed >0 -- the device is actually HOT)
#   - the full MARK sequence for the cell, IN ORDER (the verdict encoding --
#     a missing or out-of-order MARK means the probe's emit structure is
#     broken and a real-HW log could not be parsed)
#   - "done: closed cleanly, exit 0"
#
# REAL-HW-ONLY MECHANISM (per [[dosbox_not_proxy]]): DOSBox-X does NOT
# reproduce the ISA IOCHRDY stall -- BOTH cells run clean here, including the
# cell-M positive controls (hot 0x331 read/write) that are EXPECTED to wedge
# the g2k DX2-66. A clean DOSBox cell M proves ONLY structural correctness.
# The verdict is g2k-DX2-66-only. Do NOT read a clean M cell as "no repro".
#
# BLASTER is set in the driver BAT; the parity conf configures
# sb16/220/IRQ5/DMA1/HDMA5 + MPU-401 at 330 to match g2k.
#
# Usage:
#   tests/run-wbhot-smoke.sh                # run both cells
#   tests/run-wbhot-smoke.sh --keep-stage   # preserve temp dirs for debug
#
# Exit codes:
#   0  smoke passed (both cells structurally correct)
#   1  one or more expected markers missing / out of order
#   2  invocation error (probe not built, dosbox-x absent)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/probes/wbhot.exe"
CWSDPMI="$REPO_ROOT/vendor/cwsdpmi/cwsdpmi.exe"
CONF="$REPO_ROOT/tools/dosbox-x.conf"
LOG_DIR="$REPO_ROOT/build/wbhot-smoke"
BLASTER="A220 I5 D1 H5 P330 T6"   # g2k VIBRA profile (matches parity conf)

KEEP_STAGE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --keep-stage) KEEP_STAGE=1; shift ;;
        -h|--help) sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //; s/^#$//'; exit 0 ;;
        *) echo "$(basename "$0"): unknown arg: $1" >&2; exit 2 ;;
    esac
done

for f in "$EXE" "$CWSDPMI" "$CONF"; do
    if [[ ! -f "$f" ]]; then
        echo "$(basename "$0"): required file missing: $f" >&2
        [[ "$f" == "$EXE" ]] && echo "  hint: build with \`make probes-wbhot\`" >&2
        exit 2
    fi
done
command -v dosbox-x >/dev/null 2>&1 || { echo "$(basename "$0"): dosbox-x not in PATH" >&2; exit 2; }

mkdir -p "$LOG_DIR"
rc=0

# Expected MARK step sequences (post-markers; pre is implied by post). Anchored
# on the unique "^MARK post " line format per [[grep_anchor_confound...]] --
# banner/help text cannot contain it.
MARKS_M=(
    "m1 cold_uart_entry"
    "m2 cold_note_on b1 0x90"
    "m2 cold_note_on b2 0x30"
    "m2 cold_note_on b3 0x7F"
    "m2 cold_note_off b1 0x80"
    "m2 cold_note_off b2 0x30"
    "m2 cold_note_off b3 0x00"
    "m3 sb16_hot_bringup"
    "m4 hot_data b1 0x90"
    "m4 hot_data b2 0x3C"
    "m4 hot_data b3 0x7F"
    "m6 hot_data_off b1 0x80"
    "m6 hot_data_off b2 0x3C"
    "m6 hot_data_off b3 0x00"
    "m7 ctrl_status_read"
    "m8 ctrl_cmd_write"
)
MARKS_D=(
    "d1 sb16_hot_bringup"
    "d2 dsp_uart_entry"
    "d3 dsp_note_on b1 0x90"
    "d3 dsp_note_on b2 0x43"
    "d3 dsp_note_on b3 0x7F"
    "d5 dsp_note_off b1 0x80"
    "d5 dsp_note_off b2 0x43"
    "d5 dsp_note_off b3 0x00"
)
MARKS_P=(
    "p1 cold_uart_entry_polled"
    "p2 cold_note_on b1 0x90"
    "p2 cold_note_on b2 0x40"
    "p2 cold_note_on b3 0x7F"
    "p2 cold_note_off b1 0x80"
    "p2 cold_note_off b2 0x40"
    "p2 cold_note_off b3 0x00"
    "p3 sb16_hot_bringup_mix"
    "p4 hot_data b1 0x90"
    "p4 hot_data b2 0x40"
    "p4 hot_data b3 0x7F"
    "p6 hot_data_off b1 0x80"
    "p6 hot_data_off b2 0x40"
    "p6 hot_data_off b3 0x00"
    "p7 ctrl_status_read"
    "p8 ctrl_cmd_write_reset"
)
# Cell 9 (v3): fixed-beat stall watches -- the beat counts are compile-time
# constants (P9_BASE_BEATS=8 / P9_WATCH_BEATS=20 / P9_RECOV_BEATS=8), so the
# full per-beat MARK trail is verifiable exactly.
MARKS_9=("p9a sb16_hot_bringup_mix")
for ((i = 1; i <= 8; i++));  do printf -v _n '%02d' "$i"; MARKS_9+=("p9a hb$_n"); done
MARKS_9+=("p9b hot_uart_entry_undrained")
for ((i = 1; i <= 20; i++)); do printf -v _n '%02d' "$i"; MARKS_9+=("p9b hb$_n"); done
MARKS_9+=("p9c drain_pending")
for ((i = 1; i <= 8; i++));  do printf -v _n '%02d' "$i"; MARKS_9+=("p9r hb$_n"); done
MARKS_9+=("p9c hot_uart_entry_drained" "p9c entry_ack_drain")
for ((i = 1; i <= 20; i++)); do printf -v _n '%02d' "$i"; MARKS_9+=("p9d hb$_n"); done
unset _n

# run_cell <cell> <tag> [timeout_s]
# Cell 9 emits ~170 fsync'd lines + four fixed-beat watches and can exceed the
# 90 s budget under host contention (observed: killed at hb06 in a 4-cell
# back-to-back run, then 16.5 s standalone) -- it gets a larger timeout.
run_cell() {
    local cell="$1" tag="$2" tmo="${3:-90}"
    local stage; stage="$(mktemp -d -t wbhot-smoke-"$cell".XXXXXX)"
    cp "$EXE" "$stage/WBHOT.EXE"
    cp "$CWSDPMI" "$stage/CWSDPMI.EXE"
    printf "@ECHO OFF\r\nSET BLASTER=%s\r\nSET DOSKUTSU_LOG_TAG=%s\r\nWBHOT.EXE %s\r\n" \
        "$BLASTER" "$tag" "$cell" > "$stage/RUN.BAT"

    echo "[wbhot-smoke] cell $cell (tag $tag): running under DOSBox-X..."
    timeout "$tmo" dosbox-x -conf "$CONF" -nopromptfolder \
        -c "MOUNT C $stage" -c "C:" -c "CALL RUN.BAT" -c "EXIT" \
        -exit -nogui -nomenu >"$LOG_DIR/dosbox-$cell.out" 2>&1

    local log="$stage/LOGS/${tag}PROBE.LOG"
    if [[ ! -f "$log" ]]; then
        echo "[wbhot-smoke] FAIL ($cell): LOGS\\${tag}PROBE.LOG not produced"
        rc=1
        [[ "$KEEP_STAGE" == "0" ]] && rm -rf "$stage"
        return
    fi
    cp "$log" "$LOG_DIR/${tag}PROBE.LOG"

    # Per-cell bring-up lines: M = raw stream (unchanged failing reference);
    # D/P/9 = production MIX path. Cell 9 has no serviced_hold drain-witness
    # line; its witness is the per-beat stall watch (p9 SUMMARY line).
    local need=(
        "wbhot v3"
        "prime: OK"
        "done: closed cleanly, exit 0"
    )
    case "$cell" in
        M)   need+=("init: SDL_Init(AUDIO) ok" "open: device live, stream bound" "drain-witness fill") ;;
        D|P) need+=("init: SDL_Init(AUDIO) + MIX_Init ok" "open: mixer up, looping tone track playing" "drain-witness fill") ;;
        9)   need+=("init: SDL_Init(AUDIO) + MIX_Init ok" "open: mixer up, looping tone track playing" "p9 SUMMARY:") ;;
    esac
    local m miss=0
    for m in "${need[@]}"; do
        if ! grep -qF "$m" "$log"; then
            echo "[wbhot-smoke] FAIL ($cell): missing marker: $m"
            miss=1; rc=1
        fi
    done

    # MARK sequence: every expected post-MARK present, and in file order.
    local -n marks="MARKS_$cell"
    local prev_line=0 line
    for m in "${marks[@]}"; do
        line="$(grep -nF "MARK post $m" "$log" | head -1 | cut -d: -f1)"
        if [[ -z "$line" ]]; then
            echo "[wbhot-smoke] FAIL ($cell): missing MARK post $m"
            miss=1; rc=1
            continue
        fi
        if (( line <= prev_line )); then
            echo "[wbhot-smoke] FAIL ($cell): MARK out of order: $m (line $line <= $prev_line)"
            miss=1; rc=1
        fi
        prev_line="$line"
    done

    # Sentinel guards.
    if grep -qF "INVALID:" "$log"; then
        echo "[wbhot-smoke] FAIL ($cell): INVALID sentinel present (device not hot / precondition missing)"
        rc=1; miss=1
    fi
    # Drain-witness mechanism validity: under DOSBox-X the DAC always drains,
    # so a STALLED verdict here means the witness itself is broken (e.g.
    # SDL_Delay services the ring behind our back) -- fail loudly rather than
    # ship an instrument that cannot distinguish ALIVE from STALLED.
    if grep -qF "DAC STALLED" "$log"; then
        echo "[wbhot-smoke] FAIL ($cell): drain-witness reports STALLED under DOSBox-X (witness mechanism invalid)"
        rc=1; miss=1
    fi
    # Cell-9 twin of the guard: DOSBox-X does not reproduce the stall, so any
    # beat=STALL here means the stall-watch witness itself is broken. Same for
    # a WITNESS-DEFECT (implausible delta) line.
    if [[ "$cell" == "9" ]]; then
        # Anchored on the hb verdict-line FORMAT, not a bare substring --
        # banner/EXPECT text must never satisfy this ([[grep_anchor_confound]]).
        if grep -qE '^p9[abrd] hb[0-9]+ t=[0-9]+ .* beat=STALL$' "$log"; then
            echo "[wbhot-smoke] FAIL ($cell): beat=STALL verdict under DOSBox-X (stall-watch witness invalid)"
            rc=1; miss=1
        fi
        if grep -qF "WITNESS-DEFECT" "$log"; then
            echo "[wbhot-smoke] FAIL ($cell): WITNESS-DEFECT emitted (implausible fill delta)"
            rc=1; miss=1
        fi
    fi
    if [[ "$miss" == "0" ]]; then
        echo "[wbhot-smoke] OK   ($cell): all markers present + in order"
    fi
    [[ "$KEEP_STAGE" == "0" ]] && rm -rf "$stage"
}

run_cell P WP1
run_cell M WM1
run_cell D WD1
run_cell 9 WP9 300

if [[ "$rc" == "0" ]]; then
    echo "[wbhot-smoke] PASS -- structural correctness only (the ISA IOCHRDY stall"
    echo "              is g2k-DX2-66-only; a clean cell M here is NOT 'no repro')."
else
    echo "[wbhot-smoke] FAIL -- see markers above; logs in $LOG_DIR"
fi
exit "$rc"
