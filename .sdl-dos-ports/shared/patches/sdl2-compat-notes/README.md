# patches/sdl2-compat/

> **Status: ABANDONED.** This directory was created during the original Phase 3
> plan, when the architecture was `NXEngine-evo → sdl2-compat → SDL3-DOS →
> DJGPP`. Path 3 (sdl2-compat as a runtime-loadable forwarding shim) turned
> out to be structurally incompatible with DJGPP/DOS for the reasons
> documented below. The project switched to Path B (direct SDL3 migration of
> NXEngine-evo) on 2026-04-24 — see `PLAN.md § Plan Amendments` for the full
> decision record. **No patches were authored or planned for this directory.**
>
> This README is retained as part of the audit trail: the architectural
> findings here are why we don't try sdl2-compat again later. The block that
> follows ("Why patches are needed here") describes the *original* problem
> we were trying to solve; the section after that ("Why we abandoned the
> approach entirely") explains why patches alone weren't enough.

See [`patches/README.md`](../README.md) for the project-wide patch convention
(numeric-prefix ordering, `git format-patch` source, `[DOSKUTSU]` subject
prefix, why-not-what commit messages, one-concern-per-patch rule).

## Why patches were originally needed here (historical)

sdl2-compat is architecturally a runtime-loadable shim. Its design assumption
is: an SDL2 application gets `libSDL2.so`/`SDL2.dll` at link time, and at boot
the shim `dlopen()`s `libSDL3.so`/`SDL3.dll` to forward calls to. The 830
SDL3 entry points (`src/sdl3_syms.h`) are stored as function pointers and
populated via `dlsym` lookups in `Init_SDL3` (`src/sdl2_compat.c:1092`).

This works on every platform sdl2-compat targets (Windows, macOS, Linux,
*BSD, Android via Bionic) because each has a real dynamic loader. On
**DJGPP/DOS** it doesn't:

1. **sdl2-compat's platform conditional has `#error Please define your
   platform.`** at `src/sdl2_compat.c:438` for non-Windows/Apple/Unix
   targets. DJGPP doesn't compile at all without a new branch. **This
   alone forces the patch.**
2. **DJGPP libc ships dlopen but it returns NULL at runtime.** It's there
   to back DJGPP's DXE format (a different dynamic-load mechanism that
   sdl2-compat doesn't speak). Even if we removed (1), `Loaded_SDL3` would
   be NULL after `LoadSDL3Library()`, `Init_SDL3` would set the failure
   string, and `SDL_Init` would fail at runtime with no useful diagnostic.
3. **Explicit > clever for downstream readers.** A `__DJGPP__` branch
   that says "static-link, no runtime loader" makes the architecture
   match the runtime, instead of relying on a libc quirk to silently
   no-op a code path.

(Earlier drafts argued binary bloat as a third reason. djgpp-dos measured
the relevant DJGPP libc objects — `dlopen.o + dxe3stat.o + dlerror.o +
dxeload.o + dlunregs.o + dlregsym.o = 5,016 bytes total`. That's noise
at our memory budget; not a patch driver. Keep it real.)

The "static-link mode" we add via patches sidesteps all of the above by
assigning the `SDL3_<fn>` function pointers directly to the linked-in
`SDL_<fn>` symbols at init time, with no runtime symbol lookup.

## Why we abandoned the approach entirely

The "what's needed" above (one `__DJGPP__`-gated patch with four touch
points to add a static-link mode) describes what the patch *would* have
done — but a deeper inspection during the patch-authoring step found that
the approach is structurally unworkable for reasons no patch can fix:

1. **Symbol collision at static link time.** Both `libSDL3.a` and the
   prospective `libSDL2.a` define `SDL_Init`, `SDL_Quit`, `SDL_CreateWindow`,
   etc. — every public SDL3 entry point. Verified via
   `nm libSDL3.a | grep ' T _SDL_'`. sdl2-compat's whole architecture
   depends on these living in different *binary namespaces* (separate `.so`
   files, dlopen'd at runtime). With static linking inside one archive, the
   linker has two definitions of every name and emits a multiple-definition
   error.
2. **`&SDL_<fn>` self-aliasing.** Even if (1) were resolved, in
   sdl2-compat's source `&SDL_Init` resolves at compile time to the LOCAL
   `SDL_Init` (the wrapper function defined in `sdl2_compat.c:140-153`,
   not SDL3's underlying symbol). The `IGNORE_THIS_VERSION_OF_SDL_<fn>`
   macros in `sdl3_include_wrapper.h` are load-bearing for compilation but
   don't expose SDL3's actual function addresses to C code at runtime. So
   `SDL3_Init = &SDL_Init` would point the SDL3 function-pointer slot back
   at sdl2-compat's wrapper, which calls `SDL3_Init(args)`, which is now
   the wrapper — **infinite recursion, stack overflow.**
3. **`SDL2COMPAT_STATIC` is misnamed for our purpose.** The CMake option is
   gated to Linux-only (`CMakeLists.txt:96-98` — `FATAL_ERROR` otherwise)
   AND it only changes the *output artifact form* (`.a` vs `.so`); it does
   not change the *runtime model* — the resulting `.a` still expects a
   real `dlopen` to load libSDL3 at runtime. There is no upstream
   "statically link SDL3 in" mode.

The architectural fix would have required either (a) renaming SDL3's
exports at build time (~all 1500 symbols) so they don't collide, or (b)
per-symbol `--wrap` linker flags for ~830 SDL3 symbols. Both add complexity
that doesn't go away. PLAN.md's documented fallback (direct SDL3 migration
of NXEngine-evo, dropping the sdl2-compat shim entirely) was structurally
cleaner. Path B was ratified on 2026-04-24.

## Macro choice rationale (retained for future readers)

If the architectural picture ever changes upstream and sdl2-compat-on-DOS
becomes viable, the macro decision was already made: use `__DJGPP__`
exclusively, not `__MSDOS__` (also defined by Watcom and Borland) and not
a project umbrella like `DOSKUTSU_DOS` (that's NXEngine-evo's `NXE_DOS`
territory, for project-policy decisions). SDL-side patches gate on the
C compiler/runtime, not the project.

## Upstream policy posture (retained)

No `CLAUDE.md`, `AGENTS.md`, or `CONTRIBUTING.md` was found in sdl2-compat
at SHA `91d36b8d` — no committed AI/LLM constraint at the repository level
(verified during Phase 3a / task #18). This finding stands; if a future
attempt needs it, downstream patches were unconstrained at the time of
this inspection. libsdl-org may still apply a project-level policy at PR
review time even when not committed.
