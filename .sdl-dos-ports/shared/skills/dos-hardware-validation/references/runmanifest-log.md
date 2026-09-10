# RUNMANIFEST-style structured logs

A single grep-able block a DOS port's engine emits once per run, carrying
every metric a validation cell needs -- so collection is "grep the block"
instead of manual log archaeology or hand arithmetic across scattered log
lines. Doskutsu's implementation is the reference; this describes the
pattern so another port can add its own equivalent rather than copying
doskutsu's specific field names verbatim.

## Shape

```
[RUNMANIFEST-BEGIN]
environment=realhw
build_sha12=04388290f735
per_loop_fps=58.3
exit_code=0
critical_count=0
warn_count=1
stage_title_fps=61.0
stage_gameplay_fps=57.9
[RUNMANIFEST-END]
```

Exact field set is per-port -- add whatever a validation campaign for that
port actually needs (per-stage breakdowns, a PRNG seed if the run used
deterministic replay, a TAS-replay identifier). The properties that matter
are:

- **One block, emitted unconditionally at a defined point** (e.g. clean
  exit or a specific milestone), so collection doesn't need to guess
  whether the run got far enough to log anything.
- **Delimited with a literal `BEGIN`/`END` marker pair** so it's
  trivially greppable out of a log file that may also contain unrelated
  diagnostic noise.
- **`key=value` lines, one per line**, so extraction is a grep + split, not
  a parser.
- **`environment=` is mandatory** -- this is what lets a downstream
  consumer (a benchmark aggregator, a QA script) distinguish a DOSBox-X
  run from a real-hardware run without out-of-band bookkeeping. See the
  env-var note below for how this gets set.
- **`build_sha12=` (or equivalent) is mandatory** -- a content-based build
  fingerprint, not a `git describe`, so results stay attributable to an
  exact build even across rebuilds of identical source (see
  `shared/build/sdl3-dos.mk`'s note on why `SDL_REVISION` is pinned to a
  deterministic string rather than `git describe` for the same reason).

## Wiring `environment=` via this hub's shared/ hooks

This hub's `shared/tools/dosbox-launch.sh` sets `DOS_PORT_ENVIRONMENT` as
its neutral default. A port whose engine reads its own differently-named
var for this (doskutsu's engine patches read `DOS_PORT_ENVIRONMENT`
specifically -- see `shared/patches/sdl3-dos/README.md`'s "Known naming
debt" section for why that wasn't renamed) should pass
`LAUNCH_EXTRA_SET="<PORT>_ENVIRONMENT=dosbox-x"` (or the real-hardware
equivalent, set directly in the rig's launch profile rather than through
`dosbox-launch.sh`, which is DOSBox-X-specific) so the manifest's
`environment=` field reads correctly regardless of which var name the
engine itself was built to read. Don't leave this to the engine's
auto-detect default -- auto-detect defaulting to "realhw" when unset is a
correct fallback for actual real hardware but silently wrong for an
emulator run that forgot to set the override.

## Wiring `build_sha12=` via `RUNMANIFEST_FLAGS`

`shared/build/sdl3-dos.mk` deliberately does not bake a build-fingerprint
macro into its shared `CMAKE_COMMON` (see the migration plan's "Known
naming/wiring mismatches" #4 for the reasoning: only the engine stage
consumes it, so it doesn't belong in a flag block shared by all four
build stages). A port wanting this field composes its own
`CMAKE_CXX_FLAGS="$(NOSIMD_FLAGS) $(RUNMANIFEST_FLAGS)"` in its own
engine-stage recipe, where `RUNMANIFEST_FLAGS` is that port's own make
variable defining the fingerprint macro (e.g.
`-DPORT_BUILD_SHA12=$(shell ...)`), not something this shared fragment
exports for you.

## Extraction

Grep between the markers, split on `=`, done:

```sh
sed -n '/\[RUNMANIFEST-BEGIN\]/,/\[RUNMANIFEST-END\]/p' run.log \
  | grep '=' \
  | while IFS='=' read -r k v; do printf '%s\t%s\n' "$k" "$v"; done
```

Feed this into campaign aggregation (collect `per_loop_fps=` etc. across
cells for the ABBA verdict table -- see `abba-methodology.md`) and into
post-hoc confirmation of what a cell actually did (does `environment=`
match what this cell's arm expected?).

**Do not use a RUNMANIFEST field as a cell's `expect_log` gate.** The
block is emitted late -- typically at or after clean exit -- so anything
that disrupts normal teardown (a quit-path hang, a crash on the way out,
any exit-path bug unrelated to the lever under test) can suppress it on a
run that was otherwise completely correct. Gating live cell validity on
it turns an unrelated bug into a false "invalid cell." Use RUNMANIFEST for
metrics extraction and post-collect cross-checking; use an early,
reliably-emitted line (a boot banner, an init-time log line) for
`expect_log`. See `cell-protocol.md`'s `expect_log` section for the
concrete failure this caused and what to do instead when a manifest is
missing.
