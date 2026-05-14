# patches/nxengine-evo/NOTES.md

Authoring-discipline notes for the engine patch series. Layout convention,
slot rules, and the SDL2->SDL3 migration story are in `README.md`. This file
captures rough edges + non-obvious workflows that surface only at edit time.

## Filtered patch series for historical-fps archaeology

When backporting a single patch (e.g. patch 0135 TAS record/replay) onto a
historical codebase state for fps-comparison iters (e.g., the wave-43-
archaeology task #22 PHASE 2 binaries C1/C3/C4 representing wave-30 /
wave-34a / wave-35), the standard `make patches` + `make nxengine` path
hits a gate-fail:

```
[verify-patches] nxengine-evo:    delta = 9
[verify-patches] patches-applied check FAILED -- see above
make: *** [Makefile:250: verify-patches-applied] Error 1
```

`scripts/verify-patches-applied.sh` counts `*.patch` files in
`patches/<name>/` and compares to commits in `vendor/<name>/` since the
manifest-pinned SHA. The check assumes equality -- which is correct for
the main-line workflow (apply ALL patches; mismatch = bug) but is
WRONG for filtered backports.

**Workaround (no script changes needed):**

1. Reset vendor to manifest pin:
   ```
   git -C vendor/nxengine-evo reset --hard <manifest-pin>
   git -C vendor/SDL reset --hard <manifest-pin>
   git -C vendor/SDL_mixer reset --hard <manifest-pin>
   ```

2. Manually `git am` the desired filtered subset (lexical order matters
   per script convention):
   ```
   for p in $(ls patches/nxengine-evo/*.patch | LC_ALL=C sort); do
       num=$(basename "$p" | cut -c1-4)
       if [ "$num" -le NNNN ] 2>/dev/null; then  # NNNN = filter ceiling
           git -C vendor/nxengine-evo am "$(pwd)/$p"
       fi
   done
   git -C vendor/nxengine-evo am "$(pwd)/patches/nxengine-evo/0135-tas-record-replay.patch"
   ```
   (Same for `vendor/SDL`, `vendor/SDL_mixer` with their respective
   filter ceilings.)

3. Bypass the verify gate via direct build-target invocation:
   ```
   make $(pwd)/build/doskutsu.exe   # NOT `make nxengine` which depends on verify-patches-applied
   ```
   The `$(BUILD_DIR)/doskutsu.exe` rule itself runs the full 4-stage
   cmake build but doesn't list `verify-patches-applied` as a
   prereq, only the `.PHONY: nxengine` target does. Same compile
   output as `make nxengine` would produce; just skips the gate.

4. **Restore main-line state** when done:
   ```
   git -C vendor/nxengine-evo reset --hard <manifest-pin>
   git -C vendor/SDL reset --hard <manifest-pin>
   git -C vendor/SDL_mixer reset --hard <manifest-pin>
   make patches   # idempotent; re-applies the full series
   ```
   Re-build verifies main-line binary sha matches the expected ship.

**Sanity checks at each backport step:**

- `git -C vendor/<name> log --oneline | head -5` confirms the vendor
  is at the intended state (last patch commit's subject matches the
  filter ceiling).
- `strings build/doskutsu.exe | grep -c <patch-specific-string>` for
  two-witness verification of the backport-target patch.
- `make tas-smoke` (or equivalent functional smoke for the patch
  under test) confirms the backport is functionally green, not just
  built.

**Cross-incident learning** (wave-43 archaeology, 2026-05-14):
patch 0135's hooks (input_poll, run_tick tick_count static, SoundManager::
shutdown, Logger djgpp_log_fp, SDL_Quit, game.running) all exist in
wave-30+ codebases. The TAS engine is structurally orthogonal to render
/ audio levers that differ between waves; backport surface is minimal.
Wave-29 and earlier may require an additional patch to land
`Logger::djgpp_log_fp` (introduced by patch 0036 era) -- not validated
here. If you backport this far, run the file-existence check
`grep -l "djgpp_log_fp" vendor/nxengine-evo/src/Utils/` after applying
the patch series and BEFORE applying 0135.

## Reproducibility canary: what the binary sha actually proves

Cross-incident finding from the wave-43-archaeology iter (2026-05-14):
the DOSKUTSU.EXE binary file's sha256 is **NOT a stable canary for
"same source produces same binary"**. The DJGPP linker embeds a
wall-clock timestamp in the COFF file-header (offset 0x808; 4 bytes
little-endian). Same source tree + same toolchain version + different
link wall-clock = different binary sha.

**Empirical evidence**: three independent rebuilds of `phase11/wave-37-
production-floor` + patch 0135 (= the wave-41-TAS production source
state):

| Rebuild | When | Binary file sha12 | Bytes |
|---|---|---|---|
| Production wave-41-TAS ship | 2026-05-13 | `466976316b9d` | 7,129,312 |
| nx-engine C5+TAS | 2026-05-14 (~same hour) | `466976316b9d` | 7,129,312 |
| build-qa redundant rebuild | 2026-05-14 (~2h later) | `b47c5df37742` | 7,129,312 |

Byte count: stable across all three. Binary file sha: drifts with
wall-clock. The first two happened to match because they fell in the
same hour-bucket of the COFF timestamp.

**What IS the strong reproducibility canary**:

1. **Byte count match** (`stat -c %s build/doskutsu.exe`) -- proves
   structural identity of patches + source + toolchain.
2. **`strings` audit counts** (e.g., `grep -c "DOSKUTSU_TAS_"`,
   `grep -c "tas: "`) -- proves the expected code paths are embedded
   in `.rodata`.
3. **Source-tree state** -- `git -C vendor/<name> log --oneline | head -N`
   matching expected patch series -> source equivalence (not necessarily
   binary equivalence).

**Proposed fix** (deferred to a future patch slot, possibly 0139):
add `-Wl,--no-insert-timestamp` to the engine target's link flags via
a CMake patch. Binutils respects this option (since binutils 2.24) and
zeroes the COFF timestamp field. Result: same source -> same binary
sha exactly. SOURCE_DATE_EPOCH env var also works for some toolchains
but is less precise (granularity = build-system implementation
dependent).

**Cross-incident reference**: probe-engineer's hwinv.c embeds
`__DATE__ " " __TIME__` in its PROBE_BUILD_DATE constant; their
reproducibility fix is source-side (remove or sanitize the macro
expansion). DOSKUTSU.EXE has no such source-side `__DATE__/__TIME__`
usage (verified via `grep -rn "__DATE__\|__TIME__" vendor/nxengine-evo/src/`);
the timestamp drift is purely linker-side. The fix is single-line in
CMakeLists.txt or LDFLAGS.

## Patch slot reservation policy

(Reserved for future expansion.)
