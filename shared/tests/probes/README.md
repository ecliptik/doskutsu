# Hardware diagnostic probes

Standalone, DJGPP-only diagnostic tools for characterizing DOS-era
hardware: memory bandwidth, palette DAC behavior, VESA/Cirrus/S3 chip
identification, WaveBlaster/MPU-401/GUS detection, PC speaker, IRQ timing,
and related sound/video probes. Originally authored for doskutsu's
real-hardware QA campaign, carried here because the underlying hardware
questions they answer ("does this card's DAC accept 8-bit palette writes",
"what's this IRQ's actual rate on this CPU") are not Cave-Story-specific.

Reach for these when real-hardware behavior is unexplained by your port's
own code — they isolate whether a bug is in your port or in how a specific
card responds. See `.sdl-dos-ports/docs/testing.md` in a port repo.

## Most probes are fully standalone

Most files here build and run independently of any game engine or SDL
build — they talk to hardware directly (I/O ports, DPMI, DMA) or through
DJGPP's own runtime only.

## A few require project-specific instrumentation hooks

`audbuf.c` and `sdlprob1.c` (and a couple of others that reference the same
symbols) `extern` a small set of instrumentation counters —
`dos_port_audio_irq_count`, `dos_port_audio_irq_wall_us`,
`dos_port_audio_sfx_active_count` — that doskutsu's own patched SDL/engine
exports for exactly this purpose. These probes will not link against a
build that doesn't export those symbols under those names.

If your port wants to reuse these two probes, either:
- add equivalent instrumentation to your own SDL/engine patches exporting
  counters under the same names, or
- treat them as reference implementations to adapt rather than build as-is.

Everything else in this directory does not have this dependency.

## Naming

Probe-owned environment variables have been renamed from doskutsu's
originals to a neutral prefix (e.g. `DOS_PORT_LOG_TAG` -> `DOS_PORT_LOG_TAG`)
since these are read by the probe's own code and are safe to rename freely.
Comments referencing `SDL_HINT_DOS_*` hints still use that name because
they document behavior of doskutsu's *current* (not-yet-renamed) SDL patch
set — see `docs/patch-conventions.md`'s neutral-hint-naming convention for
what these should become once `shared/patches/sdl3-dos/` completes its own
rename pass, and update the corresponding probe comment at that time.

## Pruning

This library was carried over close to as-is from doskutsu's QA campaign
and includes some wave-specific one-off probes alongside genuinely
general-purpose ones. It has not yet been pruned or reorganized by
general-purpose-vs-one-off — treat file names and header comments as the
guide to what a given probe actually does before relying on it.
