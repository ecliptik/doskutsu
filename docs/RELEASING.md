# Releasing DOSKUTSU

How a DOSKUTSU release is built and published to Forgejo, Codeberg, and GitHub.
Modeled on the sibling `geomys` project's `scripts/release.sh` flow, adapted for
DOSKUTSU's single asset-free bundle.

## What ships in a release

One artifact per release: **`doskutsu-cf-<version>.zip`** -- exactly what `make dist`
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
- The release asset is named with the bare version (no `v`): `doskutsu-cf-1.2.0.zip`.

## Forges

| Forge | Role | Repo | Auth |
|---|---|---|---|
| Forgejo (`forgejo.ecliptik.com`) | primary (`origin`) | `ecliptik/doskutsu` | `FORGEJO_TOKEN` (Settings > Applications) |
| Codeberg (`codeberg.org`) | mirror | `ecliptik/doskutsu` | `CODEBERG_TOKEN` (Settings > Applications) |
| GitHub | mirror | `ecliptik/doskutsu` | `gh auth login` |

Codeberg + GitHub are not yet configured as git remotes. One-time setup:

```bash
git remote add codeberg ssh://git@codeberg.org/ecliptik/doskutsu.git
git remote add github   git@github.com:ecliptik/doskutsu.git
# create the empty repos on each forge first; push main + tags:
for r in codeberg github; do git push "$r" main --tags; done
```

Both Forgejo and Codeberg speak the same Gitea/Forgejo REST API, so the release
calls are identical bar the base URL + token. GitHub uses the `gh` CLI.

## Release procedure

Prerequisites on the build host: `jq`, `gh` (for GitHub), `zip`, the DJGPP toolchain
(for `make dist`), `dosbox-x` (for the pre-ship gates), and the tokens above exported.

1. **Land all changes on `main`** and confirm the tree is clean.
2. **Pre-ship gates** (the same gates CI-less releases rely on):
   - `make distclean && make` -- clean four-stage build (avoids the stale-`.obj`
     trap; see `docs/BUILDING.md`).
   - `make smoke` -- parity-cycle DOSBox-X boot + gameplay banner gate.
   - `tests/run-gameplay-smoke.sh` -- runtime banner-emit gate.
   - `make setup-test` + `tests/run-setup-e2e.sh` -- SETUP config round-trip.
   - Confirm `CHANGELOG.md` has the new version's section (the release body source).
   - ASCII check on any changed tracked text (repo convention).
3. **Tag**: `git tag -a v1.2.0 -m "DOSKUTSU v1.2.0"` and push to all forges:
   `git push origin v1.2.0 && git push codeberg v1.2.0 && git push github v1.2.0`.
4. **Build the bundle**: `make dist` -> `dist/doskutsu-cf.zip`.
5. **Publish**: `./scripts/release.sh v1.2.0` (see below). It uploads the zip as a
   release asset on all three forges with the CHANGELOG section as the body, and
   updates the README download link.
6. **Verify** the download link on each forge resolves and the zip integrity (sha256)
   matches `dist/`.

## scripts/release.sh

Implemented at `scripts/release.sh` (adapted from `geomys/scripts/release.sh`). Run it
with the tag: `./scripts/release.sh v1.2.0` (or no arg for the latest tag, or
`--hierarchical` to backfill every tag not yet on all forges).

**Test first with `--dry-run`**: `./scripts/release.sh --dry-run v1.2.0` builds the
bundle and prints what WOULD publish, touching no forge and not writing the README. A
real dry run REQUIRES this flag -- unsetting `FORGEJO_TOKEN` / `CODEBERG_TOKEN` is NOT
enough, because an authenticated `gh` will create a GitHub release regardless.

What a real run does:

- **Repo/URL vars**: `FORGEJO_REPO=ecliptik/doskutsu`, `CODEBERG_REPO=ecliptik/doskutsu`,
  `GITHUB_REPO=ecliptik/doskutsu`. Keep geomys's `FORGEJO_URL` / `CODEBERG_URL` defaults.
- **Version source**: DOSKUTSU has no CMakeLists version; take the tag from `$1`
  (require it), or default to the latest `git tag -l 'v*' --sort=version:refname | tail -1`.
- **`extract_changelog`**: reuse geomys's verbatim -- DOSKUTSU's `## [ver] - date`
  headings match its `## [ver]` sed range.
- **`collect_artifacts`**: replace geomys's 9-file preset matrix with the single
  `doskutsu-cf-<ver>.zip`. Build it by running `make dist` then copying
  `dist/doskutsu-cf.zip` -> `dist/doskutsu-cf-<ver>.zip` (the upload name).
- **`build_all_presets`** -> a `build_bundle` that runs `make dist` (no presets).
- **`release_forgejo` / `release_codeberg` / `release_github`**: reuse verbatim
  (idempotent create-or-skip + asset upload).
- **`update_readme_downloads`**: point the README `## Download` section's
  `releases/download/v<ver>/doskutsu-cf-<ver>.zip` link at the new tag (sed rewrite,
  same idempotent pattern as geomys).
- **`--hierarchical`**: reuse for backfilling older tags (v1.0.x .. v1.2.0) once the
  forges are set up.
- Tokens absent -> warn-and-skip that forge (geomys's posture); never hard-fail the
  whole run because one forge is unconfigured.

If you'd rather not use the script, a release can be cut by hand: `make dist`, rename
the zip to `doskutsu-cf-<ver>.zip`, and create the release + upload the asset through
each forge's web UI, pasting the CHANGELOG section as the body.

## Notes

- No CI. Releases are cut manually from a maintainer's host (same as geomys). A
  future Forgejo Actions / Woodpecker pipeline could automate steps 2+4+5 on tag push.
- The Organya PCM cache (`make org-cache`) and any Cave Story data are NEVER part of
  a release -- they are operator-local / user-supplied derived artifacts (see
  `docs/BUILDING.md` + `docs/ASSETS.md`).
