#!/usr/bin/env bash
# check-changelog.sh -- mechanical CHANGELOG-presence ship-gate.
#
# WHY: a release entry was omitted from the release commit TWICE (v1.0.4
# da0be5a, v1.0.5 cba7d6c -- both patched after the fact with a trailing docs
# commit; see [[changelog_in_release_commit_discipline]]). This gate makes that
# failure LOUD instead of relying on memory. It is pure text + git: no build,
# no DOSBox, no display -- safe to run anywhere, anytime.
#
# CHANGELOG entries look like:   ## [1.0.5] - 2026-05-29
# git tags look like:            v1.0.5
# (the gate normalizes the leading 'v' on either side before comparing.)
#
# TWO MODES:
#
#   tests/check-changelog.sh <version>      (EXPLICIT -- the pre-tag gate)
#       Assert CHANGELOG.md has a "## [<version>]" entry for the exact version
#       about to be tagged. THIS is the release-procedure guard: run it with the
#       pending version IMMEDIATELY before `git tag`. <version> may be given with
#       or without a leading 'v' (v1.0.6 and 1.0.6 are equivalent).
#
#   tests/check-changelog.sh                (NO-ARG -- the standing gate)
#       "Never behind a shipped tag." Derive CHANGELOG's top entry version and
#       compare to the newest v* git tag. FAIL if the top entry is OLDER than the
#       latest tag -- that is exactly the omitted-entry signature (a tag exists
#       for a version the CHANGELOG never documented). PASS when the top entry is
#       equal to (steady state) or newer than (release in flight, entry written
#       but not yet tagged) the latest tag. This mode is wired into `make smoke`
#       so a post-hoc omission re-trips on the next build.
#
# Exit: 0 = pass; 1 = CHANGELOG missing/behind the required version;
#       2 = usage error / CHANGELOG.md not found / no parseable entries.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
CHANGELOG="$REPO/CHANGELOG.md"

case "${1:-}" in
  -h|--help)
    sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
esac

[[ -f "$CHANGELOG" ]] || { echo "[changelog-gate] FAIL: CHANGELOG.md not found at $CHANGELOG"; exit 2; }

# strip a single leading v/V from a version token
norm() { printf '%s' "${1#[vV]}"; }

# all "## [x.y.z]" versions, in file order (top-most first)
mapfile -t ENTRIES < <(grep -oE '^## \[[0-9]+\.[0-9]+\.[0-9]+\]' "$CHANGELOG" | sed -E 's/^## \[([0-9.]+)\]/\1/')
if [[ ${#ENTRIES[@]} -eq 0 ]]; then
  echo "[changelog-gate] FAIL: no '## [x.y.z]' version entries found in CHANGELOG.md"
  echo "[changelog-gate]   expected Keep-a-Changelog form, e.g.: ## [1.0.6] - 2026-06-01"
  exit 2
fi
TOP="${ENTRIES[0]}"

# ----- MODE A: explicit version must be present --------------------------------
if [[ -n "${1:-}" ]]; then
  WANT="$(norm "$1")"
  if [[ ! "$WANT" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "[changelog-gate] usage error: version '$1' is not x.y.z (with optional leading v)"
    exit 2
  fi
  for v in "${ENTRIES[@]}"; do
    if [[ "$v" == "$WANT" ]]; then
      echo "[changelog-gate] PASS: CHANGELOG.md has an entry for $WANT (## [$WANT])"
      exit 0
    fi
  done
  echo "[changelog-gate] FAIL: no '## [$WANT]' entry in CHANGELOG.md -- refusing the release."
  echo "[changelog-gate]   the version about to be tagged has no changelog entry."
  echo "[changelog-gate]   top entry is currently: ## [$TOP]"
  echo "[changelog-gate]   FIX: add a '## [$WANT] - <date>' section to CHANGELOG.md before tagging,"
  echo "[changelog-gate]        and include it in the RELEASE commit (not a trailing docs commit)."
  exit 1
fi

# ----- MODE B: never behind the newest shipped tag -----------------------------
LATEST_TAG="$(cd "$REPO" && git tag --list 'v*' --sort=-version:refname 2>/dev/null | head -1)"
if [[ -z "$LATEST_TAG" ]]; then
  echo "[changelog-gate] PASS: no v* git tags yet -- nothing to be behind (top entry ## [$TOP])"
  exit 0
fi
LATEST="$(norm "$LATEST_TAG")"

# newest of {top entry, latest tag} via version sort; if it's the tag and they
# differ, the CHANGELOG is behind a shipped release.
NEWEST="$(printf '%s\n%s\n' "$TOP" "$LATEST" | sort -V | tail -1)"
if [[ "$TOP" != "$LATEST" && "$NEWEST" == "$LATEST" ]]; then
  echo "[changelog-gate] FAIL: CHANGELOG is BEHIND a shipped tag."
  echo "[changelog-gate]   newest git tag : $LATEST_TAG"
  echo "[changelog-gate]   top CHANGELOG  : ## [$TOP]"
  echo "[changelog-gate]   a release was tagged whose version the CHANGELOG never documented"
  echo "[changelog-gate]   (the omitted-entry signature). FIX: add the ## [$LATEST] section."
  exit 1
fi

if [[ "$TOP" == "$LATEST" ]]; then
  echo "[changelog-gate] PASS: CHANGELOG top entry ## [$TOP] matches newest tag $LATEST_TAG (steady state)"
else
  echo "[changelog-gate] PASS: CHANGELOG top entry ## [$TOP] is ahead of newest tag $LATEST_TAG (release in flight)"
fi
exit 0
