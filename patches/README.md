# patches/

DOS-port patches against the five vendored upstream repos. Each subdirectory corresponds to a `<name>` in `vendor/sources.manifest`.

```
patches/
├── SDL/               0001-*.patch ...   → applied to vendor/SDL/
├── sdl2-compat/       0001-*.patch ...   → applied to vendor/sdl2-compat/
├── SDL_mixer/         0001-*.patch ...   → applied to vendor/SDL_mixer/
├── SDL_image/         0001-*.patch ...   → applied to vendor/SDL_image/
└── nxengine-evo/      0001-*.patch ...   → applied to vendor/nxengine-evo/
```

## Convention

- **Lexical order of application.** Use numeric prefixes `0001-`, `0002-`, ... so `ls`-ordered application is deterministic. `scripts/apply-patches.sh` uses `find ... -name '*.patch' | sort` to drive the order; don't fight it.
- **`git format-patch` output, not raw diffs.** `git am` consumes `format-patch` output, carrying the commit message and authorship across. Raw `diff` output can be applied with `git apply`, but we prefer `git am` because it gives every patch a named commit with a rationale.
- **Commit messages explain *why DOS needs this*, not *what the patch does*.** The diff already shows what changes; the commit message's subject + body should explain the underlying constraint. Examples:

  Good:
  > `[DOSKUTSU] Force software renderer; SDL3-DOS has no accelerated path`
  > `[DOSKUTSU] Drop JPEG find_package; Cave Story ships no .jpg assets`
  > `[DOSKUTSU] Lock window to 320x240 fullscreen at runtime`

  Bad:
  > `Change SDL_RENDERER_ACCELERATED to SDL_RENDERER_SOFTWARE`
  > `Remove JPEG dependency`
  > `Change window size`

- **One concern per patch.** Easier to review, easier to rebase, easier to send upstream. A Renderer.cpp change is one patch; CMakeLists.txt changes are separate patches.
- **Prefix subject lines with `[DOSKUTSU]`** so the patches are identifiable if we ever send them upstream.

## Creating a patch

Working from a clean vendor tree (`scripts/apply-patches.sh` resets to the pinned SHA before applying), make your change in the vendor directory, commit it with a proper message, then export:

```bash
# Example: fix a thing in NXEngine-evo
cd vendor/nxengine-evo

# Make your changes, then:
git add -p
git commit -m "[DOSKUTSU] Short subject explaining the DOS constraint

Longer body: which platform constraint forces this change, what the
symptom is without the patch, why this approach over alternatives."

# Export to the patches/ directory:
git format-patch -1 HEAD -o ../../patches/nxengine-evo/
```

Rename the generated file to fit the lexical-ordering scheme (next available `NNNN-` prefix under `patches/nxengine-evo/`). Commit the new patch file to the `doskutsu` repo.

## Refreshing against a new upstream SHA

When bumping a `vendor/sources.manifest` SHA to a newer upstream commit:

1. Update the SHA in the manifest.
2. `./scripts/fetch-sources.sh` to pull the new tree.
3. `./scripts/apply-patches.sh` — if all patches still apply cleanly, you're done.
4. If a patch fails: the script aborts and leaves `vendor/<name>/` in a conflict state. Either:
   - Fix the patch: resolve conflicts in the vendor tree, `git am --continue`, then `git format-patch` the updated commit back to `patches/<name>/` (overwriting the stale version).
   - Or revert the SHA bump if the upstream change isn't essential.

## Rerolling the entire patch stack

If the patches need wholesale revision (e.g., significant upstream drift):

```bash
cd vendor/nxengine-evo
git reset --hard <pinned-sha>
# Apply existing patches selectively via `git am --3way`, edit as needed,
# then re-export the whole series:
rm ../../patches/nxengine-evo/*.patch
git format-patch <pinned-sha>..HEAD -o ../../patches/nxengine-evo/
```

## License implications

Patches against GPLv3 upstreams (`nxengine-evo`) are **derivative works and therefore GPLv3**, regardless of this repo's MIT `LICENSE`. This is not a conflict: the MIT `LICENSE` correctly describes our original non-derivative source (Makefile, scripts, docs, port glue), and the patches inherit GPLv3 by operation of copyright law. See `THIRD-PARTY.md`.

Patches against zlib-licensed upstreams (`SDL`, `sdl2-compat`, `SDL_mixer`, `SDL_image`) are **derivatives of zlib-licensed code and therefore zlib-licensed**. More permissive than MIT; no conflict.
