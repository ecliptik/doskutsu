#!/usr/bin/env bash
# Scaffold a new port repository from templates/, wired to this repo's
# shared/ layer via a git submodule. See PORTING.md.
#
# Usage:
#   scripts/new-port.sh <name> [target-dir]
#
# <name>       lowercase-hyphenated, must match a candidate in ports.yaml
#              (or you're about to add one -- do that first).
# [target-dir] where to create the new repo. Defaults to ../<name>
#              (a sibling of this repo), matching the "each port is its
#              own repo" model.
#
# This script does not push anywhere. It sets up a local repo with the
# shared/ submodule already added and templates already filled in; review
# and push it yourself.

set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <name> [target-dir]" >&2
  exit 1
fi

NAME="$1"
HUB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_DIR="${2:-${HUB_DIR}/../${NAME}}"

if [ -e "$TARGET_DIR" ]; then
  echo "error: $TARGET_DIR already exists" >&2
  exit 1
fi

# Prefer this repo's own configured remote for the submodule URL, so a
# fork or a not-yet-pushed local clone still works; fall back to the known
# forgejo location otherwise.
HUB_REMOTE="$(git -C "$HUB_DIR" remote get-url origin 2>/dev/null || true)"
HUB_REMOTE="${HUB_REMOTE:-ssh://git@forgejo.ecliptik.com/ecliptik/sdl-dos-ports.git}"

echo "==> Creating $TARGET_DIR"
mkdir -p "$TARGET_DIR"
cd "$TARGET_DIR"
git init -q

echo "==> Adding sdl-dos-ports as a submodule at .sdl-dos-ports/ ($HUB_REMOTE)"
git submodule add -q "$HUB_REMOTE" .sdl-dos-ports || {
  echo "warning: submodule add failed (offline / remote not yet pushed?)." >&2
  echo "         Falling back to a local path submodule against $HUB_DIR." >&2
  git submodule add -q "$HUB_DIR" .sdl-dos-ports
}

echo "==> Creating directory skeleton"
mkdir -p vendor patches scripts tests qa-results setup .claude/agents .claude/skills

echo "==> Wiring shared SDL3-DOS patch series into patches/ (symlinks)"
ln -s ../.sdl-dos-ports/shared/patches/sdl3-dos patches/SDL
ln -s ../.sdl-dos-ports/shared/patches/sdl3-mixer patches/SDL_mixer

echo "==> Filling in templates"
NAME_UPPER="$(echo "$NAME" | tr '[:lower:]-' '[:upper:]_')"

sed -e "s/<NAME>/${NAME}/g" -e "s/<name>/${NAME}/g" \
  ".sdl-dos-ports/templates/PORT-README.md" > README.md
sed -e "s/<NAME>/${NAME}/g" -e "s/<name>/${NAME}/g" \
  ".sdl-dos-ports/templates/PORT-STATUS.md" > STATUS.md
sed -e "s/<NAME>/${NAME}/g" -e "s/<name>/${NAME}/g" \
  ".sdl-dos-ports/templates/PORT-PLAN.md" > PLAN.md
sed -e "s/<NAME>/${NAME}/g" -e "s/<name>/${NAME}/g" \
  ".sdl-dos-ports/templates/PORT-CLAUDE.md" > CLAUDE.md
sed -e "s/<NAME>/${NAME}/g" -e "s/<name>/${NAME}/g" \
  ".sdl-dos-ports/templates/LICENSE-REVIEW.md" > LICENSE-REVIEW.md
sed -e "s/<NAME>/${NAME_UPPER}/g" -e "s/<name>/${NAME}/g" \
  ".sdl-dos-ports/templates/vcctrl-profile.yaml.template" > "profiles/${NAME}.yaml"

cat > vendor/sources.manifest <<'EOF'
# vendor/sources.manifest -- pinned upstream sources for this port.
# Format: <name>  <url>  <ref>  <sha>
# See .sdl-dos-ports/docs/patch-conventions.md.
#
# This port vendors its OWN copy of SDL/SDL_mixer/SDL_image (shared/ only
# carries the DOS patch series, not the vendored source -- see
# .sdl-dos-ports/docs/architecture.md). Fill in real pinned SHAs for the
# three entries below (match whatever SHA the shared SDL3-DOS patch series
# in .sdl-dos-ports/shared/patches/sdl3-dos/ was built against -- check its
# README/patch headers), and add this port's own engine as a fourth entry.
SDL            https://github.com/libsdl-org/SDL.git          main          PIN_ME
SDL_mixer      https://github.com/libsdl-org/SDL_mixer.git    release-3.2.x PIN_ME
SDL_image      https://github.com/libsdl-org/SDL_image.git    release-3.2.x PIN_ME
EOF

echo "==> Done."
echo ""
echo "Next steps:"
echo "  1. Fill in README.md, STATUS.md, PLAN.md, LICENSE-REVIEW.md, and"
echo "     profiles/${NAME}.yaml (placeholders are marked <...>)."
echo "  2. Locate the canonical upstream repo and pin a revision in"
echo "     vendor/sources.manifest -- never guess."
echo "  3. Set this repo's own git remote and push when ready."
echo "  4. In the sdl-dos-ports hub repo, update ports.yaml: set"
echo "     dos_status: RESEARCH and port_repo_url for '${NAME}'."
