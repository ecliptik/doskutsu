# Agent charter templates

These are genericized versions of the AI agent-team charters doskutsu
developed over its own porting campaign. Copy them into a port repo's
`.claude/agents/` and fill in the placeholders — they're process templates,
not doskutsu's actual campaign history (KPI numbers, wave-by-wave findings,
specific file line numbers have been stripped; those belonged to doskutsu's
own `docs/internal/`, not to a shared template).

| File | Role | Reusable as-is vs. needs a per-port fill-in |
|---|---|---|
| `team-lead.md` | Wave/phase coordinator, gate decisions, specialist arbitration | Fill in your specialist roster and doc paths |
| `sdl-engine.md` | Owns `shared/patches/sdl3-dos/` (or your port's local copy of it) | Reusable close to as-is — this patch set is shared |
| `ENGINE-SPECIALIST.md.template` | Owns your game engine's own patches | Fill in engine name, source paths, file-format specifics |
| `build-qa.md` | Cross-build + DOSBox-X smoke + visual A/B | Fill in your binary name and expected banner/log lines |
| `realhw.md` | Release packaging + real-hardware handoff | Fill in transfer mechanism (vcctrl by default — see `docs/hardware-testing.md`) and BAT/launcher naming |
| `probe-engineer.md` | Standalone DJGPP diagnostic probes | Reusable close to as-is |
| `PERF-CAMPAIGN.md.template` | Diagnostic/instrumentation coordination for a perf-focused wave | Only stand this up once you're actually doing profiling-driven optimization (see `docs/optimization.md`) |

## Conventions carried over from doskutsu that these templates assume

- **Narrow charters.** Each specialist has a lane; "what you do NOT do" is
  as load-bearing as the charter itself.
- **Team-lead doesn't do the work.** It sequences, arbitrates, and
  synthesizes; specialists author patches/builds/packages.
- **STOP-and-ack contract narrowing.** A specialist that decides to narrow
  or reinterpret its brief mid-task says so and gets acknowledgment before
  writing code, rather than silently substituting its own judgment.
- **Hypotheses, not predictions.** Performance claims are measured, not
  estimated — see `docs/optimization.md` and `docs/timing.md`.
- **Never contribute upstream.** Every specialist's patches are
  workspace-local — see `CLAUDE.md`'s "Never contribute upstream" section.
- **A project-local durable-lessons doc**, analogous to doskutsu's own
  `docs/internal/` findings docs, is worth standing up once a port has
  enough campaign history to need one — these templates don't assume one
  exists yet.
