#!/usr/bin/env bash
# stage-tas.sh -- copy the canonical PLAY.TAS recording into build/stage/.
#
# Why this exists (wave-44 tri-env regression, 2026-05-14):
#   `make stage` previously did not copy a PLAY.TAS, so any DOSBox-X smoke
#   that engaged TAS replay either (a) ran with an ambient stub left behind
#   by a prior session (build/stage/PLAY.TAS at 28 bytes -- just the DTASv1
#   header), or (b) found no file at all. The 28-byte stub auto-exits at
#   tick 1 because patch 0135 v1.1's EOF-auto-exit fires the instant the
#   replay buffer is exhausted. Result: gameplay-class smoke was effectively
#   a boot/init smoke; the engine never ran the Mimiga BK_PARALLAX scene
#   (~tick 2593 on the canonical 1932-byte recording).
#
#   That broke the tri-env compare from wave-44 onward: T1 (DOSBox-X) ran
#   "boot then exit" while T3 (real-HW) ran the full ~52-second scene. No
#   useful scaling factor could be computed.
#
#   This script stages the operator's canonical recording so DOSBox-X smoke
#   exercises the same scene the real-HW iter exercises.
#
# Source path (DOSKUTSU_TAS_SRC env override, else default search):
#   1) $DOSKUTSU_TAS_SRC (if set; explicit override always wins)
#   2) /tmp/wave-43-archaeology/PLAY.TAS (current canonical; 1932 bytes,
#      sha 14c1e3b94f57...)
#   3) /tmp/wave-42/PLAY.TAS (older but identical sha; flush-instr's
#      addendum names this; same content)
#
# Destination:
#   $REPO_ROOT/build/stage/PLAY.TAS         (primary; root of mounted C:)
#   $REPO_ROOT/build/stage/DOSKUTSU/PLAY.TAS (secondary; some smoke paths
#                                            cd to C:\DOSKUTSU before
#                                            launching the engine and the
#                                            TAS code fopens "PLAY.TAS"
#                                            relative to CWD)
#
# Idempotent: re-runs sha-check before copying. Safe to invoke from
# `make stage` or as a manual one-shot.
#
# Min-size sanity: refuses to stage a TAS smaller than $MIN_BYTES (default
# 1000). The 28-byte stub bug above was at 28 bytes; the canonical
# recording is 1932 bytes. 1000 is a wide-margin floor that rejects stubs
# but does not preclude shorter legitimate recordings if someone ever
# stages a different scene.
#
# Usage:
#   ./scripts/stage-tas.sh                              # use defaults
#   DOSKUTSU_TAS_SRC=/path/to/MY.TAS ./scripts/stage-tas.sh
#   ./scripts/stage-tas.sh --quiet                      # only print on error

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

QUIET=0
while (($#)); do
    case "$1" in
        --quiet|-q) QUIET=1 ;;
        -h|--help)
            sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "[stage-tas] unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

log() {
    if [[ "$QUIET" == "0" ]]; then
        printf '[stage-tas] %s\n' "$*" >&2
    fi
}
err() {
    printf '[stage-tas] error: %s\n' "$*" >&2
}

MIN_BYTES="${DOSKUTSU_TAS_MIN_BYTES:-1000}"

# 1) Resolve source TAS path.
candidates=()
if [[ -n "${DOSKUTSU_TAS_SRC:-}" ]]; then
    candidates+=("$DOSKUTSU_TAS_SRC")
fi
candidates+=(
    "/tmp/wave-43-archaeology/PLAY.TAS"
    "/tmp/wave-42/PLAY.TAS"
)

SRC=""
for cand in "${candidates[@]}"; do
    if [[ -f "$cand" ]]; then
        SRC="$cand"
        break
    fi
done

if [[ -z "$SRC" ]]; then
    err "no canonical PLAY.TAS found. Tried:"
    for cand in "${candidates[@]}"; do
        printf '         %s\n' "$cand" >&2
    done
    err "set DOSKUTSU_TAS_SRC=/path/to/PLAY.TAS to override, or place the"
    err "canonical recording at /tmp/wave-43-archaeology/PLAY.TAS."
    exit 3
fi

# 2) Validate source: size + DTASv1 magic.
src_size=$(stat -c%s "$SRC")
if (( src_size < MIN_BYTES )); then
    err "source $SRC is $src_size bytes (< $MIN_BYTES); refusing to stage stub."
    err "this is the bug from 2026-05-14 wave-44; set DOSKUTSU_TAS_SRC to a"
    err "real recording or update the canonical at /tmp/wave-43-archaeology/."
    exit 4
fi

# DTASv1 magic = bytes 0..6 = "DTASv1\n". Check the "DTAS" prefix (first 4
# bytes) -- permissive enough that v2/v3 future-version TAS files would
# also pass; the engine's own loader handles version negotiation.
prefix=$(head -c 4 "$SRC")
if [[ "$prefix" != "DTAS" ]]; then
    err "source $SRC does not begin with 'DTAS' magic (got prefix '$prefix')."
    exit 5
fi

src_sha=$(sha256sum "$SRC" | awk '{print $1}')
log "source: $SRC ($src_size bytes, sha ${src_sha:0:12})"

# 3) Stage to primary + secondary destinations.
STAGE_DIR="$REPO_ROOT/build/stage"
mkdir -p "$STAGE_DIR" "$STAGE_DIR/DOSKUTSU"

stage_one() {
    local dst="$1"
    if [[ -f "$dst" ]]; then
        local dst_sha
        dst_sha=$(sha256sum "$dst" | awk '{print $1}')
        if [[ "$dst_sha" == "$src_sha" ]]; then
            log "  $dst already up-to-date (sha ${dst_sha:0:12})"
            return 0
        fi
    fi
    install -m 0644 "$SRC" "$dst"
    log "  staged $dst"
}

stage_one "$STAGE_DIR/PLAY.TAS"
stage_one "$STAGE_DIR/DOSKUTSU/PLAY.TAS"

log "done."
