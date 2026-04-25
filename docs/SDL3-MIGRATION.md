# SDL3 Migration Architecture Brief

> **Why we're on this path:** see `PLAN.md § Plan Amendments / 2026-04-24 Path B` for the rationale and decision record (sdl2-compat rejection, Path 1 SDL3_mixer redesign acceptance, the tripwire ladder). This brief is the operational truth (which decisions, which invariants, which boundaries); PLAN.md is the why.

Architectural decisions for the SDL2 → SDL3 migration of NXEngine-evo (Path B per `PLAN.md § Fallback path: direct SDL3 migration`). Companion to `patches/nxengine-evo/README.md` (which documents the patch *layout*) and `PLAN.md § Phase 5` (which sketched the original patch list).

This brief governs commit hygiene and code structure for tasks #30 (audio refactor), #31 (mechanical renames), #32 (mixer/image lib swap), and #33 (NXEngine DJGPP patches). Read it before opening one of those PRs.

**Why this exists:** Path B (rejecting `sdl2-compat` for direct SDL3) and Path 1 within it (accepting SDL3_mixer's `MIX_*` redesign rather than building our own shim) are the two big architectural calls. Beyond those, four downstream decisions shape the patch set's structure. Pinning them now means tasks #30–#33 land with consistent code idioms instead of rediscovering them mid-PR.

---

## 1. Logical presentation: stay manual

**Decision:** Keep NXEngine-evo's existing manual scaling. Do **not** introduce `SDL_SetRenderLogicalPresentation` during the migration.

### Context

SDL3 introduced `SDL_SetRenderLogicalPresentation(renderer, w, h, mode)` to replace SDL2's `SDL_RenderSetLogicalSize` + `SDL_RenderSetIntegerScale` machinery. The API gives the renderer a logical resolution and a presentation mode (letterbox / overscan / integer scale / stretch), and the renderer handles scaling/centering internally.

NXEngine-evo never used the SDL2 logical-size API. `Renderer.cpp` (lines 211–241) maintains its own `gres_t` resolutions table keyed on `(display_w, display_h, render_w, render_h, scale_factor)`. Scaling is applied manually in draw paths through the `Renderer::scale` field and the `screenWidth` / `screenHeight` globals.

### Why not adopt during migration

Introducing `SDL_SetRenderLogicalPresentation` would replace the manual-scale code with a logical-presentation call. That's a **semantic change** (SDL3 picks the scaling mode; the resolutions table mostly becomes informational), bundled with a mechanical-rename pass (`0010-sdl3-mechanical-renames.patch`). Two changes in one patch series fail the "one concern per patch" rule and obscure regressions.

The 320x240 fullscreen lock (`0005-renderer-lock-320x240-fullscreen.patch`) sits cleanly on top of the existing manual-scale code. Adopting logical-presentation would force that patch to be redesigned — adding migration risk for marginal cleanliness gain.

### When to revisit

- **Phase 9** (perf tuning) — if logical-presentation's "integer scale" mode lets us drop a per-frame copy, that's a real perf benefit on Tier 2/3.
- **v2.0** (re-enable widescreen at runtime) — at that point logical-presentation gives us letterbox/overscan/stretch modes for free. NXEngine-evo's manual-scale code already understands aspect ratios in `gres_t`; the conversion is mechanical at that point.

Today, on a 320x240-locked fullscreen, the modes don't matter and the diff cost does. Defer.

### Code impact

None beyond the existing `0005` lock patch. `SDL_RenderSetLogicalSize` doesn't appear anywhere in NXEngine-evo source — there's nothing to rename, nothing to remove.

---

## 2. Blit path: keep texture-copy, hook for direct-surface

**Decision:** During the migration, keep the texture-copy path (`SDL_Surface` → `SDL_CreateTextureFromSurface` at load → `SDL_RenderTexture` per frame). **But** retain the `SDL_Surface*` reference alongside `SDL_Texture*` in `NXE::Graphics::Surface`, gated behind a build-time flag `NXE_DOS_DIRECT_BLIT` (default off).

### Context

NXEngine-evo's hot blit path:

1. Load PNG via `IMG_Load` → `SDL_Surface*`
2. `SDL_CreateTextureFromSurface(_renderer, image_scaled)` → `SDL_Texture*` (one-time at load — `Surface.cpp:46`)
3. `SDL_FreeSurface(image_scaled)` (current behavior — `Surface.cpp:53`)
4. `SDL_RenderCopy(_renderer, src->texture(), &srcrect, &dstrect)` per frame (~10 call sites across `Renderer.cpp`, `Sprites.cpp`, `Font.cpp`)

On SDL3-DOS's software renderer, the texture path adds a per-frame upload overhead that surface→surface bypass via `SDL_BlitSurface` would avoid. `PLAN.md § Phase 9 lever 2` plans this optimization. The question for this brief is whether to build in the structural hook for it during the migration.

### Why hook now (cheaply, gated off)

The structural change is keeping the `SDL_Surface*` retained in `NXE::Graphics::Surface` after `SDL_CreateTextureFromSurface`, instead of freeing it. That's a 5-line delta:

```cpp
// In Surface.h:
SDL_Surface *_surface;  // retained for NXE_DOS_DIRECT_BLIT path
SDL_Texture *_texture;

// In Surface.cpp::LoadImage:
_texture = SDL_CreateTextureFromSurface(renderer, image_scaled);
#ifdef NXE_DOS_DIRECT_BLIT
_surface = image_scaled;       // retain
#else
SDL_DestroySurface(image_scaled);  // free as before
#endif
```

Doing this during the migration means Phase 9 work is "switch the draw path under `#ifdef NXE_DOS_DIRECT_BLIT`" instead of "redesign `NXE::Graphics::Surface`'s lifecycle while reading hot-path stack traces." The plumbing lands cleanly inside `0010-sdl3-mechanical-renames.patch` (since we're touching `SDL_FreeSurface` → `SDL_DestroySurface` there anyway).

### Why off by default

Retaining the `SDL_Surface` costs `width × height × bytes_per_pixel` per loaded sprite sheet. For a 320×240 sheet at 32bpp, that's ~300 KB. At ~30 sheets across Quote variants, NPCs, tiles, and UI — roughly **9 MB working-set increase**.

| Tier | RAM | Direct-blit cost as % of usable | Verdict |
|---|---|---|---|
| 1 (PODP83 / 48 MB) | 48 MB | ~20% | Fine |
| 2 (486DX2-66 / 16 MB) | ~12 MB usable | ~75% | Material; flip carefully |
| 3 (486DX2-50 / 8 MB) | ~6 MB usable | exceeds budget | Cannot enable as-is |

Default-off keeps the migration's memory profile identical to upstream. The flag flips when perf data justifies it on a specific tier — and Tier 3 will additionally need the lazy-load sprite-sheet work from `PLAN.md § Phase 9 lever 5` before direct-blit becomes viable there.

### Code impact summary

- `NXE::Graphics::Surface` gains an `SDL_Surface* _surface` member, retained in `LoadImage`, freed in destructor — both under `#ifdef NXE_DOS_DIRECT_BLIT`.
- `Renderer::drawSurface` gains an `#ifdef NXE_DOS_DIRECT_BLIT` branch that calls `SDL_BlitSurface` against a destination retrieved from `SDL_GetRenderTarget` (or, if SDL3-DOS exposes it, the framebuffer surface directly via a `SDL_GetRenderProperties` query).
- All of the above lands in `0010-sdl3-mechanical-renames.patch`. Activating the flag is a Phase 9 change, not a migration change.

---

## 3. Audio refactor: co-ownership boundary

**Decision:** The audio refactor (#30) is co-owned by sdl-engine (SDL3 mechanics) and nxengine (synthesis correctness). The boundary is drawn by **what kind of bug surfaces a problem**, not by which file or which API.

### Context

Path B's audio refactor migrates two API surfaces simultaneously:

- **SDL_AudioCVT → SDL_AudioStream** (SDL3 core).
  - 3 sites in `Pixtone.cpp` (lines 372, 445, 508) for the S8/22050 mono → S16/SAMPLE_RATE stereo resample of pitch-shifted sound-effect variants.
- **Mix_*** → **MIX_*** (SDL3_mixer redesign).
  - `Pixtone.cpp` `Mix_QuickLoad_RAW` (lines 398, 473, 529) → `MIX_CreateAudioDecoder` over `SDL_IOFromMem` wrapping the synthesized PCM.
  - `Organya.cpp` `Mix_HookMusic` (lines 378, 400) → `MIX_Track` pull-callback for the music-synthesis loop.
  - `Ogg.cpp` `Mix_HookMusicFinished` (lines 123, 140) → MIX_Track equivalent for OGG soundtrack completion.
  - `SoundManager.cpp` `Mix_Init` + `Mix_OpenAudioDevice` / `Mix_OpenAudio` + `Mix_AllocateChannels` (lines 36, 43, 49, 55) → `MIX_CreateMixer` on an `SDL_AudioDeviceID`.

These two API surfaces touch the same files (Pixtone in particular). Drawing ownership by API, by file, or by patch all produce confusing splits — the same `.cpp` is owned by different engineers depending on which line you point at.

### Boundary by symptom

| Symptom | Owner | Why |
|---|---|---|
| `MIX_CreateMixer` returns null on DJGPP | sdl-engine | SDL3_mixer / SDL3-DOS audio backend integration |
| `SDL_AudioStream` push/pull semantics differ from `SDL_BuildAudioCVT` + `SDL_ConvertAudio` (flush behavior, partial output, format negotiation) | sdl-engine | SDL3 core audio mechanics |
| `MIX_Track` callback fires at wrong rate or with misaligned buffers | sdl-engine | Callback-contract correctness |
| Audio device format negotiation fails or returns unexpected format | sdl-engine | SDL3 audio device lifecycle |
| Pixtone pitch-shifted variants sound different post-migration (wrong pitch, distortion, aliasing) | nxengine | Resample correctness against synthesis intent |
| Organya music silent / wrong instrument / wrong tempo | nxengine | Synthesis output correctness |
| OGG playback completes correctly but next track doesn't start | nxengine | Playlist state machine |
| Save/load mid-music produces audio glitch | nxengine | Audio state save semantics |
| `Mix_AllocateChannels(64)` mapping to `MIX_Track` count is wrong | shared | Architectural mapping decision |

### Audio-capture gate

Before/after WAV capture from DOSBox-X at three checkpoints:

1. **Title screen** — Organya music, no SFX, baseline mix levels.
2. **Mimiga Village dialog** — text-blip Pixtone SFX, dialog-tone variations, music continues underneath.
3. **First Cave combat** — pitch-shifted Pixtone SFX (gun, hit, enemy death) layered over Organya music; this exercises both subsystems concurrently.

Capture before the `0013–0017` cluster lands; capture again after; play both back from the same DOSBox-X invocation for A/B. **Difference at perceptible threshold = nxengine triages first** (synthesis correctness), escalates to sdl-engine if mechanics suspected.

### Architectural invariant: synchronous only

**No `SDL_CreateThread`, no `std::thread`, no worker threads of any kind during this rework.**

SDL3-DOS uses a cooperative scheduler (verified in `CLAUDE.md § SDL3 DOS backend quirks`); introducing a worker thread to "fix" audio dropout would violate the constraint that lets the DOS backend work at all. nxengine's #27 audit verified zero threading primitives in NXEngine-evo source — that is a baseline we preserve, not a coincidence we tolerate.

If audio dropouts surface during the refactor, the fix lives in:

1. Mixer buffer sizing (SoundManager opens at 4096 frames currently; tune up if needed)
2. Callback efficiency (Pixtone synthesis hot loop, Organya wave-table lookup)
3. `PLAN.md § Phase 9 lever 1` (drop to 11025 mono — matches Cave Story's 2004 spec)

— not in threading. A contributor reaching for "let's offload Pixtone synthesis to a worker thread" is the well-intentioned change that breaks the cooperative-scheduler invariant. Block it at review.

### Tripwire

Per the architect ratification (#29 prior thread): **N=7 working days** for the audio refactor (#30) to pass the audio-capture gate. If unmet, escalate to Path 2 (custom shim over `SDL_AudioStream`) — **not** to `PLAN.md § Phase 9 lever 6` (drop to original SDL1.2 NXEngine), which stays reserved for actual SDL3-DOS-backend incompatibilities.

---

## 4. `// DOS-PORT:` annotation policy

**Decision:** Annotate *decisions*, not *substitutions*.

### Annotate

- **Structural decisions** — introducing the `NXE_DOS_DIRECT_BLIT` build flag, the audio-adapter abstraction in `SoundManager`, the manual-scaling-vs-logical-presentation choice, the `MIX_Track` callback wiring strategy.
- **`#ifdef NXE_DOS` ring-fences** — every block guarded by `NXE_DOS` should have a one-line comment above explaining what DOS constraint forced the guard. A future maintainer reading `#ifdef NXE_DOS` should not have to reverse-engineer *why*.
- **DJGPP-specific buffer-size math** — any `(size_t)` cast, `off_t` reasoning, `uint32_t`-in-place-of-`size_t` correction, `ftell` bounds checking. CLAUDE.md flags this as a critical rule; the annotation makes the audit trail readable.
- **`fopen(..., "rb")`** — every binary open that overrides DJGPP's text-mode default. CLAUDE.md flags this as a critical rule.
- **Stack / memory budget decisions** — where we're consciously sizing a buffer to fit DPMI / CWSDPMI memory budget vs upstream's larger allocation.

### Don't annotate

- **Mechanical SDL2→SDL3 renames** — `SDL_RenderCopy` → `SDL_RenderTexture`, every `SDL_FreeSurface` → `SDL_DestroySurface`, every event enum rename. These are migration noise; `git blame` plus the `0010`-series commit messages explain them.
- **Ordinary DJGPP-friendly code** that happens to also be portable.
- **Comments inside imported upstream patches** in `patches/SDL/`, `patches/SDL_mixer/`, etc. Those carry their own commit-message rationale. `// DOS-PORT:` is for code we own, per CLAUDE.md.

### Rationale

`grep -rn 'DOS-PORT:' src/` should enumerate the **decisions** a future maintainer needs to understand to reason about the port — not page through every renamed call. If the marker becomes noise, it stops being read. The signal-to-noise floor for a useful grep target is roughly one hit per architectural choice; we have maybe 15–25 such choices in the entire port, so that's the order-of-magnitude target.

### Format

Single-line comment immediately above the decision, prefixed `// DOS-PORT:` then a one-sentence reason. Multi-line rationale gets a multi-line block. Examples:

```cpp
// DOS-PORT: Retain SDL_Surface alongside SDL_Texture so Phase 9
// can switch to SDL_BlitSurface direct path. Memory cost is opt-in
// via NXE_DOS_DIRECT_BLIT build flag.
SDL_Surface *_surface;
```

```cpp
// DOS-PORT: DJGPP's fopen defaults to text mode; .pxm map files are
// binary and would have CRLF translation applied otherwise.
FILE *fp = fopen(path, "rb");
```

```cpp
// DOS-PORT: SDL3-DOS uses a cooperative scheduler. The audio refactor
// stays synchronous; do not add worker threads here without revisiting
// docs/SDL3-MIGRATION.md § 3.
```

---

## Cross-references

- `PLAN.md § Fallback path: direct SDL3 migration` — strategic context for Path B
- `PLAN.md § Phase 5` — the patch list this brief refines
- `PLAN.md § Phase 9` — when the deferred decisions (logical presentation, direct-blit on by default) get revisited
- `CLAUDE.md § Critical Rules` — DJGPP/DOS rules this brief operationalizes (no threads, `fopen "rb"`, `size_t` audits, no SIMD)
- `CLAUDE.md § SDL3 DOS backend quirks` — the cooperative-scheduler invariant cited in § 3
- `patches/README.md` — general patch convention
- `patches/nxengine-evo/README.md` — patch numbering and ordering for this migration
- Task #27 — the call-site audit grounding the migration sizing
- Task #30 — the audio refactor governed by § 3
- Task #31 — the mechanical-rename patch governed by § 1, § 2, § 4
- Task #32 — the mixer/image lib swap touching § 3
- Task #33 — the NXEngine DJGPP patches governed by § 4
