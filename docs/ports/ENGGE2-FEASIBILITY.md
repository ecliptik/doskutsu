# engge2 — DOS Port Feasibility

Feasibility memo for porting [engge2](https://github.com/scemino/engge2) (an open-source reimplementation of Thimbleweed Park's engine — Ron Gilbert's post-Monkey-Island adventure-game engine) to MS-DOS 6.22 / DJGPP using the doskutsu port stack.

**Recommendation up front:** **Not feasible** within the doskutsu stack constraints. engge2 is written in Nim (no DJGPP toolchain support) and renders via OpenGL 3.3 core profile (which SDL3-DOS does not provide — SDL3-DOS is software-renderer-only). Either constraint alone would be a major sub-project; both together place the port well outside the cost / risk envelope of doskutsu's NXEngine-evo work. The predecessor [engge](https://github.com/scemino/engge) (C++) is archived and shares the OpenGL dependency.

A DOS port of Thimbleweed Park-class adventure engines is plausible only via a different reimplementation that uses 2D-only rendering (no GL) and a DOS-portable language (C / C++ / DJGPP-compatible Pascal). engge / engge2 are not the right starting point for a doskutsu-style cross-build.

This is MIT-licensed prose. Readers should consult `docs/ports/DOS-PORT-PLAYBOOK.md` for the engine-agnostic playbook this memo cites.

---

## License

**MIT** (per the `engge2.nimble` package metadata: `license = "MIT"`). MIT is GPLv3-compatible and trivially compatible with the host repo's MIT.

The predecessor `engge` (archived) is also **MIT** licensed.

License is not a blocker.

---

## Language and toolchain

**Nim** (100% of `engge2`'s codebase). Build via `nimble run`, the Nim package manager.

This is the **first hard blocker.**

DJGPP is GCC + binutils for `i586-pc-msdosdjgpp`. There is no DJGPP back-end for the Nim compiler. Nim's standard targets are: native (compile to C, then through any C compiler), C++ (similar), Objective-C, JS. The "compile to C" path is in principle available — `nim c --cc:gcc --os:dos ...` could be attempted — but `--os:dos` is not in Nim's documented OS list, and Nim's standard library makes platform-specific assumptions (file IO via POSIX, threading via pthreads, network via BSD sockets) that DJGPP either does not provide or provides in idiosyncratic forms.

A serious port would require, at minimum:

1. Adding `dos` as a recognized Nim OS target (Nim core change).
2. Stubbing or rewriting the parts of Nim's stdlib that engge2 transitively depends on for DJGPP-compatible equivalents.
3. Ensuring Nim's GC and exception machinery produce code that links against DJGPP's libc.

Rough estimate: this is a **multi-month language-toolchain project** before any engge2 source compiles. Out of scope for a port that hopes to leverage doskutsu's existing infrastructure.

The predecessor `engge` is C++ (94.6%) and shares the OpenGL dependency below — see "Predecessor: engge."

---

## Build system

**Nimble.** Nim's package manager / build tool. Primary build command is `nimble run`. Custom dependency forks managed via `nimble`.

For a DJGPP cross-build, this would need replacing with a CMake or hand-rolled Makefile. Not a major sub-blocker on its own (doskutsu re-orchestrates upstream's CMake from the top-level Makefile already), but it stacks on top of the Nim toolchain blocker.

---

## Graphics dependency: OpenGL 3.3 core profile

This is the **second hard blocker.**

From `src/sys/app.nim`:

```nim
import sdl2
import sdl2/mixer
import nglib/opengl

sdl2.init(INIT_EVERYTHING)
discard glSetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)
discard glSetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3)
discard glSetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)
w = createWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                 size.x.int32, size.y.int32, SDL_WINDOW_OPENGL | ...)
glContext = glCreateContext(w)
```

engge2 requests an **OpenGL 3.3 core profile context** for rendering. This is incompatible with SDL3-DOS in two ways:

1. **SDL3-DOS has no GL context support at all.** Per `docs/ports/DOS-PORT-PLAYBOOK.md § DOS-specific gotchas / Renderer`: SDL3-DOS provides only a software renderer; `SDL_GL_*` symbols exist (since they're part of SDL3's API surface) but the runtime cannot satisfy a `SDL_GL_CreateContext` call. The window flag `SDL_WINDOW_OPENGL` cannot be honored.
2. **DJGPP has no production OpenGL implementation.** There were experimental Mesa-on-DJGPP ports historically (DMesa, FXMesa for 3dfx), but none target a modern OpenGL 3.3 core profile, and none are actively maintained for the GCC 12 / DJGPP 12 era. A working `OpenGL 3.3 core` driver on DJGPP would be a major upstream project independent of the engge2 port itself.

The only path forward would be **rewriting engge2's renderer from OpenGL to software 2D blits**. This is not a port-level adaptation; it's an engine-architecture rewrite. Thimbleweed Park's engine uses GL for: textured-quad sprite drawing, alpha blending, scene compositing, scrolling backgrounds, lit scenes. Replicating that surface in pure software at adventure-game-acceptable framerates on a Pentium-class machine is a research project, not a porting project.

For comparison: NXEngine-evo's renderer is already structured around 2D blits (paletted sprites onto a 320x240 framebuffer), which is why doskutsu could rely on `SDL_RenderTexture` over `SDL_RENDERER_SOFTWARE`. engge2's renderer is fundamentally different.

---

## Audio dependency: SDL2_mixer

From `src/audio/audio.nim`:

```nim
import sdl2
import sdl2/mixer
```

Calls `loadWAV`, `playChannel`, `fadeInChannel`, `volume` from SDL2_mixer.

This is the **least-bad part of the dependency tree.** doskutsu has already done the SDL2_mixer → SDL3_mixer migration work for NXEngine-evo (`patches/nxengine-evo/0014-0017` cluster). The audio-API delta is well-understood: `Mix_OpenAudio` → `MIX_CreateMixer`, `Mix_HookMusic` → `MIX_Track`, `Mix_QuickLoad_RAW` → `MIX_CreateAudioDecoder`. If everything else worked, the audio migration would be straightforward.

But "if everything else worked" is doing a lot of lifting given the Nim and OpenGL blockers above.

---

## Threading

Not visible from the brief inspection. The Nim runtime's threading model (`{.thread.}` pragma, `system/threadpool`) would be the relevant question, plus any `SDL_CreateThread` use in the `nglib` framework.

This question is moot given the Nim and OpenGL blockers — pursuing engge2 is not viable regardless of the threading audit.

---

## Asset format

Thimbleweed Park ships assets in `.ggpack` files (Ron Gilbert's custom container format). engge2's `nimyggpack` dependency parses them. The asset format is documented in the engge / engge2 source and would be extraction-portable if the rest of the port worked, but the user-facing "buy Thimbleweed Park, copy the .ggpack files" step is straightforward — the engine reads the original game's data files directly without an extraction pre-pass.

License of the game data: **commercial.** Thimbleweed Park is a paid game; users supply their own copy. This is parallel to doskutsu's "user supplies Cave Story 2004 freeware" model — except the data is purchased rather than freeware. doskutsu's "asset data is gitignored, never in dist" pattern applies cleanly.

---

## Memory budget

Not characterized. Modern Nim + SDL2 + OpenGL applications typically use ≥100 MB on desktop. Adventure-game working sets are art-asset-bound; Thimbleweed Park's `.ggpack` files are roughly 1.4 GB total, but resident-set behavior is uncharacterized for engge2 specifically.

This is moot given the language and renderer blockers.

---

## Predecessor: `engge` (C++)

The original `engge` repository (`github.com/scemino/engge`) is archived as of 2022, with a banner directing readers to `engge2`. Its language is **C++ (94.6%)** — much more amenable to DJGPP cross-compilation than Nim.

But engge has the **same OpenGL dependency**. Its build pulls in `extlibs/ngf/` (Engge Framework, built on SDL2 + OpenGL) and `glew32.dll`. From the upstream `CMakeLists.txt`:

```
add_subdirectory(extlibs/ngf/)
"${VCPKG_BIN_DIR}/glew32.dll"
"${VCPKG_BIN_DIR}/SDL2.dll"
```

The C++ language path is workable for DJGPP, but the OpenGL renderer is not. Same blocker, different repository. engge is also archived — even if the OpenGL dependency were solvable, working against an archived codebase loses the upstream-rebase benefit that the snapshot+SHA vendoring pattern is designed for.

---

## Specific risks and blockers

1. **Nim toolchain on DJGPP (showstopper).** No precedent. Multi-month language-port project before any engge2 source compiles. Out of scope.
2. **OpenGL 3.3 core profile on DJGPP (showstopper).** SDL3-DOS provides no GL; DJGPP has no production OpenGL implementation. Renderer rewrite from GL to software 2D would be an engine-architecture project, not a port.
3. **Both `engge` and `engge2` share the OpenGL dependency.** Switching to the C++ predecessor only solves the language problem; the renderer problem remains.
4. **Predecessor is archived.** Even if the OpenGL problem were solvable for `engge`, the upstream is not maintained, defeating the snapshot-vendoring model.

Any one of these is a project on its own. All three together place engge / engge2 outside the doskutsu envelope.

---

## Recommendation

**Do not pursue engge2 as a doskutsu-style DOS port.** The language and renderer blockers are independently large enough to disqualify, and they compound.

**If a Thimbleweed-class adventure engine on DOS is the goal**, alternatives to investigate:

1. **ScummVM.** Supports many adventure engines including Thimbleweed Park (since ScummVM 2.7.0). Written in C++, has historical DOS / DJGPP support that was dropped some years ago but might be recoverable. Renderer is already 2D-software-friendly. License: GPLv3 (compatible with doskutsu's licensing posture). **This is probably the right starting point** for any classic-adventure engine port to DOS.
2. **A 2D-only Thimbleweed reimplementation that doesn't yet exist.** Speculative. The asset format (`.ggpack`) and scripting language (Squirrel) are documented; a clean-room 2D-renderer-only reimplementation in C / C++ would be a multi-quarter project but would land on a stack that ports cleanly.

For the `doskutsu` follow-on engine selection process, **prefer ScummVM as the next investigation** — it's far more likely to fit the porting envelope than engge / engge2.

**Sized estimate for engge2 specifically:** infinite. The combination of "port Nim to DJGPP" + "implement OpenGL 3.3 core on top of a software framebuffer that targets a 486" is not a finite-effort engineering task within the budget any sane port project would have. Decline.

---

## Open questions for human review

These could not be resolved from upstream alone.

1. **Threading audit not performed.** Moot given the showstoppers above, but if ever revisited, run `grep -rE '\\.thread\\.|spawn\\b|threadpool|SDL_CreateThread' src/` against engge2 and `grep -rE 'std::thread|pthread_create|SDL_CreateThread' src/ extlibs/` against engge.
2. **Has anyone attempted Nim → DJGPP?** Worth one search; the Nim community might have a partial story (e.g., a `--cc:gcc --os:dos` PR draft that could be a starting point if a future maintainer wanted to research this further).
3. **What ScummVM looks like on DJGPP today.** ScummVM had a DJGPP port circa 2003-2010; it's unclear when it was officially dropped, what its current portability story is, and whether SDL3-DOS would be a viable backend for the parts of ScummVM that aren't engine-specific. Worth a separate ScummVM feasibility memo before engge2 is reconsidered.
4. **Is there a 2D-only Thimbleweed port effort by any third party?** Unknown. If one exists, it would be a much better starting point than engge2.

---

## References

- engge2 upstream: https://github.com/scemino/engge2
- engge (archived predecessor): https://github.com/scemino/engge
- Thimbleweed Park: https://thimbleweedpark.com/
- Nim language: https://nim-lang.org/
- ScummVM (alternative path forward): https://www.scummvm.org/
- doskutsu DOS-port playbook: `docs/ports/DOS-PORT-PLAYBOOK.md`
