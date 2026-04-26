# AGS (Adventure Game Studio) — DOS Port Feasibility

Feasibility memo for porting [Adventure Game Studio](https://github.com/adventuregamestudio/ags) (the engine that powers Wadjet Eye's Blackwell series, Resonance, Gemini Rue, Technobabylon, Unavowed, Kathy Rain, and several hundred indie adventure games) to MS-DOS 6.22 / DJGPP using the doskutsu port stack (statically-linked SDL3-with-DOS-backend + SDL3_mixer + SDL3_image + DJGPP + CWSDPMI).

**Recommendation up front:** AGS is **substantially harder than NXEngine-evo** to port. It is feasible on Tier 1 (PODP83 / 48 MB) hardware in principle, but the engine's `std::thread`-based audio core is a load-bearing blocker that requires real architectural rework before the binary will run correctly under SDL3-DOS's cooperative scheduler. The Allegro 4 dependency that historically gave AGS DOS support is **gone** — the current AGS code links a stripped Allegro 4 fork that explicitly drops `djgpp` from its supported targets, and uses SDL2 for everything that matters. Plan for a porting project on the order of 3-6x the effort of NXEngine-evo, dominated by the audio-thread refactor.

This is MIT-licensed prose. Readers should consult `docs/ports/DOS-PORT-PLAYBOOK.md` for the engine-agnostic playbook this memo cites.

---

## License

**Artistic License 2.0** for both the editor and engine. Quoted from upstream's `LICENSE.txt`. Artistic 2.0 is GPLv3-compatible per the [FSF's compatible-licenses list](https://www.gnu.org/licenses/license-list.html#ArtisticLicense2). It is **also** compatible with the host repo's MIT (Artistic 2.0 has no copyleft requirement on derivative works distributed under a different license).

For a doskutsu-style port, the license stack would be:

- AGS engine code: Artistic 2.0
- Port-glue and Makefile: MIT (or whatever the host repo uses)
- Static-link binary: dominant license is whatever the most-restrictive linked dep imposes — likely Artistic 2.0 for AGS itself, possibly LGPL for some video codec deps if those got linked.

No GPLv3 derivative concerns since AGS is not GPL.

---

## Language and minimum standard

C++ (45.2%), C (30.0%), C# (19.3% — editor only, not engine), Java (1.6% — Android port), Shell (1.5%), Makefile (0.9%). The C# is the editor (Windows-only WinForms) and is **not** part of the runtime engine — irrelevant to a DOS port.

**Engine C++ standard:** uncertain — the upstream `CMakeLists.txt` does not surface a `CMAKE_CXX_STANDARD` line in the portion fetched. AGS 3.6.x is contemporary code, so C++14 or C++17 is plausible. DJGPP 12.2.0 (the doskutsu pin) supports C++20; not a concern unless the engine reaches into very recent library features (filesystem, parallel STL).

**Open question:** confirm exact C++ standard requirement against `Engine/CMakeLists.txt`'s `set(CMAKE_CXX_STANDARD ...)` line — couldn't verify from the fetch.

---

## Build system

**CMake-based.** Documented in `CMAKE.md` at the upstream root. Build options surfaced:

- `AGS_TESTS` — build tests
- `AGS_BUILD_ENGINE` — engine target (on by default)
- `AGS_BUILD_TOOLS` — packing utility
- `AGS_BUILD_COMPILER` — standalone AGS Script Compiler
- `AGS_NO_VIDEO_PLAYER` — turns off Theora/AVI video playback
- `AGS_BUILTIN_PLUGINS` — link plugins into the engine
- `AGS_DEBUG_MANAGED_OBJECTS`, `AGS_DEBUG_SPRITECACHE` — verbose logging toggles

Local-libs flags exist for `SDL2`, `SDL2_SOUND`, `GLM`, `Miniz`, `TinyXML2`, `OGG`, `Theora`, `Vorbis`, and `GTest`. Acceptable build-system shape for a doskutsu-style stage cross-build; no autotools or hand-rolled build system to fight.

**Required `find_package` calls** in upstream's `CMakeLists.txt` (verified by fetch):

```
find_package(SDL2 REQUIRED)
find_package(SDL2_sound REQUIRED)
find_package(Ogg REQUIRED)
find_package(Vorbis REQUIRED)
find_package(Theora REQUIRED)
find_package(glm REQUIRED)
find_package(tinyxml2 REQUIRED)
find_package(miniz REQUIRED)
```

Plus `include(FindLocalAllegro)` (the AGS-forked Allegro 4) and `include(FindLocalOpenAL)` (referenced but possibly only on some platforms).

Sized to the doskutsu Makefile model, this is roughly 8-10 build stages instead of 3:

1. SDL3 (existing)
2. SDL3_mixer (existing — but see "Audio dependency" below; AGS uses SDL2_sound which is **not** SDL_mixer)
3. SDL2_sound (new — needs DJGPP port; SDL2_sound depends on Ogg/Vorbis/etc.)
4. libogg, libvorbis (host deps; both are LGPL-compatible)
5. libtheora (LGPL-compatible video codec; can probably be skipped via `AGS_NO_VIDEO_PLAYER=ON`)
6. glm (header-only; trivial)
7. tinyxml2 (zlib license; trivial)
8. miniz (MIT; trivial)
9. Stripped Allegro 4 fork (`adventuregamestudio/lib-allegro`; needs DJGPP target re-added — see below)
10. AGS engine itself

**The stripped Allegro 4 fork** at `github.com/adventuregamestudio/lib-allegro` (branch `allegro-4.4.3.1-agspatch`) **explicitly lists `djgpp` as no longer supported**. Quoted from that fork's CMakeLists comment:

> "Ports that used to work which aren't supported yet: Haiku/BeOS port, djgpp port, QNX port, BCC32, Watcom, DMC"

Re-adding DJGPP support to a stripped Allegro 4 fork is a substantial sub-project — but it might be tractable since vanilla Allegro 4.2.x had working DJGPP support (`liballeg.org/old.html`), and a third-party fork at `github.com/msikma/allegro-4.2.2-xc` exists explicitly for DOS cross-compilation. The path: build the AGS-fork's Allegro 4 against the DJGPP-cross targets that the original Allegro 4.4.x had stripped, restoring just the surface area AGS calls into. Order-of-magnitude estimate: 2-4 weeks of bring-up work.

---

## Graphics dependency: SDL2 (load-bearing) + Allegro 4 (residual)

This is the most important question for a DOS port and the answer is mixed.

**SDL2 is the primary backend.** AGS 3.6.0 (March 2022 onwards) uses SDL2 for all platform abstraction: window creation, input, event loop, audio device, OpenGL ES2 / OpenGL context. Quoted from `Engine/main/engine.cpp`:

```cpp
if (sys_main_init()) {
    const char *err = SDL_GetError();
    ...
}
```

`sys_main_init()` lives in `Engine/platform/sdl/sys_main.cpp` (path inferred; the directory is `Engine/platform/sdl/` and was confirmed via `Engine/main/engine.cpp`'s `#include "platform/base/sys_main.h"`).

**Allegro 4 is residual.** The same `engine.cpp` contains:

```cpp
if (install_allegro(SYSTEM_NONE, &errno, atexit)) {
    platform->DisplayAlert("Internal error: unable to initialize stripped Allegro 4 library.")
    ...
}
```

The phrase "stripped Allegro 4 library" is the upstream's own characterization. AGS still uses Allegro 4 for **bitmap drawing** — the renderer compositing pipeline is built on Allegro 4 BITMAPs, with SDL2 only handling the final blit-to-window. Quoted from the AGS forum (`adventuregamestudio.co.uk/forums/.../wip-ags-sdl2-port-for-testing-supposedly-ags-3-6-0/`):

> "The port keeps existing AGS renderers, only using SDL for drawing bitmaps (same way as allegro is used now), while providing more stable contemporary system support for window creation, input etc."

So the architecture is:

```
Game logic
    ↓
AGS sprite system / scene composition (Allegro 4 BITMAPs)
    ↓
Software renderer: AGS draws into Allegro 4 BITMAP
    ↓
Convert to SDL2 surface/texture, push to SDL2 window
```

For the doskutsu stack, this is workable: SDL3-DOS only has a software renderer anyway, so the "convert to SDL surface, push to window" step doesn't conflict. The Allegro 4 BITMAP layer is internal to the engine and doesn't need a working DOS Allegro 4 if the AGS-fork can be built against DJGPP — i.e., if the renderer's Allegro 4 dependency is purely structural (BITMAP type, blit functions) and not platform-specific (no Allegro 4 video driver / sound driver / input driver in the runtime path).

**Open question:** confirm the AGS-fork Allegro 4's runtime path uses `SYSTEM_NONE` everywhere (which the engine.cpp snippet suggests), meaning Allegro's platform layer is fully bypassed and only its drawing primitives are linked. If yes, restoring the DJGPP build is straightforward (compile only the data-structure/blit-function parts). If no, the DJGPP work expands to re-add a DOS video/sound/input driver to the Allegro 4 fork — a much larger project.

**OpenGL/OpenGL ES2 is also referenced** (`set(AGS_OPENGLES2 TRUE)` in upstream CMakeLists) but is mostly used for the Android/Emscripten ports, not desktop. Verify against an upstream maintainer that desktop builds work with the software-renderer-only pathway and don't require a GL context. AGS's plugin API may also expose OpenGL functions; if so, those plugin signatures need to compile against a stub GL header on DOS (and any plugin that actually calls them won't load).

---

## Audio dependency: SDL2_sound + custom mixer + std::thread

**This is the load-bearing porting blocker.**

AGS does **not** use SDL_mixer. It uses **SDL2_sound** (a separate library: `github.com/icculus/SDL_sound`) for decoding, plus a custom mixer in `Engine/media/audio/audio_core.cpp`.

The custom mixer runs on a **dedicated `std::thread`**. Quoted from `Engine/media/audio/audio_core.cpp`:

```cpp
std::thread audio_core_thread;                                   // line 47
std::mutex mixer_mutex_m;                                        // line 54
std::condition_variable mixer_cv;                                // line 55
g_acore.audio_core_thread = std::thread(audio_core_entry);       // line 127
if (g_acore.audio_core_thread.joinable())                        // line 131
    g_acore.audio_core_thread.join();
std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);           // lines 172, 182
std::unique_lock<std::mutex> ulk(g_acore.mixer_mutex_m);         // lines 177-178
g_acore.mixer_cv.wait_for(lk, std::chrono::milliseconds(50));    // line 226
```

The audio core spawns a worker thread that polls sound decoders every 50 ms, protected by a single mutex. **This is incompatible with SDL3-DOS's cooperative scheduler.** Per `docs/ports/DOS-PORT-PLAYBOOK.md § DOS-specific gotchas / Threading`:

> Do not introduce `SDL_CreateThread`, `std::thread`, `pthread_create`, or `std::async` in port glue, audio refactors, or anywhere else. An engine whose upstream uses `std::thread` for audio mixing or asset streaming has to rewrite that code synchronously before the binary will run correctly under SDL3-DOS.

DJGPP 12.2.0 has a `std::thread` implementation that compiles against pthreads — but pthreads on DJGPP either does not exist or pre-empts only via cooperative yield points. Even if `std::thread` compiles and links, the `wait_for(lk, 50ms)` call will not behave like the SDL-pumped cooperative scheduler the rest of the engine assumes, and the audio worker will either deadlock or starve.

**The required rework:** convert AGS's audio core from a `std::thread` polling-loop architecture to a synchronous pump driven from the SDL3 audio device callback. SDL3's audio API (`SDL_OpenAudioDeviceStream`, `SDL_AudioStreamCallback`) calls back into application code at audio-fill time; mixing happens inside that callback synchronously. Doable, but it's a rewrite of `audio_core.cpp` and possibly its callers. Sized: 1-2 weeks of focused engineering plus a correctness gate (capture WAV before/after, A/B against a reference build).

This rewrite is **co-equal in scope** with the entire NXEngine-evo SDL2→SDL3 audio refactor cluster (`patches/nxengine-evo/0013-0017` plus follow-ups `0020`, `0024`). It is the single largest piece of porting work.

---

## SDL2 → SDL3 migration surface

AGS 3.6.x is on SDL2. Migrating to SDL3 (since SDL3-DOS is the only working stack) requires the same kind of patch series as doskutsu's NXEngine-evo migration. By doskutsu's call-site categories:

- **Mechanical renames** (`SDL_RenderCopy`→`SDL_RenderTexture` etc.): expect 30-100 sites across `Engine/platform/sdl/*.cpp`, `Engine/main/*.cpp`, and any direct SDL2 use in graphics code. Largely sed-bait.
- **Event enum renames** (`SDL_KEYDOWN`→`SDL_EVENT_KEY_DOWN`): expect ~14-20 sites in input handling.
- **Surface/Renderer API**: `SDL_CreateRGBSurface`→`SDL_CreateSurface`, `SDL_FillRect`→`SDL_FillSurfaceRect`. AGS does its own software composition, so surfaces are heavily used. Expect 10-30 sites.
- **Audio API**: SDL2's audio device API differs from SDL3's. The migration is part of the audio-thread rewrite above.
- **Joystick / GameController**: SDL3 collapsed `SDL_GameController` and `SDL_Joystick` API surfaces. AGS uses GameController per the search results showing `SDL_INIT_GAMECONTROLLER` in the `SDL_Init` call.
- **`SDL_INIT_TIMER`** is gone in SDL3 (events subsystem covers what timer used to provide).

**Open question:** AGS-side migration patch count — couldn't enumerate precisely without fetching every `.cpp`.

---

## SDL2_sound DJGPP port

[SDL2_sound](https://github.com/icculus/SDL_sound) is a decoder library (Vorbis, FLAC, MP3, etc.) that produces PCM. License: zlib (GPLv3- and MIT-compatible). Lightweight C; depends on the chosen decoder backends (libvorbis, libflac, etc.).

Has **no prior DJGPP build documented**. Bring-up is its own sub-project:

- Verify `find_package` and CMake gates work with the DJGPP toolchain file
- Disable codecs the engine doesn't need (AGS uses Vorbis primarily; FLAC and MP3 might be optional)
- Audit for `size_t` 32-bit assumptions in stream-length math
- Confirm no threading primitives (the decoder is a pull-API, so this is plausibly clean)

Estimated: 1-2 days of incremental patching, similar shape to the doskutsu SDL3_mixer bring-up but without the API redesign overhead (no SDL2 → SDL3 migration on SDL2_sound — it was zlib-licensed and never had an SDL3 follow-up that I can confirm; if AGS's audio rewrite drops to SDL3 directly, SDL2_sound's role might collapse into "just call `SDL_LoadWAV_IO` plus stb_vorbis directly").

**Open question:** does AGS strictly require SDL2_sound, or could the audio rewrite use SDL3_mixer's decoder API directly, eliminating one porting stage?

---

## Threading audit

**`std::thread` count in upstream:** at least 1 confirmed (`Engine/media/audio/audio_core.cpp`). There may be more — the audit should grep the whole tree for `std::thread`, `pthread_create`, `SDL_CreateThread`, `std::async`. This is the load-bearing question for porting feasibility; if the count is "just the audio core," the rewrite is contained. If it's 5+, the rewrite cascades.

**Open question:** complete threading audit not done — needs `grep -rE 'std::thread|SDL_CreateThread|pthread_create|std::async' Engine/ Common/` against a clone.

---

## Memory budget

No upstream documentation on minimum RAM. Empirically:

- AGS on modern Windows comfortably uses 200-500 MB. That's modern-platform expectation, not a hard requirement.
- AGS games of the Wadjet Eye era (Blackwell, Resonance, Gemini Rue: 2009-2014) targeted Windows XP, which means engines could plausibly run in 64-128 MB.
- AGS's sprite cache is `AGS_DEBUG_SPRITECACHE`-tunable; the engine has explicit cache management.

**For Tier 1 (PODP83 / 48 MB):** plausible if the sprite cache fits. Each Wadjet Eye game has ~100-300 MB of art assets — not all loaded at once, but the resident-set behavior is uncharacterized for AGS on tight memory.

**For Tier 2 (16 MB):** speculative. Probably requires running a game written for low-spec hardware (a small-art-budget AGS game from the early 2010s) rather than a full Wadjet Eye production.

**For Tier 3 (8 MB):** unlikely without significant engine work to lazy-load and stream sprite assets. This tier is aspirational for any modern engine port; probably out of scope for AGS specifically.

**Open question:** what's the resident-set size of a representative AGS game on a modest configuration? Worth measuring against a real Wadjet Eye game on Wine + a memory-limited Windows VM before committing to the port.

---

## Specific risks and blockers

In rough order from "likely show-stopper" to "tractable":

1. **`std::thread` audio core (likely show-stopper without rewrite).** Sized as a co-equal effort to the entire NXEngine-evo audio refactor. Mandatory.
2. **Stripped Allegro 4 fork's no-DJGPP stance (substantial blocker).** Requires re-adding DJGPP support to the AGS-fork. 2-4 weeks. The path exists (vanilla Allegro 4.2.x supported DJGPP, third-party `msikma/allegro-4.2.2-xc` exists for cross-compile), but reconciling the AGS-fork's patches with DJGPP-target build infrastructure is its own mini-project.
3. **SDL2_sound DJGPP port (no prior precedent).** 1-2 days; tractable.
4. **AGS's plugin system might require `dlopen`.** SDL3-DOS doesn't support `SDL_LoadObject`; AGS plugins on DOS would need to be `AGS_BUILTIN_PLUGINS=ON` (statically linked) or unsupported. Worth confirming whether the games of interest depend on runtime-loaded plugins.
5. **Theora video playback (skippable).** AGS games often have intro videos. `AGS_NO_VIDEO_PLAYER=ON` skips this. For an art-asset port, the user accepts no intro video.
6. **OpenGL / OpenGL ES2 paths (probably skippable).** AGS's software renderer should not require a GL context on desktop builds, but the OpenGL plugin surface and any GL-using plugins (rare) would need to fall back to no-op.
7. **C# editor is irrelevant.** No port concerns for the engine binary.
8. **Allegro 4 BITMAP semantics on DJGPP.** The AGS-fork's blit and color-format conventions need to match what the engine assumes. Mostly mechanical once Allegro 4 builds.
9. **SDL2 → SDL3 migration patch series.** Roughly the same shape as doskutsu's NXEngine-evo migration. 1-2 weeks of patch authoring against a working SDL2 baseline.

---

## Recommendation

**AGS port is feasible but expensive.** Plan for a 6-12 week project on a single engineer, maybe shorter with two. The work decomposes:

| Stage | Effort | Notes |
|---|---|---|
| SDL2_sound DJGPP build | 1-2 days | New build stage |
| Audio core rewrite (sync only) | 1-2 weeks | Co-equal with doskutsu's audio refactor; the load-bearing piece |
| AGS-fork Allegro 4 DJGPP restoration | 2-4 weeks | Largest unknown; may be much faster if structural |
| SDL2 → SDL3 migration | 1-2 weeks | Mechanical pass + audio integration |
| DJGPP build patches (CMake gates, target flags, software-renderer force) | 2-3 days | Reusable categories from `DOS-PORT-PLAYBOOK.md` |
| Asset extraction / packaging | engine-data-format question; probably trivial | AGS games ship as `.exe + .vox + .ags` data file; extraction may not be needed if the data files are loadable as-is |
| Testing (DOSBox-X playthrough, real HW) | 1-2 weeks | Mirror Phase 7 / 8 from doskutsu |

**Total estimate:** 8-14 weeks for a working `AGS.EXE` reaching the title screen of one Wadjet Eye game on Tier 1 hardware. Tier 2 / Tier 3 are stretch goals beyond initial port.

Compared to doskutsu (NXEngine-evo): roughly **3-4x the effort**, dominated by the audio-thread rewrite and the Allegro 4 fork restoration.

**Patch surface estimate:**

| Patch category | Expected count |
|---|---|
| AGS-engine DJGPP build patches (`0001-0009` slot) | 8-12 |
| AGS-engine SDL2→SDL3 migration patches (`0010-0019` slot) | 10-15 |
| AGS-engine audio rewrite (separate concern) | 1 large patch or 4-6 smaller patches |
| AGS-fork Allegro 4 DJGPP restoration patches | 5-15 |
| SDL2_sound DJGPP patches | 1-3 |

Roughly 30-50 patches in a working set, vs doskutsu's 27.

---

## Open questions for human review

These could not be resolved from upstream alone. Resolution should precede a serious porting commitment.

1. **Confirm exact C++ standard requirement.** Check `Engine/CMakeLists.txt` for `set(CMAKE_CXX_STANDARD ...)`. DJGPP 12.2.0 supports C++20; C++23 features could trip the toolchain.
2. **Audit `std::thread` / threading primitive count across the full tree.** The audio-core thread is confirmed; are there others (asset streaming, save-game async, video playback)? Run `grep -rE 'std::thread|SDL_CreateThread|pthread_create|std::async' Engine/ Common/` against a current clone.
3. **Confirm the AGS-fork Allegro 4's runtime architecture.** Does it actually use `SYSTEM_NONE` everywhere (Allegro's platform layer fully bypassed, only blit primitives linked) or does it expect a working Allegro video/sound driver? This determines whether DJGPP support requires re-adding a DOS Allegro driver or just compiling the structural-data parts.
4. **Confirm SDL2_sound is strictly required.** Could the audio rewrite use SDL3_mixer's decoder API directly (`MIX_CreateAudioDecoder` over `SDL_IOFromMem`) and eliminate SDL2_sound from the dependency tree? This would save the SDL2_sound DJGPP-port stage.
5. **Confirm AGS's plugin model.** Are plugins required to be statically linked (`AGS_BUILTIN_PLUGINS`)? Do any of the target games (Wadjet Eye, etc.) ship with runtime-loaded plugins that wouldn't work under static-only DOS? Affects which games are playable on the port.
6. **Memory characterization.** Run a representative Wadjet Eye game on Wine with a 48 MB cap and confirm working-set sustainability. If a representative AGS game already exceeds 48 MB working set on Wine, Tier 1 is at risk.
7. **OpenGL plugin surface.** Confirm desktop AGS builds run cleanly without a GL context, or identify which subsystems require it. If any rendering path requires GL, it must be patched out for SDL3-DOS.
8. **Theora / video playback necessity.** Are intro videos optional in target games, or hard-coded? `AGS_NO_VIDEO_PLAYER=ON` should make this a non-issue, but worth confirming behavior on a game that has intro videos when the toggle is set.

---

## References

- AGS upstream: https://github.com/adventuregamestudio/ags
- AGS Allegro 4 fork: https://github.com/adventuregamestudio/lib-allegro
- SDL2_sound: https://github.com/icculus/SDL_sound
- AGS SDL2 migration discussion: https://www.adventuregamestudio.co.uk/forums/editor-development/wip-ags-sdl2-port-for-testing-supposedly-ags-3-6-0/
- AGS engine ports: https://www.adventuregamestudio.co.uk/wiki/AGS_Engine_ports
- vanilla Allegro 4 with DJGPP: https://liballeg.org/old.html
- third-party Allegro 4 DJGPP fork: https://github.com/msikma/allegro-4.2.2-xc
- AGS issue #1096 (Allegro→SDL migration tracker): https://github.com/adventuregamestudio/ags/issues/1096
- AGS issue #1148 (AGS3 SDL2 port fixes): https://github.com/adventuregamestudio/ags/issues/1148
- doskutsu DOS-port playbook: `docs/ports/DOS-PORT-PLAYBOOK.md`
