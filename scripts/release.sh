#!/bin/bash
# Create DOSKUTSU releases on GitHub with the CHANGELOG section as the body and
# the CF-ready bundle (doskutsu-<ver>.zip) as the asset.
#
# Git pushes go to Forgejo (origin), which mirrors to GitHub; the release
# object + asset are published on GitHub ONLY (the public face). Adapted from
# the sibling geomys/scripts/release.sh; see docs/RELEASING.md.
#
# Prerequisites:
#   - jq           sudo apt install jq
#   - gh           sudo apt install gh        (REQUIRED -- GitHub is the only
#                                             publish target; `gh auth login` first)
#   - zip + DJGPP toolchain + dosbox-x        (to run `make dist`)
#
# Usage:
#   ./scripts/release.sh v1.2.0           # release a specific tag
#   ./scripts/release.sh                  # release the latest vX.Y.Z tag
#   ./scripts/release.sh --hierarchical   # release every tag not yet on GitHub
#   ./scripts/release.sh --dry-run v1.2.0 # build the bundle + show what WOULD publish;
#                                         # NO gh calls, NO tag push, NO README
#                                         # write. Required for a real dry run --
#                                         # an authenticated `gh` would otherwise
#                                         # create a real GitHub release.

set -e

DRY_RUN=0
if [ "$1" = "--dry-run" ]; then DRY_RUN=1; shift; fi

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GITHUB_REPO="${GITHUB_REPO:-ecliptik/doskutsu}"
# Forge whose download URL the README "Latest" link points at.
README_DL_URL="${README_DL_URL:-https://github.com/$GITHUB_REPO}"

# Extract the CHANGELOG section for a version (no v prefix), header stripped.
extract_changelog() {
    local ver="$1"
    sed -n "/^## \[${ver}\]/,/^## \[/{/^## \[${ver}\]/d;/^## \[/d;p}" "$SCRIPT_DIR/CHANGELOG.md"
}

# Build dist/doskutsu-<ver>.zip from `make dist`.
build_bundle() {
    local ver="$1"
    echo "Building dist bundle for $ver..."
    make -C "$SCRIPT_DIR" dist
    cp "$SCRIPT_DIR/dist/doskutsu-cf.zip" "$SCRIPT_DIR/dist/doskutsu-${ver}.zip"
    echo "  -> dist/doskutsu-${ver}.zip ($(stat -c '%s' "$SCRIPT_DIR/dist/doskutsu-${ver}.zip") bytes)"
}

# Collect the release artifact(s) into RELEASE_FILES.
collect_artifacts() {
    local ver="$1"
    RELEASE_FILES=()
    local z="$SCRIPT_DIR/dist/doskutsu-${ver}.zip"
    [ -f "$z" ] && RELEASE_FILES+=("$z")
}

# Best-effort: push the tag to a named remote if it exists.
push_tag_to_remote() {
    local remote="$1" tag="$2"
    git -C "$SCRIPT_DIR" remote get-url "$remote" >/dev/null 2>&1 || return 0
    git -C "$SCRIPT_DIR" push "$remote" "$tag" 2>/dev/null || true
}

release_github() {
    local tag="$1" name="$2" body="$3"
    if ! command -v gh >/dev/null 2>&1; then
        echo "Error: gh CLI not installed -- GitHub is the only publish target" >&2
        return 1
    fi
    if ! gh auth status >/dev/null 2>&1; then
        echo "Error: gh not authenticated -- run 'gh auth login' first" >&2
        return 1
    fi
    if gh release view "$tag" --repo "$GITHUB_REPO" >/dev/null 2>&1; then
        echo "  GitHub release for $tag already exists, skipping"
        return 0
    fi
    echo "Creating GitHub release for $tag..."
    gh release create "$tag" --repo "$GITHUB_REPO" --title "$name" --notes "$body" \
        "${RELEASE_FILES[@]}"
    echo "  GitHub release complete: https://github.com/$GITHUB_REPO/releases/tag/$tag"
}

