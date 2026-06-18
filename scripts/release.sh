#!/bin/bash
# Create DOSKUTSU releases on Forgejo, Codeberg, and GitHub with the CHANGELOG
# section as the body and the CF bundle (doskutsu-cf-<ver>.zip) as the asset.
#
# Adapted from the sibling geomys/scripts/release.sh; see docs/RELEASING.md.
#
# Prerequisites:
#   - jq           sudo apt install jq
#   - gh           sudo apt install gh        (GitHub releases; optional)
#   - zip + DJGPP toolchain + dosbox-x        (to run `make dist`)
#   - FORGEJO_TOKEN   env var (Forgejo Settings > Applications)
#   - CODEBERG_TOKEN  env var (Codeberg Settings > Applications)
#   - gh auth login                           (for GitHub)
# A forge whose token/CLI is absent is warned-and-skipped, never fatal.
#
# Usage:
#   ./scripts/release.sh v1.2.0           # release a specific tag
#   ./scripts/release.sh                  # release the latest vX.Y.Z tag
#   ./scripts/release.sh --hierarchical   # release every tag not yet on all forges
#   ./scripts/release.sh --dry-run v1.2.0 # build the bundle + show what WOULD publish;
#                                         # NO forge API calls, NO tag push, NO README
#                                         # write. Use this to test: unsetting the Gitea
#                                         # tokens is NOT enough -- an authenticated `gh`
#                                         # would otherwise create a real GitHub release.

set -e

DRY_RUN=0
if [ "$1" = "--dry-run" ]; then DRY_RUN=1; shift; fi

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FORGEJO_URL="${FORGEJO_URL:-https://forgejo.ecliptik.com}"
FORGEJO_REPO="${FORGEJO_REPO:-ecliptik/doskutsu}"
CODEBERG_URL="${CODEBERG_URL:-https://codeberg.org}"
CODEBERG_REPO="${CODEBERG_REPO:-ecliptik/doskutsu}"
GITHUB_REPO="${GITHUB_REPO:-ecliptik/doskutsu}"
# Forge whose download URL the README "Latest" link points at.
README_DL_URL="${README_DL_URL:-$FORGEJO_URL/$FORGEJO_REPO}"

# Extract the CHANGELOG section for a version (no v prefix), header stripped.
extract_changelog() {
    local ver="$1"
    sed -n "/^## \[${ver}\]/,/^## \[/{/^## \[${ver}\]/d;/^## \[/d;p}" "$SCRIPT_DIR/CHANGELOG.md"
}

# Build dist/doskutsu-cf-<ver>.zip from `make dist`.
build_bundle() {
    local ver="$1"
    echo "Building dist bundle for $ver..."
    make -C "$SCRIPT_DIR" dist
    cp "$SCRIPT_DIR/dist/doskutsu-cf.zip" "$SCRIPT_DIR/dist/doskutsu-cf-${ver}.zip"
    echo "  -> dist/doskutsu-cf-${ver}.zip ($(stat -c '%s' "$SCRIPT_DIR/dist/doskutsu-cf-${ver}.zip") bytes)"
}

# Collect the release artifact(s) into RELEASE_FILES.
collect_artifacts() {
    local ver="$1"
    RELEASE_FILES=()
    local z="$SCRIPT_DIR/dist/doskutsu-cf-${ver}.zip"
    [ -f "$z" ] && RELEASE_FILES+=("$z")
}

# Best-effort: push the tag to a named remote if it exists.
push_tag_to_remote() {
    local remote="$1" tag="$2"
    git -C "$SCRIPT_DIR" remote get-url "$remote" >/dev/null 2>&1 || return 0
    git -C "$SCRIPT_DIR" push "$remote" "$tag" 2>/dev/null || true
}

# Create a release on a Gitea/Forgejo-API forge (Forgejo or Codeberg).
release_gitea_api() {
    local base_url="$1" repo="$2" token="$3" forge="$4" tag="$5" name="$6" body="$7"

    if [ -z "$token" ]; then
        echo "Warning: ${forge}_TOKEN not set, skipping $forge release"
        return 0
    fi

    local existing
    existing=$(curl -s -o /dev/null -w "%{http_code}" \
        "$base_url/api/v1/repos/$repo/releases/tags/$tag" \
        -H "Authorization: token $token")
    if [ "$existing" = "200" ]; then
        echo "  $forge release for $tag already exists, skipping"
        return 0
    fi

    echo "Creating $forge release for $tag..."
    local response release_id
    response=$(curl -s -X POST "$base_url/api/v1/repos/$repo/releases" \
        -H "Authorization: token $token" \
        -H "Content-Type: application/json" \
        -d "$(jq -n --arg tag "$tag" --arg name "$name" --arg body "$body" '{
            tag_name: $tag, name: $name, body: $body, draft: false, prerelease: false
        }')")
    release_id=$(echo "$response" | jq -r '.id // empty')
    if [ -z "$release_id" ]; then
        echo "Error creating $forge release: $response"
        return 1
    fi
    echo "  Created release ID: $release_id"

    local file filename
    for file in "${RELEASE_FILES[@]}"; do
        filename=$(basename "$file")
        echo "  Uploading $filename..."
        curl -s -X POST \
            "$base_url/api/v1/repos/$repo/releases/$release_id/assets?name=$filename" \
            -H "Authorization: token $token" \
            -H "Content-Type: application/octet-stream" \
            --data-binary @"$file" > /dev/null
    done
    echo "  $forge release complete: $base_url/$repo/releases/tag/$tag"
}

