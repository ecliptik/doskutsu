#!/usr/bin/env bash
#
# scripts/bootstrap.sh — one-shot setup: prerequisites → DJGPP check →
# vendor sources → patches → build → optional asset extraction → stage.
#
# After this finishes successfully you have:
#   - build/doskutsu.exe       the DJGPP-built game binary
#   - build/stage/             runtime layout (DOSKUTSU.EXE + CWSDPMI + DATA)
#                              ready for tools/dosbox-launch.sh
#
# Usage:
#   ./scripts/bootstrap.sh
#   ./scripts/bootstrap.sh --cave-story-exe /path/to/Doukutsu.exe
#
# Options:
#   --cave-story-exe FILE   Extract Cave Story assets from the named EXE
#                           into ./data/ as a final step. If omitted, the
#                           script prints instructions for doing it later.
#   --skip-djgpp-check      Skip the DJGPP-toolchain probe (useful if you
#                           know your DJGPP_PREFIX is set out-of-band).
#   -h, --help              Show this help.
#
# Environment overrides:
#   EMULATORS_ROOT=/path     Use a non-default ~/emulators/ hub location
#                            (passed through to scripts/setup-symlinks.sh).
#   DJGPP_PREFIX=/path       Skip the symlink step; use this DJGPP install
#                            directly. Caller is responsible for ensuring
#                            $DJGPP_PREFIX/bin is on the build's PATH (the
#                            Makefile reads this variable).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

CAVE_EXE=""
SKIP_DJGPP_CHECK=0

while (($#)); do
  case "$1" in
    --cave-story-exe) shift; CAVE_EXE="${1:-}";;
    --skip-djgpp-check) SKIP_DJGPP_CHECK=1 ;;
    -h|--help) sed -n '/^# Usage:/,/^# Environment overrides:/p' "$0" | sed 's/^# \{0,1\}//; /Environment/q'; exit 0 ;;
    *) echo "bootstrap: unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

step() {
  printf '\n\033[1;36m== %s ==\033[0m\n' "$*"
}

note() {
  printf '   %s\n' "$*"
}

fail() {
  printf '\033[1;31merror:\033[0m %s\n' "$*" >&2
  exit 1
}

# ----------------------------------------------------------------------------
step "1/7  Prerequisites on the host"

