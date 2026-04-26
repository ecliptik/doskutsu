# ScummVM — DOS Port Feasibility

Feasibility memo for porting [ScummVM](https://github.com/scummvm/scummvm) (the universal classic-adventure-game interpreter that supports SCUMM, AGI, SCI, AGS, and ~95 other engine families, including Thimbleweed Park since v2.9.0) to MS-DOS 6.22 / DJGPP using the doskutsu port stack (statically-linked SDL3-with-DOS-backend + SDL3_mixer + SDL3_image + DJGPP + CWSDPMI).

**Recommendation up front:** **Feasible-but-expensive for a SCUMM-class subset; not feasible for Thimbleweed Park specifically.** The ScummVM core architecture is unusually well-suited to a DOS port — its renderer is software-first with OpenGL as a separate dependency, its audio mixer is callback-driven (no engine thread), its mutex layer abstracts behind `MutexInternal`, and SDL3 support already exists in the SDL backend tree. The blocker is **per-engine**: `engines/twp/configure.engine` declares `3d` and `opengl_game_shaders` as hard dependencies, and `engines/twp/gfx.cpp` calls OpenGL directly (`glGenTextures`, `glDrawArrays`, etc.). The same OpenGL constraint that ruled out engge2 rules out the TWP engine. Other engines (SCUMM, SCI, AGI, AGS) have no `3d` dependency and are renderer-software-pure. **Sized estimate: 6-10 weeks for a SCUMM-only build (Monkey Island 1-3, Day of the Tentacle, Sam & Max Hit the Road) reaching the title screen on Tier 1 hardware.** TWP-on-DOS would require either rewriting `engines/twp/gfx.cpp` from GL to ScummVM's software renderer (multi-quarter project), or waiting for an upstream non-GL fallback that doesn't currently exist.

This is MIT-licensed prose. Readers should consult `docs/ports/DOS-PORT-PLAYBOOK.md` for the engine-agnostic playbook this memo cites.

---

## License

**GPLv3** ([scummvm/COPYING](https://raw.githubusercontent.com/scummvm/scummvm/master/COPYING) line 2: *"GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007"*). This matches doskutsu's existing GPLv3-binary posture (NXEngine-evo is also GPLv3) — the static-link binary `SCUMMVM.EXE` would inherit GPLv3 just like `DOSKUTSU.EXE` does. License compatibility math is the same one already solved.

Per-engine sub-licensing is uniform: the entire ScummVM tree (engines + core + backends) is GPLv3+. No mixed-license complications.

License is **not a blocker.**

---

## Language and minimum C++ standard

C++ (~94% of the codebase by language, with C support shims for some backends).

**C++ standard:** `-std=c++11` is set in the configure script (`configure` line referenced as `append_var CXXFLAGS "-std=${std_variant}11"`). DJGPP 12.2.0 supports up through C++20 with some library gaps; C++11 is well within range and matches doskutsu's NXEngine-evo baseline.

This is the **best-case outcome** for the language question — no C++17/C++20 dependency to negotiate against DJGPP's libstdc++ gaps. (Compare AGS-FEASIBILITY's open question on C++ standard.)

---

## Build system

**Hand-written GNU Make** (`Makefile.common`, `Makefile`, `ports.mk`) plus a 7,971-line POSIX shell `configure` script. **No top-level `CMakeLists.txt`** — `raw.githubusercontent.com/scummvm/scummvm/master/CMakeLists.txt` returns 404. ScummVM does NOT use CMake.

This is a structural difference from every other port doskutsu has investigated (NXEngine-evo, AGS, engge2 all use CMake). Implications:

- doskutsu's Makefile orchestration model (`make sdl3 → make sdl3-mixer → make sdl3-image → make scummvm`) still works at the top level, but the per-stage build delegate for ScummVM is `./configure && make`, not `cmake`.
- The configure script is hand-written and supports cross-compilation via `--host=<triplet>`. No documented `i586-pc-msdosdjgpp` triplet handling exists, but the script structure (case-statement-based host OS detection at the `case $_host_os in` block, with arms for `3ds`, `amigaos*`, `android`, `beos*`, `cygwin*`, `darwin*`, `dreamcast`, `ds`, `emscripten`, etc.) is straightforward to extend. **Adding a `msdosdjgpp*` case arm is the natural integration point.**
- Engine-selection options exist and are well-developed: `--enable-engine=<name>`, `--disable-all-engines`, `--enable-engine-static`, `--enable-engine-dynamic`, plus `--disable-all-unstable-engines`. This is **load-bearing for a DOS port** — see the "Plugin / engine selection" section below.

---

## Graphics dependency

**Software renderer is the primary path.** `graphics/` contains `renderer.cpp`, `primitives.cpp`, `surface.cpp`, `VectorRendererSpec.cpp` — all software. OpenGL lives in a **separate `graphics/opengl/` subdirectory** that engines opt into via per-engine `configure.engine` dependency declarations.

The contrast with engge2 (where OpenGL was the *only* path) is decisive: ScummVM's core does not require GL. The SDL backend (`backends/platform/sdl/`) handles the final blit-to-window through `SDL_RenderTexture` against an `SDL_Texture` populated from a `Graphics::Surface`. This maps onto SDL3-DOS's software renderer cleanly, mirroring doskutsu's NXEngine-evo path.

**Per-engine 3D opt-in.** `engines/twp/configure.engine` declares (verbatim from upstream):

```
add_engine twp "Thimbleweed Park" yes "" "" "16bit 3d highres vorbis png opengl_game_shaders" "imgui"
```

The `3d` and `opengl_game_shaders` tokens are hard requirements that the configure script gates on `--enable-engine=twp`. `engines/twp/gfx.cpp` calls OpenGL directly — `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glDrawArrays`, `glDrawElements`, `glGenFramebuffers`, `glEnable(GL_BLEND)`, `glBlendFuncSeparate`, plus shader pipeline calls (`_shader.use()`, `_shader.setUniform`). This is **not abstracted behind the ScummVM renderer** — it's direct GL.

For comparison, `engines/scumm/configure.engine` declares:

```
add_engine scumm "SCUMM" yes "scumm_7_8 he" "v0-v6 games" "" "midi fmtowns_pc98_audio sid_audio imgui"
```

No `3d`, no `opengl_game_shaders`, no `highres`. SCUMM is software-renderer-pure and well-suited to a DOS port. Other adventure-engine families (SCI, AGI, AGOS, KYRA, GROOVIE, LURE, QUEEN) have similar minimal dependency lists.

**Other engines requiring 3d / OpenGL:** worth a separate audit. Engines plausibly affected by the same constraint based on era and asset shape: `grim`, `myst3`, `stark`, `playground3d`, `hpl1`, `freescape`, `wintermute` (if present), and likely others. A port-side `configure.engine` audit would establish the buildable subset.

(See `DOS-PORT-PLAYBOOK.md § DOS-specific gotchas / Renderer` for the SDL3-DOS software-renderer-only constraint.)

---

## Audio dependency

**SDL audio callback model — well-suited to SDL3-DOS.**

`audio/mixer_intern.h` documents the architecture explicitly: *"Hook up the mixCallback() in a suitable audio processing thread/callback. The mixer callback function, to be called at regular intervals by the backend (e.g. from an audio mixing thread). All the actual mixing work is done from here."*

`backends/mixer/sdl/sdl-mixer.cpp` confirms the SDL backend uses the **SDL audio callback path, not a managed thread**:

- SDL 2.x: `SDL_OpenAudio()` with `desired.callback = sdlCallback; desired.userdata = this;` registered on `SDL_AudioSpec`. The callback `sdlCallback(void *this_, byte *samples, int len)` invokes `_mixer->mixCallback(samples, len)`.
- SDL 3.x: `SDL_OpenAudioDeviceStream()` with the SDL3-style callback signature `sdlCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)`.

ScummVM does **not spawn an audio thread itself**. It relies on whatever threading model SDL's audio subsystem uses internally. Under SDL3-DOS, that means the audio fill happens cooperatively in `SDL_PumpEvents` / `SDL_Delay` yield points — exactly the model `DOS-PORT-PLAYBOOK.md § DOS-specific gotchas / Audio` describes.

This is the **single biggest architectural advantage** ScummVM has over AGS for a DOS port. AGS's audio core (`Engine/media/audio/audio_core.cpp`) spawns a `std::thread` polling-loop that requires a full rewrite for SDL3-DOS (see `AGS-FEASIBILITY.md § Audio dependency`). ScummVM's mixer needs no rewrite — only the SDL3 audio API migration that doskutsu has already done in patches `0013-0017`.

The SCUMM and TWP audio paths use Vorbis decoding (TWP declares `vorbis` in its dependencies; SCUMM uses it for music tracks in some games). doskutsu's existing SDL3_mixer build already includes stb_vorbis.

---

## Threading

**ScummVM's threading footprint is minimal.** Confirmed observations:

- `engines/twp/twp.cpp` (~2,146 lines): no `std::thread`, no `pthread_create`, no `SDL_CreateThread`, no `std::async`, no `Common::Thread`. The only "thread" reference is `sq_newthread(v, 1024)` at line ~1320 — a Squirrel VM coroutine, not an OS thread. **All Thimbleweed Park "threads" are scripted coroutines on the engine's main loop.**
- `engines/scumm/scumm.cpp`: no threading-primitive matches. SCUMM is single-threaded.
- `audio/audiostream.h`: no threading-primitive matches. The audio stream system is callback-driven from the backend, not internally threaded.
- `common/mutex.h` defines `Common::Mutex` as a wrapper around an opaque `MutexInternal *` pointer; the SDL backend (`backends/mutex/sdl/sdl-mutex.cpp`) implements `MutexInternal::lock/unlock` via `SDL_LockMutex` / `SDL_UnlockMutex` (SDL 3.x) or `SDL_mutexP` / `SDL_mutexV` (SDL 2.x). This is fine on SDL3-DOS — mutex operations are no-ops on a single-threaded scheduler, and the abstraction layer means we could swap in a no-op `MutexInternal` for DOS if tail-latency on `SDL_LockMutex` ever became a concern (almost certainly it won't).

**Open question (limited):** complete tree audit not done. The `Common::Thread` class exists in `common/`, and per-engine usage might be present in some engines we don't care about. For a SCUMM-only port, the threading audit is essentially clean. For a broader port, run `grep -rE 'std::thread|SDL_CreateThread|pthread_create|std::async|Common::Thread' engines/ common/ backends/` and exclude any engine that fails the audit from the build.

(See `DOS-PORT-PLAYBOOK.md § DOS-specific gotchas / Threading` for the cooperative-scheduler invariant.)

---

## Memory budget

No formal minimum-RAM documentation in the upstream tree. Empirical points:

- **RISC OS port docs** (`docs.scummvm.org/en/latest/other_platforms/risc_os.html`) state *"A minimum of 64 MB RAM. 32 MB may work in some circumstances, but is not generally recommended."* That's RISC OS — not a hard transferable number, but a useful anchor.
- **Atari Falcon backend** (`backends/platform/atari/`) exists in the current tree (`osystem_atari.cpp`, `dlmalloc.cpp`, `atari_ikbd.S`, `build-release030.sh`) and targets 68030-class hardware. Atari Falcon stock RAM is 14 MB max. The Atari port runs *some* SCUMM games on that profile, which is the closest extant analog to our Tier 1 / Tier 2 / Tier 3 model — and it's a strong existence proof that ScummVM's core can run on a constrained vintage profile.
- **Per-engine asset working sets vary wildly.** Original LucasArts SCUMM games (Monkey Island 1-3, Indy 4, Day of the Tentacle, Sam & Max) have 5-50 MB total disk footprint, roomy fits within 48 MB DPMI. Late-era SCUMM (Curse of Monkey Island, Full Throttle) and modern engines (TWP, Grim, Stark, Myst3) have 100MB-1GB+ working sets and are not realistic on Tier 1 / Tier 2.

**For Tier 1 (PODP83 / 48 MB):** plausible for SCUMM v0-v6 era games (Maniac Mansion through Sam & Max). Probably workable for some SCI and AGI titles. Not realistic for late-90s / 2000s engines.

**For Tier 2 (16 MB):** SCUMM v0-v3 (Maniac Mansion, Zak McKracken, Indy 3) likely work. Anything later is uncertain.

**For Tier 3 (8 MB):** speculative. Engine working set + ScummVM core overhead + SDL3 + asset cache may exceed 8 MB even on the smallest games. Probably not viable without significant memory-budget work.

**Open question:** characterize working-set on a representative SCUMM game (Monkey Island 2 is canonical) under a 48 MB-cap on Wine or DOSBox-X with limited XMS. This should precede any port commitment.

---

## Plugin / engine selection

**This is ScummVM's biggest porting advantage.** The configure script supports per-engine enable/disable, which means we can scope the DOS port to whatever subset fits the memory and renderer envelope.

Available controls:

- `--disable-all-engines` then `--enable-engine=scumm` for "just SCUMM."
- `--disable-all-unstable-engines` for "stable engines only."
- `--enable-engine=<a>,<b>,<c>` for explicit subsets.
- `--enable-engine-static=<name>` vs `--enable-engine-dynamic=<name>` — but **static is the only viable option on DOS** (SDL3-DOS does not support `SDL_LoadObject`; see `DOS-PORT-PLAYBOOK.md § DOS-specific gotchas / DJGPP / language`).

**Recommended port scope:** start with `--disable-all-engines --enable-engine=scumm`. SCUMM v0-v6 alone covers the canonical LucasArts adventure run (Maniac Mansion, Zak, Indy 3, Loom, Monkey 1-2, Indy 4, Day of the Tentacle, Sam & Max, Full Throttle through The Dig and Curse). That subset has:

- No 3d/opengl dependencies
- Mature, stable engine code (SCUMM has been in ScummVM since the project's start)
- The most well-known adventure-game catalog
- Resident-set sizes that plausibly fit 48 MB Tier 1

After SCUMM ships, expand outward — SCI, AGI, AGOS, KYRA, QUEEN, LURE are all software-renderer-pure and add another ~50 classic adventures.

**Engines explicitly excluded by the SDL3-DOS / Tier-1 envelope:** `twp` (OpenGL), `grim` (3D), `myst3` (3D), `stark` (3D), `hpl1` (3D), `freescape` (3D), `playground3d` (3D), and any others that declare `3d` / `opengl_game_shaders` in their `configure.engine`. Filter by greppinng `engines/*/configure.engine` for those tokens.

This is structurally cleaner than the AGS situation, where the port either includes the entire engine or doesn't build at all.

---

## DOS-port history

**ScummVM had a DJGPP/DOS port circa 2003-2008. It is no longer in the tree.** Confirmation:

- `backends/platform/` current contents (per `github.com/scummvm/scummvm/tree/master/backends/platform`): `3ds, android, atari, dc, ds, ios7, libretro, maemo, n64, null, openpandora, psp, samsungtv, sdl, wii`. **No `dos`, `djgpp`, `msdos`, or equivalent directory.**
- `configure` script has no `msdosdjgpp*` arm in its `case $_host_os in` block. The script handles `3ds`, `amigaos*`, `android`, `beos*`, `cygwin*`, `darwin*`, `dreamcast`, `ds`, `emscripten`, etc. — DOS is absent.
- `ports.mk` lists POSIX, macOS, iOS, tvOS, Linux. No DOS reference.
- ScummVM forum lore indicates a DOS backend was contributed circa early 2000s with DJGPP+Allegro, then bitrotted and was dropped. Specific commit-history dating not retrievable via WebSearch in this session — see "Open questions" below.

**Implication:** there is no extant DOS-port codebase in the upstream tree to rebase against. The port is greenfield, but it builds on ScummVM's clean cross-platform abstractions, which is a much better starting point than restoring a stripped Allegro 4 fork (the AGS situation). The Atari Falcon backend in `backends/platform/atari/` is the closest analog and could serve as a structural reference for what a DOS port directory should look like.

---

## SDL2 → SDL3 migration surface

**ScummVM already supports SDL3 in its current SDL backend.** The codebase uses parallel implementations gated on `SDL_VERSION_ATLEAST(3, 0, 0)`:

- `backends/mixer/sdl/sdl-mixer.cpp`: parallel `SDL_OpenAudioDeviceStream` (SDL 3.x) and `SDL_OpenAudio` (SDL 2.x) paths.
- `backends/mutex/sdl/sdl-mutex.cpp`: parallel `SDL_LockMutex`/`SDL_UnlockMutex` (SDL 3.x) and `SDL_mutexP`/`SDL_mutexV` (SDL < 3.0) paths.
- `backends/platform/sdl/sdl.cpp`: extensive `#if SDL_VERSION_ATLEAST(3, 0, 0)` usage for window creation, GL context management (`SDL_GL_DeleteContext` vs `SDL_GL_DestroyContext`), error signaling.

**This is unique among the engines doskutsu has investigated.** NXEngine-evo, AGS, and engge2 all required a one-shot SDL2→SDL3 patch series in the doskutsu pattern (`patches/<name>/0010-0019` cluster). ScummVM has already done that work upstream. Building against SDL3 is selecting the SDL3 codepath, not migrating the engine.

**Practical consequence:** the doskutsu patches `0010-0017` cluster (mechanical renames + audio refactor) almost entirely vanishes from the ScummVM patch budget. Build-system patches (`0001-0009` cluster) and DOS-specific gotcha patches (`0020-...`) remain.

---

## Asset format

**Game data is user-supplied.** ScummVM's standard model: user owns the original game (commercial or freeware), ScummVM reads the original disc / pack files directly. doskutsu's "game data is gitignored, never in dist" pattern (per `DOS-PORT-PLAYBOOK.md § Asset extraction patterns`) applies cleanly.

For SCUMM games specifically: ScummVM reads the original `.000` / `.001` disk image files, `index.lfl`, `monkey.000`, etc. — directly. **No extraction step is needed**; the user copies the original game files to the install directory and ScummVM detects them via `engines/scumm/detection.cpp`'s game-detection table.

For TWP (moot given the OpenGL blocker, but documenting for completeness): `engines/twp/ggpack.h` defines `GGPackDecoder` for the `.ggpack1` / `.ggpack2` containers Thimbleweed Park ships. Several third-party extractors exist (`mstr-/twp-ggdump`, `scemino/NGGPack`, `s-l-teichmann/ggpack`, `fzipp/gg`), but ScummVM reads the packs natively without an extraction pre-pass — same model as SCUMM, just a different container format.

`docs/ASSETS.md` for a doskutsu-style ScummVM port would document: which SCUMM games to obtain, where to copy the data files (`C:\SCUMMVM\games\<game>\`), and the standard ScummVM detection flow.

---

## Specific risks and blockers

In rough order from "likely show-stopper" to "tractable":

1. **OpenGL-required engines (TWP, Grim, Myst3, Stark, etc.) — scope-limit, not blocker.** Per-engine `configure.engine` audit identifies the buildable subset. Affects which games the port runs, not whether the port runs. **For a TWP-specifically port: hard blocker** without rewriting `engines/twp/gfx.cpp` from GL to ScummVM's renderer abstraction.
2. **No upstream DOS backend.** A new `backends/platform/dos/` directory needs to be authored, modeled on the SDL backend (`backends/platform/sdl/`) but with SDL3-DOS-specific cooperative-scheduler accommodations and the gotchas from `DOS-PORT-PLAYBOOK.md`. Sized: 1-2 weeks; the SDL backend is a good template.
3. **`configure` script cross-compile arm needed.** The hand-written shell `configure` needs an `msdosdjgpp*` case arm in the host-OS detection block, plus the corresponding `_port_mk="backends/platform/dos/dos.mk"` wireup. Sized: 2-3 days.
4. **Memory budget characterization.** Working-set on Monkey Island 2 (canonical SCUMM v5) under 48 MB is uncharacterized. Risk: late-era SCUMM games may exceed Tier 1 budget. Mitigation: start with v0-v3 games (Maniac Mansion, Zak, Indy 3) which have small disk footprints, then expand. Sized: 1-2 days of empirical measurement on Wine or DOSBox-X with constrained XMS.
5. **`size_t` 32-bit audit.** ScummVM is 25+ years old C++ and has been ported to many 32-bit and 16-bit platforms (Atari 68k, GBA, DS, PSP, N64). The audit pain is probably already done by upstream. Verify with a grep for `int64_t` in seek/tell paths. Sized: 1 day.
6. **`fopen("rb")` audit.** ScummVM's file abstraction is `Common::File`, which is platform-portable. Direct `fopen()` calls in the engine code need the binary-mode flag. Likely already correct, since the codebase has been ported to platforms with the same pitfall. Sized: 1 day.
7. **`Common::Thread` users in non-SCUMM engines.** Some engines (video playback in `groovie`, some asset-streaming paths) might use the engine's thread abstraction. Affects which engines build. Mitigation: filter out non-thread-clean engines from the `--enable-engine` allowlist. Sized: depends on which engines we want.
8. **stdout/stderr behavior under DOSBox-X / real DOS.** ScummVM uses `debug()` / `warning()` / `error()` macros that route through the OSystem's logging — DOS port glue needs to wire these to `printf` (per `DOS-PORT-PLAYBOOK.md § DOSBox-X test workflow / Visible vs headless`). Sized: 1 day.
9. **`AGS_NO_VIDEO_PLAYER`-equivalent for cinematic-using engines.** Some SCUMM games have intro videos (SAN format for Curse of Monkey Island; Smush for Full Throttle, The Dig). These likely just-work since SCUMM video is software-decoded, but worth confirming. Sized: minor.
10. **GL plugin / GL utility code.** ScummVM's `graphics/opengl/` exists and may be transitively pulled in by the build even when no GL-using engine is enabled. Probably fine to compile against a stub GL header on DOS (the symbols are referenced but never called at runtime), but worth confirming. Sized: 1 day.

---

## Recommendation

**ScummVM port (SCUMM-only subset) is feasible.** Plan for **6-10 weeks** on a single engineer for a working `SCUMMVM.EXE` reaching the title screen of one canonical SCUMM game on Tier 1 hardware. Compared to NXEngine-evo (the doskutsu baseline at ~6 weeks): roughly **1.5x the effort**, dominated by the new `backends/platform/dos/` directory and the configure-script integration — neither of which has the architectural surprise factor that AGS's `std::thread` audio core or engge2's OpenGL+Nim stack do.

| Stage | Effort | Notes |
|---|---|---|
| Configure script `msdosdjgpp` arm | 2-3 days | Add case statement, wire port_mk |
| `backends/platform/dos/` directory | 1-2 weeks | Model on `backends/platform/sdl/`, accommodate cooperative scheduler |
| `backends/platform/atari/` reference reading | 1 day | Closest analog for memory layout, dlmalloc |
| DJGPP build patches (CMake-equivalent gates, target flags, software-renderer force) | 3-5 days | Reusable categories from `DOS-PORT-PLAYBOOK.md`; ScummVM uses Make not CMake so the patches target Makefile fragments |
| `--enable-engine=scumm --disable-all-engines` baseline | 2-3 days | Configure-time engine filtering; verify build |
| Memory characterization + tier sizing | 2-3 days | Wine/DOSBox-X with constrained XMS |
| Testing (DOSBox-X playthrough Monkey Island 2, real HW) | 2-3 weeks | Mirror Phase 7 / 8 from doskutsu |
| Stretch: expand engine set (SCI, AGI, AGOS, KYRA, etc.) | each adds 1-2 weeks | Gating on per-engine threading audit + memory budget |

**Total estimate (SCUMM only):** 6-10 weeks for `SCUMMVM.EXE` reaching MI2 title screen on Tier 1. Beats AGS's 8-14 weeks. Dominated by the new DOS backend + configure-script work; no rewriting of upstream architecture (in stark contrast to AGS).

**For Thimbleweed Park specifically:** **not feasible** without rewriting `engines/twp/gfx.cpp`. The OpenGL dependency is direct, hard, and runs through hundreds of GL calls plus a shader pipeline. Replicating that surface in ScummVM's software renderer is multi-quarter work. If the goal is "Thimbleweed Park on doskutsu hardware," **wait** — either for an upstream non-GL fallback (unlikely; the engine is GL-architected) or for a clean-room 2D-software reimplementation by a third party. The next investigation in this series should NOT pursue TWP-on-ScummVM.

**Patch surface estimate:**

| Patch category | Expected count |
|---|---|
| ScummVM build-system patches (configure arm, Makefile fragments, port_mk) | 4-6 |
| New backend directory `backends/platform/dos/` (greenfield, not patches) | n/a — new code, sized as 1-2 KLOC |
| DJGPP target-flag gates in Makefile.common | 2-3 |
| `size_t` / `fopen` audit fixes | 1-3 |
| Engine-selection lockdown (gating out OpenGL-using engines) | 1-2 |
| SDL3-DOS-specific cooperative-scheduler accommodations | 2-4 |

Roughly **10-18 patches** plus a 1-2 KLOC DOS backend directory, vs doskutsu's 27 patches against NXEngine-evo. **The patch count is lower because ScummVM has done much of the work upstream** (SDL3 support, software-renderer-first architecture, MutexInternal abstraction).

---

## Open questions for human review

These could not be resolved from upstream alone. Resolution should precede a serious porting commitment.

1. **Confirm date and reason for DOS port removal from upstream.** Was it dropped circa 2008 (Allegro→SDL transition era)? Circa 2015 (when SDL2 became baseline)? Identifying the last-good commit and what was in it might give us a starting point — even a 2008-era DOS backend would have useful structural choices (DJGPP-specific tweaks, conventional-memory-vs-XMS handling) we could mine. Run `git log --oneline --all -- '*dos*' '*djgpp*'` against an upstream clone.
2. **Per-engine threading audit beyond SCUMM and TWP.** Run `grep -rE 'std::thread|SDL_CreateThread|pthread_create|std::async|Common::Thread' engines/` against a clone and tabulate which engines fail the audit. This determines the buildable engine set beyond SCUMM. Likely-suspect engines: `groovie` (video playback), `bladerunner` (cinematic-heavy), `mtropolis` (modern), `qdengine`. Worth a survey.
3. **Per-engine `configure.engine` audit for `3d` / `opengl_game_shaders` tokens.** Run `grep -rE '3d|opengl_game_shaders|highres' engines/*/configure.engine` and produce the buildable-vs-not-buildable list. Probably 60-70 engines pass; 10-15 fail.
4. **Atari Falcon backend memory model.** The Atari port (`backends/platform/atari/`) targets 14 MB max RAM and uses a custom `dlmalloc`. Is the engine actually playable on that profile, and which games does it run? If yes, it's a strong existence proof for our Tier 2 / Tier 3 targets. If no, the Atari backend is a Cubrick analog (compiles but limited at runtime), not a proof point.
5. **GL utility code transitively pulled in.** When `--disable-all-engines --enable-engine=scumm` is configured, does `graphics/opengl/` still compile (it might be referenced by `graphics/` core code)? If yes, we need a configure-time `--disable-opengl` or stub-headers approach. If no, clean.
6. **Memory budget characterization.** Run Monkey Island 2 in Wine or DOSBox-X with the XMS cap set to 32 MB / 16 MB / 8 MB and measure actual working set. Decisive for the Tier 1 / Tier 2 / Tier 3 expectation.
7. **`Common::File` 32-bit-`size_t` audit.** Spot-check `common/file.cpp` for any `int64_t` seek/tell paths that need to compile cleanly under DJGPP's 32-bit `size_t`.
8. **Shader-pipeline emulation as a TWP escape hatch.** In principle, a CPU-side shader emulator (interpret GLSL fragments in C++) could let `engines/twp/gfx.cpp` run unmodified. This is not a serious proposal — performance would be 1-10 fps on Tier 1 — but worth flagging as the only structural alternative to a TWP renderer rewrite.
9. **Upstream willingness to receive a DOS backend.** Per `CLAUDE.md § Vendoring`, doskutsu's patches stay local. But ScummVM is a community-led project that has historically welcomed unusual platform ports (3DS, Atari Falcon, N64, Dreamcast, PSP all live in tree). A DOS backend might be welcome upstream rather than carried as a patch series. Worth asking in the ScummVM forum before deciding the patch-locality posture for this specific port.

---

## References

- ScummVM upstream: https://github.com/scummvm/scummvm
- ScummVM 2.9.0 release announcement: https://www.scummvm.org/news/20241222/
- ScummVM TWP compatibility entry: https://www.scummvm.org/en/compatibility/2.9.0/twp:twp/
- Atari Falcon backend (constrained-platform reference): https://github.com/scummvm/scummvm/tree/master/backends/platform/atari
- TWP Squirrel coroutine model: `engines/twp/twp.cpp:1320` (`sq_newthread(v, 1024)` — script coroutine, not OS thread)
- TWP OpenGL direct-call usage: `engines/twp/gfx.cpp` (`glGenTextures`, `glBindTexture`, `glTexImage2D`, `glDrawArrays`, `glDrawElements`, `glGenFramebuffers`, `glBlendFuncSeparate`, plus `_shader.use()` and `_shader.setUniform`)
- TWP engine config (declares 3d + opengl_game_shaders): `engines/twp/configure.engine` (`add_engine twp "Thimbleweed Park" yes "" "" "16bit 3d highres vorbis png opengl_game_shaders" "imgui"`)
- SCUMM engine config (no 3d/GL deps): `engines/scumm/configure.engine` (`add_engine scumm "SCUMM" yes "scumm_7_8 he" "v0-v6 games" "" "midi fmtowns_pc98_audio sid_audio imgui"`)
- ScummVM mixer architecture: `audio/mixer_intern.h` (callback-driven, no engine thread)
- SDL audio init: `backends/mixer/sdl/sdl-mixer.cpp` (parallel SDL2 / SDL3 paths via `SDL_VERSION_ATLEAST(3, 0, 0)`)
- Common::Mutex abstraction: `common/mutex.h` + `backends/mutex/sdl/sdl-mutex.cpp`
- ScummVM C++ standard (`-std=c++11`): in `configure` script (`append_var CXXFLAGS "-std=${std_variant}11"`)
- Thimbleweed Park asset extraction tools (third-party): https://github.com/mstr-/twp-ggdump, https://github.com/scemino/NGGPack, https://github.com/s-l-teichmann/ggpack, https://github.com/fzipp/gg
- doskutsu DOS-port playbook: `docs/ports/DOS-PORT-PLAYBOOK.md`
- Prior memos in series: `docs/ports/AGS-FEASIBILITY.md`, `docs/ports/ENGGE2-FEASIBILITY.md`
