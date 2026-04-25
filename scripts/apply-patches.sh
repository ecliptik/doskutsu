#!/usr/bin/env bash
# apply-patches.sh — apply patches/<name>/*.patch to vendor/<name>/.
#
# Patches are produced by `git format-patch` from a working branch in the
# vendor tree and numbered lexically (0001-, 0002-, ...). Ordering matters;
# patches assume earlier patches are already applied.
#
# Behavior:
#   1. For each entry in vendor/sources.manifest that has a concrete SHA
#      (not PIN_ME), `git reset --hard` the vendor tree to that SHA
#      (discarding any prior patches, so this is idempotent).
#   2. Apply every `patches/<name>/*.patch` in lexical order via `git am`.
#   3. If a patch fails to apply, abort the `git am` cleanly, print the
#      failing path, and exit non-zero.
#
# Run AFTER scripts/fetch-sources.sh (the patches assume the vendor tree
# is populated and at the pinned SHA).
#
# Usage:
#   ./scripts/apply-patches.sh             # apply to all vendors
#   ./scripts/apply-patches.sh nxengine-evo # only the named vendor

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="$REPO_ROOT/vendor/sources.manifest"
VENDOR_DIR="$REPO_ROOT/vendor"
PATCHES_DIR="$REPO_ROOT/patches"

FILTER=""
if [[ $# -gt 0 ]]; then
    case "$1" in
        -h|--help)
            echo "Usage: apply-patches.sh [<name>]"
            echo "  <name>  Only apply patches for the named vendor (e.g. 'nxengine-evo')"
            exit 0
            ;;
        *) FILTER="$1" ;;
    esac
fi

log() { printf '[apply-patches] %s\n' "$*" >&2; }

if [[ ! -f "$MANIFEST" ]]; then
    log "error: $MANIFEST not found"
    exit 1
fi

apply_one() {
    local name="$1"
    local sha="$2"
    local vendor_path="$VENDOR_DIR/$name"
    local patches_path="$PATCHES_DIR/$name"

    if [[ ! -d "$vendor_path" ]]; then
        log "$name: vendor tree not present — run scripts/fetch-sources.sh first"
        return 1
    fi

    # Reset to pinned SHA so patch application is idempotent.
    if [[ "$sha" != "PIN_ME" ]]; then
        log "$name: resetting to pinned SHA $sha"
        (cd "$vendor_path" && git reset --hard "$sha") >/dev/null
    else
        log "$name: SHA is PIN_ME, skipping reset (using whatever fetch-sources.sh left)"
    fi

    # Abort any in-progress `git am` from a previous failed run.
    if [[ -d "$vendor_path/.git/rebase-apply" ]]; then
        log "$name: cleaning up stale .git/rebase-apply from prior run"
        (cd "$vendor_path" && git am --abort) 2>/dev/null || true
    fi

    if [[ ! -d "$patches_path" ]]; then
        log "$name: no patches/ directory — nothing to apply"
        return 0
    fi

    # Collect .patch files in lexical order. If none, that's fine.
    #
    # LC_ALL=C forces ASCII byte-order sort. Without it, glibc's default
    # locale-aware collation treats `-` (0x2D) as punctuation that gets
    # promoted next to alphabetics — so a filename like `0014a-...patch`
    # would sort BEFORE `0014-...patch` on en_US.UTF-8 even though ASCII
    # byte order has the reverse (0x2D < 0x61). Caught by nxengine during
    # Phase 5 attempt 4 when sdl-engine's `0014a` and `0010a` follow-up
    # patches both surfaced the bug. Renumbering those files to pure-
    # numeric slots (0020, 0021) sidestepped the immediate apply-order
    # problem; this LC_ALL=C export makes the durable fix so any future
    # contributor can use any naming scheme without locale-fragility.
    local patches=()
    while IFS= read -r -d '' p; do
        patches+=("$p")
    done < <(find "$patches_path" -maxdepth 1 -name '*.patch' -type f -print0 | LC_ALL=C sort -z)

    if [[ "${#patches[@]}" -eq 0 ]]; then
        log "$name: no *.patch files in $patches_path — nothing to apply"
        return 0
    fi

    log "$name: applying ${#patches[@]} patch(es)"
    # git am reads From: headers etc.; git format-patch output is its native input.
    # Pass patches explicitly rather than via stdin so error messages reference
    # the failing file path.
    if ! (cd "$vendor_path" && git am --keep-cr "${patches[@]}"); then
        log "$name: git am failed — last patch left conflicts in $vendor_path"
        log "       Inspect with: (cd $vendor_path && git status)"
        log "       Abort with:   (cd $vendor_path && git am --abort)"
        return 1
    fi

    log "$name: all patches applied cleanly"
}

# Walk the manifest
rc=0
while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue

    # shellcheck disable=SC2162
    read -r name _url _ref sha <<<"$line"
    [[ -z "${name:-}" ]] && continue
    if [[ -n "$FILTER" && "$FILTER" != "$name" ]]; then
        continue
    fi

    if ! apply_one "$name" "$sha"; then
        rc=1
    fi
done < "$MANIFEST"

if [[ "$rc" -ne 0 ]]; then
    log "one or more vendors had patch failures — see above"
    exit 1
fi
log "done."