# Fill the README's LATEST-RELEASE marker block with the versioned download link.
update_readme_downloads() {
    local tag="$1" ver="${1#v}" readme="$SCRIPT_DIR/README.md"
    [ -f "$readme" ] || { echo "  README.md not found, skipping link update"; return 0; }
    if ! grep -q 'LATEST-RELEASE:START' "$readme"; then
        echo "  No LATEST-RELEASE markers in README, skipping link update"
        return 0
    fi
    local url="$README_DL_URL/releases/download/${tag}/doskutsu-${ver}.zip"
    local line="**Latest release:** [\`doskutsu-${ver}.zip\`](${url}) (${tag})"
    awk -v s='<!-- LATEST-RELEASE:START -->' -v e='<!-- LATEST-RELEASE:END -->' -v line="$line" '
        $0 ~ s {print; print line; skip=1; next}
        $0 ~ e {skip=0}
        !skip {print}
    ' "$readme" > "$readme.tmp" && mv "$readme.tmp" "$readme"
    echo "  Updated README latest-release link to $tag"
}

# Release one tag.
do_release() {
    local tag="$1" ver="${1#v}"
    echo "=== Releasing DOSKUTSU $tag ==="

    if ! git -C "$SCRIPT_DIR" tag -l "$tag" | grep -q "^$tag$"; then
        echo "Error: tag $tag does not exist"; return 1
    fi
    # Warn if the working tree is not the tagged commit (build reproducibility).
    if [ "$(git -C "$SCRIPT_DIR" rev-parse "$tag^{commit}")" != "$(git -C "$SCRIPT_DIR" rev-parse HEAD)" ]; then
        echo "  Warning: HEAD is not $tag -- 'git checkout $tag' first for a reproducible bundle."
    fi

    local body
    body=$(extract_changelog "$ver")
    [ -z "$body" ] && { echo "  Warning: no CHANGELOG entry for $ver"; body="Release DOSKUTSU $tag"; }
    body="${body}

---

### Download

\`doskutsu-${ver}.zip\` is the CF-ready bundle: \`DOSKUTSU.EXE\`, \`SETUP.EXE\`, the CWSDPMI DPMI host, license texts, and NXEngine-evo's GPLv3 engine support data. It does NOT include Cave Story game content -- extract that from your own 2004 freeware \`Doukutsu.exe\`: **[docs/ASSETS.md](https://github.com/$GITHUB_REPO/blob/${tag}/docs/ASSETS.md)** is the step-by-step extraction guide. The engine ships; the game data is yours to supply, like a Doom port and its WAD."

    if [ ! -f "$SCRIPT_DIR/dist/doskutsu-${ver}.zip" ]; then
        build_bundle "$ver"
    fi
    collect_artifacts "$ver"
    if [ ${#RELEASE_FILES[@]} -eq 0 ]; then
        echo "Error: no artifact dist/doskutsu-${ver}.zip"; return 1
    fi
    echo "  Artifact: $(basename "${RELEASE_FILES[0]}")"

    local name="DOSKUTSU $tag"
    if [ "$DRY_RUN" = 1 ]; then
        echo "  [dry-run] built $(basename "${RELEASE_FILES[0]}"); skipping ALL publish."
        echo "  [dry-run] would push the tag to origin (Forgejo mirrors it to GitHub),"
        echo "  [dry-run] create release '$name' on GitHub, upload the artifact, and"
        echo "  [dry-run] set the README latest link."
    else
        # The tag reaches GitHub via the Forgejo mirror; the direct github-remote
        # push is a best-effort shortcut (no-op if the remote isn't configured)
        # so `gh release create` never races the mirror sync.
        push_tag_to_remote origin "$tag"
        push_tag_to_remote github "$tag"
        release_github "$tag" "$name" "$body"
        update_readme_downloads "$tag"
    fi
    echo ""
}

github_release_exists() {
    command -v gh >/dev/null 2>&1 || return 1
    gh release view "$1" --repo "$GITHUB_REPO" >/dev/null 2>&1
}

# Main
if [ "$1" = "--hierarchical" ]; then
    echo "Checking for unreleased tags..."
    for tag in $(git -C "$SCRIPT_DIR" tag -l 'v*' --sort=version:refname); do
        if github_release_exists "$tag"; then
            echo "  $tag: already released on GitHub, skipping"
        else
            do_release "$tag"
        fi
    done
elif [ -n "$1" ]; then
    do_release "$1"
else
    tag=$(git -C "$SCRIPT_DIR" tag -l 'v*' --sort=version:refname | tail -1)
    [ -z "$tag" ] && { echo "Error: no vX.Y.Z tags found"; exit 1; }
    echo "No tag given; releasing latest: $tag"
    do_release "$tag"
fi
