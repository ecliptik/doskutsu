#!/usr/bin/env bash
# verify-patches-applied.sh — pre-flight that each vendor's commit count
# matches its patches/*.patch count.
#
# Non-destructive. For each vendor in vendor/sources.manifest with a concrete
# pinned SHA and a corresponding patches/<name>/ directory, compares:
#   - count of patches/<name>/*.patch (top level; excludes _disabled/)
#   - count of commits in vendor/<name>/ since the pinned SHA
#
# Mismatch means a patch landed in patches/<name>/ but was never `git am`'d
# to vendor/<name>/ (or vice versa: extra commits in vendor without
# corresponding patch files). Exits non-zero with concrete remediation.
#
# Catches the latent-failure-mode counterpart to the wave-38 stale-.obj
# root cause: the wave-38 patches DID `git am` to vendor (per the commit
# log timestamps), but the next time a patch lands in patches/<name>/
# without a manual `git am`, the build would silently produce a binary
# missing it — `make patches` reorchestrates `apply-patches.sh`, this
# pre-flight verifies the resulting state matches expectations.
#
# Wired into the Makefile as a regular dependency of every build-stage
# convenience target (sdl3 / sdl3-mixer / sdl3-image / nxengine / all).
#
# Usage:
#   ./scripts/verify-patches-applied.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="$REPO_ROOT/vendor/sources.manifest"
VENDOR_DIR="$REPO_ROOT/vendor"
PATCHES_DIR="$REPO_ROOT/patches"

log() { printf '[verify-patches] %s\n' "$*" >&2; }

if [[ ! -f "$MANIFEST" ]]; then
    log "error: $MANIFEST not found"
    exit 1
fi

rc=0
while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue

    # shellcheck disable=SC2162
    read -r name _url _ref sha <<<"$line"
    [[ -z "${name:-}" ]] && continue
    [[ "$sha" == "PIN_ME" ]] && continue

    vendor_path="$VENDOR_DIR/$name"
    patches_path="$PATCHES_DIR/$name"

    # No patches/<name>/ dir → no patch series for this vendor → nothing to verify.
    [[ -d "$patches_path" ]] || continue

    # Vendor tree must exist and be a git repo so we can count commits.
    if [[ ! -d "$vendor_path/.git" ]]; then
        log "$name: vendor tree missing or not a git repo at $vendor_path"
        log "$name:    fix: ./scripts/fetch-sources.sh"
        rc=1
        continue
    fi

    n_patches=$(find "$patches_path" -maxdepth 1 -name '*.patch' -type f | wc -l)
    n_applied=$(cd "$vendor_path" && git log --oneline "${sha}..HEAD" 2>/dev/null | wc -l)

    if [[ "$n_patches" -ne "$n_applied" ]]; then
        log "$name: MISMATCH — $n_patches patch(es) in patches/$name/, $n_applied commit(s) since pinned SHA $sha"
        log "$name:    delta = $((n_patches - n_applied))"
        log "$name:    fix:  make patches      (re-applies the full series; idempotent)"
        log "$name:           OR (cd $vendor_path && git am path/to/missing.patch)  (incremental)"
        rc=1
    fi
done < "$MANIFEST"

if [[ "$rc" -ne 0 ]]; then
    log "patches-applied check FAILED — see above"
    exit 1
fi
exit 0
