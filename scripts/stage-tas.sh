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
#   2) $REPO_ROOT/tests/tas-segments/CALIB-MIMIGA-ORG.TAS  (TRACKED in-repo
#      canonical: the WARP_EVENT=90 ~5500-tick Mimiga organya calib segment
#      the v1.0.x audio campaign uses; 220 bytes = 20-byte DTASv1 header + 25
#      events; sha256 0e593ec5... / sha1 0ce2f1b1300a...). Committed as a
#      golden-master fixture (same convention as SEG12.TAS) so it SURVIVES
#      /tmp cleanup -- the durable fix for the 2026-06-03 wrong-TAS landmine
#      (the old /tmp-only canonical got cleaned -> fallback to a wrong TAS).
#   3) /tmp/wave-43-archaeology/PLAY.TAS (LEGACY ephemeral fallback; was the
#      old canonical, gets cleaned -- kept only as a last resort)
#   4) /tmp/wave-42/PLAY.TAS (legacy ephemeral fallback; same content)
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
# Validity sanity: DTASv1 STRUCTURAL check (NOT a byte floor). The old ">=1000
# bytes" floor predated the segmented-TAS era (patch 0191) and FALSE-REJECTED
# valid short segments -- the canonical calib segment is only 220 bytes, so the
# 1000-floor refused it and forced the wrong-TAS fallback (the 2026-06-03
# landmine). Per docs/TAS.md a recording is a 20-byte header (magic "DTASv1\n"
# + version + prng_seed + flags + n_events) then 8-byte events. VALID = "DTAS"
# magic AND size >= 28 (header + >=1 event) AND (size-20) a whole number of
# 8-byte events. The stub to reject = header-only (~20 bytes, 0 events).
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

# 1) Resolve source TAS path. In-repo tracked fixture FIRST (survives /tmp
#    cleanup); ephemeral /tmp paths only as legacy last-resort fallbacks.
candidates=()
if [[ -n "${DOSKUTSU_TAS_SRC:-}" ]]; then
    candidates+=("$DOSKUTSU_TAS_SRC")
fi
candidates+=(
    "$REPO_ROOT/tests/tas-segments/CALIB-MIMIGA-ORG.TAS"
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
    err "set DOSKUTSU_TAS_SRC=/path/to/PLAY.TAS to override, or restore the"
    err "tracked fixture tests/tas-segments/CALIB-MIMIGA-ORG.TAS."
    exit 3
fi

# 2) Validate source: DTASv1 STRUCTURAL check (magic + >=1 whole event).
#    NOT a byte floor -- the old >=1000 floor false-rejected valid segments
#    (the 220-byte canonical), which caused the wrong-TAS fallback landmine.
src_size=$(stat -c%s "$SRC")
# DTASv1 magic = bytes 0..6 = "DTASv1\n". Check the "DTAS" prefix (first 4
# bytes) -- permissive enough that v2/v3 future-version TAS files would
# also pass; the engine's own loader handles version negotiation.
prefix=$(head -c 4 "$SRC")
if [[ "$prefix" != "DTAS" ]]; then
    err "source $SRC does not begin with 'DTAS' magic (got prefix '$prefix')."
    exit 5
fi
# 20-byte header + at least one whole 8-byte event = >=28 bytes, (size-20)%8==0.
if (( src_size < 28 || (src_size - 20) % 8 != 0 )); then
    err "source $SRC is $src_size bytes -- DTASv1 header-only/truncated stub"
    err "(need magic + >=1 whole 8-byte event = >=28 bytes, (size-20)%8==0)."
    err "this is the wave-44 stub shape; set DOSKUTSU_TAS_SRC to a real recording."
    exit 4
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