missing=()
for tool in cmake git make gcc python3 unzip; do
  command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if ((${#missing[@]})); then
  fail "missing host tools: ${missing[*]}
     Install via your package manager (apt: 'apt install build-essential cmake git python3 unzip',
     brew: 'brew install cmake git make python3', etc.) and re-run."
fi
note "cmake / git / make / gcc / python3 / unzip all present"

# ----------------------------------------------------------------------------
step "2/7  DJGPP cross-toolchain"

if [[ "$SKIP_DJGPP_CHECK" == 0 ]]; then
  if [[ -n "${DJGPP_PREFIX:-}" ]]; then
    [[ -x "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-gcc" ]] \
      || fail "DJGPP_PREFIX=$DJGPP_PREFIX does not contain bin/i586-pc-msdosdjgpp-gcc.
     Verify the path or unset DJGPP_PREFIX to use the default tools/djgpp/ symlink."
    note "using DJGPP_PREFIX=$DJGPP_PREFIX (skipping symlink setup)"
  else
    if [[ -d "${EMULATORS_ROOT:-$HOME/emulators}" ]]; then
      ./scripts/setup-symlinks.sh
    else
      note "no ~/emulators/ hub found and DJGPP_PREFIX not set"
      note ""
      note "DJGPP installation options:"
      note "  1. Install build-djgpp (https://github.com/andrewwutw/build-djgpp):"
      note "       git clone https://github.com/andrewwutw/build-djgpp.git"
      note "       cd build-djgpp && ./build-djgpp.sh 12.2.0     # ~30 minutes"
      note "     Then re-run with DJGPP_PREFIX=\$HOME/djgpp ./scripts/bootstrap.sh"
      note ""
      note "  2. If you already have DJGPP installed elsewhere, set:"
      note "       DJGPP_PREFIX=/path/to/djgpp ./scripts/bootstrap.sh"
      note ""
      note "  3. Skip this check entirely (you know what you're doing):"
      note "       ./scripts/bootstrap.sh --skip-djgpp-check"
      fail "DJGPP not found"
    fi
    [[ -x "tools/djgpp/bin/i586-pc-msdosdjgpp-gcc" ]] \
      || fail "tools/djgpp/ symlink exists but DJGPP isn't actually installed there.
     Run: ~/emulators/scripts/update-djgpp.sh
     Or set DJGPP_PREFIX=/path/to/djgpp and re-run."
    note "tools/djgpp/bin/i586-pc-msdosdjgpp-gcc verified"
  fi
else
  note "DJGPP check skipped per --skip-djgpp-check"
fi

# ----------------------------------------------------------------------------
step "3/7  Fetch vendored upstreams (SDL3, SDL3_mixer, SDL3_image, NXEngine-evo)"

./scripts/fetch-sources.sh
note "vendor/ populated at pinned SHAs from sources.manifest"

# ----------------------------------------------------------------------------
step "4/7  Apply DOS-port patches"

./scripts/apply-patches.sh
note "patches applied to all vendor trees"

# ----------------------------------------------------------------------------
step "5/7  Build the four-stage chain (SDL3 → SDL3_mixer → SDL3_image → NXEngine-evo)"

make all
note "build/doskutsu.exe produced ($(stat -c %s build/doskutsu.exe 2>/dev/null || stat -f %z build/doskutsu.exe) bytes)"

# ----------------------------------------------------------------------------
step "6/7  Cave Story game data"

if [[ -n "$CAVE_EXE" ]]; then
  [[ -f "$CAVE_EXE" ]] || fail "--cave-story-exe path does not exist: $CAVE_EXE"
  python3 scripts/extract-engine-data.py "$CAVE_EXE" data
  note "engine data extracted to ./data/"

  # User likely also has Cave Story content (sprites, maps, music) extracted
  # via doukutsu-rs / NXExtract / cavestory.one. The 8.3 renamer is idempotent:
  # safe to run whether or not those exist yet.
  ./scripts/rename-user-data-83.sh data
  note "8.3 rename helper applied (idempotent)"
else
  note "--cave-story-exe not provided; skipping asset extraction."
  note ""
  note "  To get Cave Story assets: download the 2004 EN freeware Doukutsu.exe"
  note "  from https://www.cavestory.org/downloads/cavestoryen.zip (or similar"
  note "  archive of Pixel's freeware release), unzip it, and:"
  note ""
  note "    python3 scripts/extract-engine-data.py /path/to/Doukutsu.exe data"
  note "    ./scripts/rename-user-data-83.sh data"
  note ""
  note "  You'll also need the rest of the game content (sprites, maps, music,"
  note "  TSC scripts) — extract via doukutsu-rs / NXExtract / cavestory.one"
  note "  per docs/ASSETS.md, drop them into ./data/, then re-run the 8.3 renamer."
fi

# ----------------------------------------------------------------------------
step "7/7  Stage runtime layout"

if [[ -d data ]]; then
  make stage
  note "build/stage/ ready (DOSKUTSU.EXE + CWSDPMI.EXE + data/)"
else
  note "skipping make stage (no data/ directory yet)"
fi

# ----------------------------------------------------------------------------
printf '\n\033[1;32m== Done ==\033[0m\n'
note "Binary:  build/doskutsu.exe"
note "Test:    tools/dosbox-launch.sh --stage --exe DOSKUTSU.EXE   (visible)"
note "         make smoke-fast                                      (headless)"
note "Pack:    make dist                                            (CF zip)"
note "Install: make install CF=/mnt/cf                              (direct CF copy)"
