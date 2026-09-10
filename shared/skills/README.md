# Skill templates

Genericized Claude Code skills, same adoption model as `shared/agents/`
(see its own `README.md`): these are process templates distilled from real
campaign experience on one port, stripped of that port's specific numbers
and names, meant to be copied (or symlinked) into a port repo's own
`.claude/skills/` rather than invoked from inside this hub repo directly.

A skill here only helps if a port actually has it wired up locally --
this hub is a source of truth for the *content*, not a place these run
from day to day.

| Skill | What it covers | Reusable as-is vs. needs a per-port fill-in |
|---|---|---|
| `dos-hardware-validation/` | Running a real-hardware validation or A/B benchmark campaign on a vcctrl-controlled rig: design-before-run discipline, populate/verify, per-cell set/forbid/expect_log witnessing, RUNMANIFEST-driven metrics, ABBA methodology, bug-vs-workaround discipline, raw-data handoffs | Reusable close to as-is -- the harness protocol and methodology are port-agnostic. Fill in your own RUNMANIFEST field names/env-var names if you don't reuse `DOS_PORT_*` (see `docs/patch-conventions.md`'s naming-debt note), and your own vcctrl profile per `templates/vcctrl-profile.yaml.template` |
| `dos-rig-operations/` | Day-to-day rig mechanics independent of a full campaign: input-injection landed-vs-dropped detection, screen-capture evidence discipline, file transfer + packaging/handoff, log collection, power management, and multi-agent coordination (a spawned specialist doesn't inherit rig access; single-coordinator-per-campaign to prevent double-dispatch) | Reusable as-is -- mostly points at vcctrl's own `vcctrl-rig-hazards`/`vcctrl-common-workflows` for mechanics and adds the port-session framing/hazard classes on top |
| `dos-realhw-verification/` | Knowing a build/fix/diagnosis is actually correct on real hardware, not just apparently correct: two-witness build verification, stale-cache failure shapes, DOSBox-X/86Box tiering, build-host tooling traps, real-hardware-vs-emulator divergence debugging | Reusable as-is -- the epistemics (what a check's failure would look like) and failure-shape catalog are port-agnostic; the worked examples are illustrative, not something to copy literally |
| `dos-emulator-workflow/` | Local, no-rig-required DOSBox-X development: which of the three `shared/tools/dosbox-*.sh` scripts to reach for, the emulator-only escape hatches already baked into the shipped confs (and why they must never reach real hardware), the "necessary but not sufficient" pattern for a probe that can only partially answer a hardware question locally, and local-timing observation vs. an actual performance claim | Reusable as-is for the tooling/mechanics and the DOSBox-X/hardware boundary; the shipped `.conf` files' own `cycles=`/video/sound calibration is one port's reference-machine numbers, not a universal setting -- re-calibrate for your own port |

## Adopting a skill into a port repo

```sh
mkdir -p .claude/skills
ln -s ../.sdl-dos-ports/shared/skills/dos-hardware-validation .claude/skills/dos-hardware-validation
ln -s ../.sdl-dos-ports/shared/skills/dos-rig-operations .claude/skills/dos-rig-operations
ln -s ../.sdl-dos-ports/shared/skills/dos-realhw-verification .claude/skills/dos-realhw-verification
ln -s ../.sdl-dos-ports/shared/skills/dos-emulator-workflow .claude/skills/dos-emulator-workflow
```

Symlinking (rather than copying) keeps the skill in sync with this hub the
same way `patches/SDL` and the vendor scripts do -- see
`plans/DOSKUTSU-MIGRATION-PLAN.md` for the precedent and its one caveat:
anything that enumerates files by walking a symlinked directory needs `-L`
(GNU `find`) or the equivalent, or it silently sees nothing. Copying
instead of symlinking is fine too if a port wants to diverge (e.g. fill in
port-specific RUNMANIFEST field names inline rather than parameterizing) --
same tradeoff as the agent charter templates: copy-and-fill-in loses the
free sync, symlink-and-parameterize keeps it.

## Provenance

`dos-hardware-validation/` and `dos-rig-operations/` were distilled from
the vcctrl side of a real multi-hour real-hardware session validating
doskutsu on the vcctrl rig (Gateway 2000 / POD-83, ATI Mach64) during
doskutsu's `shared/` submodule migration -- an 8-cell ABBA fps campaign,
four real harness bugs found and fixed along the way, and vcctrl's own
prioritized read on what else in its rig-operations surface was worth
generalizing.

`dos-realhw-verification/` was distilled from doskutsu's side of the same
migration window -- its own build/QA and hardware-vs-emulator debugging
campaign history, including a fix that shipped twice on unconfirmed
static hypotheses before real-hardware markers refuted them, and a
multi-round investigation that initially misattributed a bug to a video
chipset before real-hardware surface dumps found the actual cause in the
port's own fast-path code.

Port-specific details (doskutsu's own env var names, the specific fps
numbers, specific bug tickets) were stripped the same way the agent
charter templates strip doskutsu's wave-by-wave findings -- see
`shared/agents/README.md`'s framing. The underlying discipline is what's
shared; the campaign history belongs to whichever port lived it.

`dos-emulator-workflow/` differs in provenance from the other three: it
wasn't distilled from a single incident, but extracted from gotchas
already documented inline in this hub's own `shared/tools/dosbox-*.sh`
comments and `shared/tests/dpmi-lfn-smoke/README.md` (the global-`pkill`
hazard, the `2>&1`-under-DOSBox-X caveat, the `SDL_DOS_AUDIO_SB_SKIP_DETECTION`
escape hatch, the "necessary but not sufficient" probe framing) --
material that was already real and already correct, just not yet
surfaced as something a session would actually consult before starting
local DOSBox-X work.
