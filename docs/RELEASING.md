# Releasing DOSKUTSU

How a DOSKUTSU release is built and published. Git pushes go to Forgejo
(`origin`), which mirrors to GitHub; the public release -- the release object,
notes, and zip asset -- is published on **GitHub only**. Modeled on the sibling
`geomys` project's `scripts/release.sh` flow, adapted for DOSKUTSU's single
asset-free bundle.

## What ships in a release

One artifact per release: **`doskutsu-<version>.zip`** -- exactly what `make dist`
produces (renamed with the version). It contains:

- `DOSKUTSU.EXE` -- the game (GPLv3; the dominant license of the binary)
- `SETUP.EXE` + `SETUP.BAT` -- the configurator
- `CWSDPMI.EXE` + `CWSDPMI.DOC` -- DPMI host (freeware, redistributable)
- `LICENSE.TXT` (MIT), `GPLV3.TXT`, `3RDPARTY.TXT`, `README.TXT`
- `DATA\` -- NXEngine-evo's GPLv3 engine support data ONLY (fonts, UI, StgMeta,
  endpic). NO Cave Story content.

It does NOT ship Cave Story game data (maps/sprites/music/SFX). Users extract that
from their own 2004 freeware `Doukutsu.exe` per `docs/ASSETS.md` -- the Doom/IWAD
posture. This is what makes publishing a binary release legally clean: nothing we
lack redistribution rights for is in the zip.

### Licensing / GPLv3 compliance

`DOSKUTSU.EXE` statically links NXEngine-evo (GPLv3), so publishing the binary
obligates us to make the corresponding source available. It is:

- The public repo IS the corresponding source.
- Each release MUST be cut from a tagged commit, and the release notes link back
  to that tag, so "the source this binary was built from" is unambiguous.
- The zip already carries `GPLV3.TXT` + `3RDPARTY.TXT`; `README.TXT` points to the
  repo. No extra step needed beyond tagging.

## Versioning

- Semantic version tags: `vMAJOR.MINOR.PATCH` (e.g. `v1.2.0`), already the convention.
- `CHANGELOG.md` carries one `## [MAJOR.MINOR.PATCH] - YYYY-MM-DD` section per
  version (Keep a Changelog format). The release body is that section, extracted
  verbatim.
- The release asset is named with the bare version (no `v`, no `-cf`):
  `doskutsu-1.2.0.zip`.

## Forges

| Forge | Role | Repo | Auth |
|---|---|---|---|
| Forgejo (`forgejo.ecliptik.com`) | primary git remote (`origin`); mirrors to GitHub; no releases published here | `ecliptik/doskutsu` | ssh (existing `origin`) |
| GitHub (`github.com`) | public face: mirror of `main` + tags, **the only release publish target** | `ecliptik/doskutsu` | `gh auth login` |

Tags reach GitHub through the Forgejo mirror. Optionally add a direct `github`
remote so a freshly-cut tag doesn't have to wait on the mirror sync before
`gh release create` can see it (the release script pushes to it best-effort if
it exists):

```bash
git remote add github git@github.com:ecliptik/doskutsu.git
```

## Release procedure

Prerequisites on the build host: `jq`, `gh` (authenticated -- GitHub is the only
publish target), `zip`, the DJGPP toolchain (for `make dist`), and `dosbox-x`
(for the pre-ship gates).

1. **Land all changes on `main`** and confirm the tree is clean.
2. **Pre-ship gates** (the same gates CI-less releases rely on):
   - `make distclean && make` -- clean four-stage build (avoids the stale-`.obj`
     trap; see `docs/BUILDING.md`).
   - `make smoke` -- parity-cycle DOSBox-X boot + gameplay banner gate.
   - `tests/run-gameplay-smoke.sh` -- runtime banner-emit gate.
   - `make setup-test` + `tests/run-setup-e2e.sh` -- SETUP config round-trip.
   - Confirm `CHANGELOG.md` has the new version's section (the release body source).
   - ASCII check on any changed tracked text (repo convention).
3. **Tag**: `git tag -a v1.2.0 -m "DOSKUTSU v1.2.0"` and push it:
   `git push origin v1.2.0` (Forgejo mirrors it to GitHub; `git push github v1.2.0`
   too if the direct remote is configured, to skip the mirror delay).
4. **Build the bundle**: `make dist` -> `dist/doskutsu-cf.zip`.
5. **Publish**: `./scripts/release.sh v1.2.0` (see below). It creates the GitHub
   release with the CHANGELOG section as the body, uploads the zip as the release
   asset, and updates the README download link.
6. **Verify** the GitHub download link resolves and the zip integrity (sha256)
   matches `dist/`.

## scripts/release.sh

Implemented at `scripts/release.sh` (adapted from `geomys/scripts/release.sh`). Run it
with the tag: `./scripts/release.sh v1.2.0` (or no arg for the latest tag, or
`--hierarchical` to backfill every tag not yet released on GitHub).

**Test first with `--dry-run`**: `./scripts/release.sh --dry-run v1.2.0` builds the
bundle and prints what WOULD publish, touching no forge and not writing the README. A
real dry run REQUIRES this flag -- an authenticated `gh` will create a real GitHub
release otherwise.

What a real run does:

- **Repo var**: `GITHUB_REPO=ecliptik/doskutsu` (overridable via env, as is the
  README link base `README_DL_URL`).
- **Version source**: take the tag from `$1`, or default to the latest
  `git tag -l 'v*' --sort=version:refname | tail -1`.
- **`extract_changelog`**: the release body is the tag's `## [ver] - date` CHANGELOG
  section, extracted verbatim.
- **`build_bundle` / `collect_artifacts`**: run `make dist`, then copy
  `dist/doskutsu-cf.zip` -> `dist/doskutsu-<ver>.zip` (the upload name); that zip
  is the single release asset. The release body is the CHANGELOG section plus a
  Download blurb linking the tag-pinned `docs/ASSETS.md` extraction guide.
- **Tag push**: push the tag to `origin` (Forgejo, which mirrors to GitHub) and
  best-effort to a `github` remote if one is configured.
- **`release_github`**: idempotent create-or-skip via `gh release create` + asset
  upload. `gh` missing or unauthenticated is a hard error -- there is no other
  publish target to fall back to.
- **`update_readme_downloads`**: fill the README `LATEST-RELEASE` marker block with
  the `releases/download/v<ver>/doskutsu-<ver>.zip` link for the new tag
  (idempotent rewrite).
- **`--hierarchical`**: backfill every `v*` tag that has no GitHub release yet.

If you'd rather not use the script, a release can be cut by hand: `make dist`, rename
the zip to `doskutsu-<ver>.zip`, and create the release + upload the asset through
each forge's web UI, pasting the CHANGELOG section as the body.

## Notes

- No CI. Releases are cut manually from a maintainer's host (same as geomys). A
  future Forgejo Actions / Woodpecker pipeline could automate steps 2+4+5 on tag push.
- The Organya PCM cache (`make org-cache`) and any Cave Story data are NEVER part of
  a release -- they are operator-local / user-supplied derived artifacts (see
  `docs/BUILDING.md` + `docs/ASSETS.md`).