release_forgejo() {
    push_tag_to_remote origin "$1"
    release_gitea_api "$FORGEJO_URL" "$FORGEJO_REPO" "$FORGEJO_TOKEN" "Forgejo" "$@"
}

release_codeberg() {
    push_tag_to_remote codeberg "$1"
    release_gitea_api "$CODEBERG_URL" "$CODEBERG_REPO" "$CODEBERG_TOKEN" "Codeberg" "$@"
}

release_github() {
    local tag="$1" name="$2" body="$3"
    if ! command -v gh >/dev/null 2>&1; then
        echo "Warning: gh CLI not installed, skipping GitHub release"
        return 0
    fi
    if ! gh auth status >/dev/null 2>&1; then
        echo "Warning: gh not authenticated, skipping GitHub release"
        return 0
    fi
    if gh release view "$tag" --repo "$GITHUB_REPO" >/dev/null 2>&1; then
        echo "  GitHub release for $tag already exists, skipping"
        return 0
    fi
    echo "Creating GitHub release for $tag..."
    push_tag_to_remote github "$tag"
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
    local url="$README_DL_URL/releases/download/${tag}/doskutsu-cf-${ver}.zip"
    local line="**Latest release:** [\`doskutsu-cf-${ver}.zip\`](${url}) (${tag})"
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

\`doskutsu-cf-${ver}.zip\` is the CF-ready bundle: \`DOSKUTSU.EXE\`, \`SETUP.EXE\`, the CWSDPMI DPMI host, license texts, and NXEngine-evo's GPLv3 engine support data. It does NOT include Cave Story game content -- extract that from your own 2004 freeware \`Doukutsu.exe\` (see docs/ASSETS.md). The engine ships; the game data is yours to supply, like a Doom port and its WAD."

    if [ ! -f "$SCRIPT_DIR/dist/doskutsu-cf-${ver}.zip" ]; then
        build_bundle "$ver"
    fi
    collect_artifacts "$ver"
    if [ ${#RELEASE_FILES[@]} -eq 0 ]; then
        echo "Error: no artifact dist/doskutsu-cf-${ver}.zip"; return 1
    fi
    echo "  Artifact: $(basename "${RELEASE_FILES[0]}")"

    local name="DOSKUTSU $tag"
    if [ "$DRY_RUN" = 1 ]; then
        echo "  [dry-run] built $(basename "${RELEASE_FILES[0]}"); skipping ALL publish."
        echo "  [dry-run] would create release '$name' on Forgejo + Codeberg + GitHub,"
        echo "  [dry-run] upload the artifact, push the tag, and set the README latest link."
    else
        release_forgejo  "$tag" "$name" "$body"
        release_codeberg "$tag" "$name" "$body"
        release_github   "$tag" "$name" "$body"
        update_readme_downloads "$tag"
    fi
    echo ""
}

# Does a Gitea/Forgejo-API forge already have the release?
gitea_release_exists() {
    local base_url="$1" repo="$2" token="$3" tag="$4"
    [ -z "$token" ] && return 1
    [ "$(curl -s -o /dev/null -w "%{http_code}" \
        "$base_url/api/v1/repos/$repo/releases/tags/$tag" \
        -H "Authorization: token $token")" = "200" ]
}
github_release_exists() {
    command -v gh >/dev/null 2>&1 || return 1
    gh release view "$1" --repo "$GITHUB_REPO" >/dev/null 2>&1
}

# Main
if [ "$1" = "--hierarchical" ]; then
    echo "Checking for unreleased tags..."
    for tag in $(git -C "$SCRIPT_DIR" tag -l 'v*' --sort=version:refname); do
        f=$(gitea_release_exists "$FORGEJO_URL" "$FORGEJO_REPO" "$FORGEJO_TOKEN" "$tag" && echo yes || echo no)
        c=$(gitea_release_exists "$CODEBERG_URL" "$CODEBERG_REPO" "$CODEBERG_TOKEN" "$tag" && echo yes || echo no)
        g=$(github_release_exists "$tag" && echo yes || echo no)
        if [ "$f" = yes ] && [ "$c" = yes ] && [ "$g" = yes ]; then
            echo "  $tag: already released on all forges, skipping"
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
