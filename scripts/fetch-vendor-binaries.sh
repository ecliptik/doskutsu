#!/usr/bin/env bash
# fetch-vendor-binaries.sh — populate vendor/<name>/<binary> entries from
# vendor/binaries.manifest. Idempotent: skips files that already exist with
# the manifest-pinned sha256.
#
# Why this exists: the four DOS binaries we redistribute (CWSDPMI's DPMI host,
# LFNDOS, two DOSLFN variants) are no longer tracked in git as of 2026-04-30.
# This script fetches them on demand. The accompanying license / .doc / .lsm
# files DO stay tracked because the redistribution licenses require them.
#
# Usage:
#   ./scripts/fetch-vendor-binaries.sh            # fetch all entries
#   ./scripts/fetch-vendor-binaries.sh --check    # verify sha256 only, no fetch
#
# Run this once after fetch-sources.sh on a fresh clone (the bootstrap script
# calls it for you). The Makefile's stage / dist / install / dpmi-lfn-smoke
# targets also depend on it via an order-only prerequisite.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="$REPO_ROOT/vendor/binaries.manifest"

mode=fetch
case "${1:-}" in
    --check)  mode=check ;;
    --help|-h) sed -n '/^# /,/^$/p' "$0" | sed 's/^# \?//'; exit 0 ;;
    "")       ;;
    *)        echo "fetch-vendor-binaries: unknown arg '$1'" >&2; exit 2 ;;
esac

log()  { printf '[fetch-vendor-binaries] %s\n' "$*" >&2; }
fail() { log "error: $*"; exit 1; }

command -v curl   >/dev/null 2>&1 || fail "curl not found on PATH"
command -v unzip  >/dev/null 2>&1 || fail "unzip not found on PATH"
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum not found on PATH"

[[ -f "$MANIFEST" ]] || fail "manifest not found: $MANIFEST"

# Verify the bytes at $1 hash to $2. Returns 0 on match, 1 on mismatch.
verify_sha256() {
    local path=$1 expected=$2 got
    [[ -f "$path" ]] || return 1
    got=$(sha256sum "$path" | awk '{print $1}')
    [[ "$got" == "$expected" ]]
}

# Fetch one manifest entry. Args: <path> <url> <sha256>.
fetch_one() {
    local path=$1 url=$2 expected_sha=$3
    local abs="$REPO_ROOT/$path"
    local member=""

    # URL syntax: zip-extract has '#<member>' suffix.
    if [[ "$url" == *'#'* ]]; then
        member="${url#*#}"
        url="${url%%#*}"
    fi

    if verify_sha256 "$abs" "$expected_sha"; then
        log "ok    $path  (sha matches)"
        return 0
    fi

    if [[ "$mode" == check ]]; then
        log "fail  $path  (sha mismatch or missing)"
        return 1
    fi

    log "fetch $path  ←  $url${member:+  (member: $member)}"
    mkdir -p "$(dirname "$abs")"

    if [[ -n "$member" ]]; then
        # Zip path: download archive to tempfile, extract member, verify.
        local tmpzip
        tmpzip=$(mktemp --suffix=.zip)
        # shellcheck disable=SC2064  # $tmpzip is captured at trap install
        trap "rm -f '$tmpzip'" EXIT
        curl -fsSL "$url" -o "$tmpzip" || fail "curl failed: $url"
        unzip -p "$tmpzip" "$member" > "$abs" || fail "unzip member '$member' failed"
        rm -f "$tmpzip"
        trap - EXIT
    else
        curl -fsSL "$url" -o "$abs" || fail "curl failed: $url"
    fi

    verify_sha256 "$abs" "$expected_sha" \
        || fail "sha256 mismatch on $path  (expected $expected_sha, got $(sha256sum "$abs" | awk '{print $1}'))"

    log "ok    $path  (fetched + verified)"
}

rc=0
while read -r line; do
    # Skip blank lines and full-line comments. We do NOT strip mid-line
    # '#' because the URL syntax uses '#<member>' for zip-extract entries
    # (e.g. csdpmi7b.zip#bin/CWSDPMI.EXE). Pure-Bash trim avoids quoting
    # surprises that 'xargs' has with apostrophes inside comment text.
    line="${line#"${line%%[![:space:]]*}"}"   # ltrim
    line="${line%"${line##*[![:space:]]}"}"   # rtrim
    [[ -z "$line" ]] && continue
    [[ "$line" == \#* ]] && continue
    # shellcheck disable=SC2086  # word-split is intentional here
    set -- $line
    [[ $# -eq 3 ]] || fail "manifest line malformed (expected 3 fields): $line"
    fetch_one "$1" "$2" "$3" || rc=1
done < "$MANIFEST"

if [[ "$mode" == check ]]; then
    if [[ $rc -eq 0 ]]; then
        log "all binaries verified."
    else
        log "verification failed — run without --check to fetch."
    fi
fi

exit $rc
