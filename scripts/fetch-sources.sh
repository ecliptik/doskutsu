#!/usr/bin/env bash
# fetch-sources.sh — clone (or update) the five vendored upstreams per
# vendor/sources.manifest. Each entry specifies a URL, a tracking ref
# (branch or tag), and a pinned SHA.
#
# Behavior:
#   - If vendor/<name>/ is absent, clone shallow from URL.
#   - If vendor/<name>/ exists, fetch and reset --hard to the pinned SHA.
#   - If the manifest's SHA is the literal string PIN_ME, fetch the ref,
#     resolve its HEAD SHA, print it (for the operator to copy into the
#     manifest), and leave the working tree at that SHA. This is the
#     Phase-2 bootstrap convenience — normal operation has concrete SHAs.
#
# Run AFTER scripts/setup-symlinks.sh (not strictly required, but build
# order is: symlinks -> fetch -> patches -> make).
#
# Usage:
#   ./scripts/fetch-sources.sh            # fetch all missing/out-of-date
#   ./scripts/fetch-sources.sh SDL        # fetch only the named entry
#   ./scripts/fetch-sources.sh --resolve  # resolve PIN_ME entries to concrete SHAs

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="$REPO_ROOT/vendor/sources.manifest"
VENDOR_DIR="$REPO_ROOT/vendor"

RESOLVE_MODE=0
FILTER=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --resolve)   RESOLVE_MODE=1; shift ;;
        -h|--help)
            echo "Usage: fetch-sources.sh [--resolve] [<name>]"
            echo
            echo "  --resolve   Resolve PIN_ME entries to concrete SHAs (prints for manual copy)."
            echo "  <name>      Only process the named entry from vendor/sources.manifest."
            exit 0
            ;;
        *) FILTER="$1"; shift ;;
    esac
done

log() { printf '[fetch-sources] %s\n' "$*" >&2; }

if [[ ! -f "$MANIFEST" ]]; then
    log "error: $MANIFEST not found"
    exit 1
fi

# Parse the manifest. Format, per vendor/sources.manifest:
#   <name> <url> <ref> <sha>
# '#' introduces comments; blank lines ignored.
while IFS= read -r line || [[ -n "$line" ]]; do
    # Strip comments and leading/trailing whitespace
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue

    # shellcheck disable=SC2162
    read -r name url ref sha <<<"$line"

    if [[ -z "${name:-}" || -z "${url:-}" || -z "${ref:-}" || -z "${sha:-}" ]]; then
        log "warning: malformed manifest entry: $line"
        continue
    fi

    if [[ -n "$FILTER" && "$FILTER" != "$name" ]]; then
        continue
    fi

    dest="$VENDOR_DIR/$name"

    if [[ "$RESOLVE_MODE" == "1" ]]; then
        # Lightweight resolve: ls-remote the ref and print its SHA. Don't clone.
        log "resolving $name ($url $ref)..."
        resolved="$(git ls-remote "$url" "$ref" | awk '{print $1}' | head -n1)"
        if [[ -z "$resolved" ]]; then
            log "error: could not resolve $name's ref '$ref' at $url"
            exit 1
        fi
        printf '%s\t%s\t%s\t%s\n' "$name" "$url" "$ref" "$resolved"
        continue
    fi

    if [[ ! -d "$dest" ]]; then
        log "$name: cloning $url -> $dest"
        git clone "$url" "$dest"
    else
        log "$name: fetching latest refs"
        (cd "$dest" && git fetch --tags --all --prune)
    fi

    # Handle PIN_ME: check out the tip of ref, print the resolved SHA for
    # the operator to copy back into the manifest.
    if [[ "$sha" == "PIN_ME" ]]; then
        log "$name: SHA is PIN_ME — checking out tip of '$ref'"
        (cd "$dest" && git checkout --detach "$ref")
        resolved="$(cd "$dest" && git rev-parse HEAD)"
        log "$name: resolved $ref -> $resolved"
        log "       update vendor/sources.manifest: replace PIN_ME with $resolved"
        continue
    fi

    # Concrete SHA: reset to it.
    log "$name: checking out pinned SHA $sha"
    (cd "$dest" && git fetch --depth 1 origin "$sha" 2>/dev/null || git fetch origin) >/dev/null 2>&1 || true
    if ! (cd "$dest" && git cat-file -e "$sha^{commit}" 2>/dev/null); then
        log "error: $name: SHA $sha not found after fetch"
        log "       Check manifest entry or re-resolve with: $0 --resolve $name"
        exit 1
    fi
    (cd "$dest" && git reset --hard "$sha")

done < "$MANIFEST"

log "done."
