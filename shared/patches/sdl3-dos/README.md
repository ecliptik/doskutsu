# patches/sdl3-dos/

Local-only patches against `libsdl-org/SDL`'s DOS backend (the commit
pinned by whichever port repo's `vendor/sources.manifest` this is
consumed through). Carried over from doskutsu's `patches/SDL/`, which
proved this backend on real hardware across VESA/Cirrus/S3 video, SB16/
OPL2/OPL3/GUS/WaveBlaster audio, gameport joystick, and DPMI timing.

**These patches are not upstreamed** (project policy inherited from
doskutsu — freedom to patch without waiting on upstream review is worth
more here than upstream alignment; the cost is that every upstream sync
is on us). See `docs/patch-conventions.md` in the sdl-dos-ports hub for the
full convention (lexical `NNNN-*.patch` ordering, `git format-patch`
output, one concern per patch, subject explains *why DOS needs this* not
*what changed*).

Subject lines use the `[SDL3-DOS]` tag (renamed from doskutsu's original
`[DOSKUTSU]` tag during extraction into this shared repo).

## `_disabled/`

Patches that were tried and reverted/disabled during doskutsu's
development — kept as documented prior art (what was tried, why it didn't
work or wasn't kept) rather than deleted. Not applied by
`scripts/apply-patches.sh` in its default configuration.

## Naming debt (resolved)

This series used to bake doskutsu's own name into several SDL hints,
log-tag env vars, and extern instrumentation symbols
(`SDL_HINT_DOSKUTSU_*`, `DOSKUTSU_LOG_TAG`, `doskutsu_audio_irq_count`,
the `Pixtone`-branded synth API, and similar) — surfaced as a real problem,
not just a cosmetic one, when the second port (dossage, porting Jason
Rohrer's *Passage*) had to define a nonsense-named `g_pixtone_active_count`
extern with no meaning in its own engine just to satisfy this backend's
ABI. All of it has since been renamed to neutral names: the
`SDL_HINT_DOSKUTSU_*` hint family is now `SDL_HINT_DOS_*`; log tags and
internal instrumentation are `DOS_PORT_*`/`_dos_port_*`; the Pixtone-branded
synth channel-pool API (`SDL_DOSPixtone*`, its header, the
`CategoryDOSPixtone`/`DOSPixtoneChannel` enums, and the
`SDL_HINT_DOSKUTSU_PIXTONE_IRQ_*` hints) is now `SDL_DOSSfxSynth*` /
`SDL_HINT_DOS_SFX_SYNTH_IRQ_*`.

The rename was pure text substitution across each patch file's full
content (hint `#define`s, string literals, comments, and commit-message
prose) — safe because every one of these identifiers is new
instrumentation this series introduces, never a rename of anything in
upstream SDL/SDL_mixer source, so it can't collide with real upstream
content. Two exceptions were deliberately left alone because they're
doskutsu's or NXEngine's own, correctly-named, and not shared/'s to
rename: doskutsu's real `include/doskutsu_config_keys.h` (cited by name
in one commit message) and NXEngine's actual `Pixtone.cpp`/`.h` synth
implementation and its own `docs/internal/*PIXTONE*-DESIGN.md` archive
docs (also cited by name in commit messages) — genuinely game-specific
and correctly named, unlike the shared-layer symbols this cleanup
targeted.

Consumers (doskutsu, dossage) update their own references to match in
their own repos/commits, bumping their `.sdl-dos-ports` submodule pin to
pick up the renamed symbols. Commit message bodies still retain
doskutsu-specific narrative (hardware names, wave/phase numbers) —
left in place because the underlying engineering rationale is still
valuable context for understanding *why* a patch exists, independent of
what the identifiers it names are called today.
