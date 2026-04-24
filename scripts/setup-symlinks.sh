#!/usr/bin/env bash
# setup-symlinks.sh — wire this repo to the shared ~/emulators/ toolchain hub.
#
# The DJGPP cross-compiler lives canonically at ~/emulators/tools/djgpp/
# alongside the sibling projects (vellm, geomys, flynn). Symlinking it into
# tools/djgpp keeps the Makefile's PATH export project-local (no $HOME
# assumption in build rules) while not duplicating the toolchain.
#
# See ~/emulators/CLAUDE.md for the hub convention.
#
# Idempotent: re-running is safe. Will replace an existing symlink but will
# refuse to overwrite a real directory (in case someone manually installed
# DJGPP locally).
#
# Usage:
#   ./scripts/setup-symlinks.sh                 # link to ~/emulators/tools/djgpp
#   EMULATORS_ROOT=/other/path ./scripts/setup-symlinks.sh   # override hub location

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EMULATORS_ROOT="${EMULATORS_ROOT:-$HOME/emulators}"
DJGPP_SRC="$EMULATORS_ROOT/tools/djgpp"
DJGPP_LINK="$REPO_ROOT/tools/djgpp"

log() { printf '[setup-symlinks] %s\n' "$*" >&2; }

if [[ ! -d "$EMULATORS_ROOT" ]]; then
    log "error: $EMULATORS_ROOT does not exist."
    log "       Expected the ~/emulators/ hub to be cloned. See doskutsu's BUILDING.md."
    exit 1
fi

if [[ ! -d "$DJGPP_SRC" ]]; then
    log "warning: $DJGPP_SRC does not exist."
    log "         The symlink will be created anyway, but DJGPP must be installed"
    log "         there before you can build. Run: ~/emulators/scripts/update-djgpp.sh"
fi

mkdir -p "$REPO_ROOT/tools"

if [[ -L "$DJGPP_LINK" ]]; then
    current="$(readlink "$DJGPP_LINK")"
    if [[ "$current" == "$DJGPP_SRC" ]]; then
        log "tools/djgpp already points to $DJGPP_SRC. Nothing to do."
        exit 0
    fi
    log "replacing existing symlink: $DJGPP_LINK -> $current  =>  $DJGPP_SRC"
    rm "$DJGPP_LINK"
elif [[ -e "$DJGPP_LINK" ]]; then
    log "error: $DJGPP_LINK exists and is not a symlink."
    log "       Refusing to overwrite. Remove or rename it manually if you want"
    log "       to use the shared ~/emulators/tools/djgpp installation."
    exit 1
fi

ln -s "$DJGPP_SRC" "$DJGPP_LINK"
log "created symlink: $DJGPP_LINK -> $DJGPP_SRC"

# Sanity: emit what the Makefile will see
if [[ -x "$DJGPP_LINK/bin/i586-pc-msdosdjgpp-gcc" ]]; then
    log "toolchain looks good:"
    "$DJGPP_LINK/bin/i586-pc-msdosdjgpp-gcc" --version | head -n1 | sed 's/^/[setup-symlinks]   /' >&2
else
    log "note: $DJGPP_LINK/bin/i586-pc-msdosdjgpp-gcc not found yet."
    log "      Install with: ~/emulators/scripts/update-djgpp.sh"
fi
