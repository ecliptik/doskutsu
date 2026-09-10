# File transfer: packaging, staging, and verifying

Covers the whole path from "a build exists on the build host" to
"a verified copy is sitting where the game actually reads it on the rig,"
and the reverse for pulling results back. Useful standalone -- shipping a
build over for manual poking is a smaller, more common operation than a
full `dos-hardware-validation` campaign, and shouldn't require reading
the campaign material to do safely.

For the literal transfer mechanics (stage/send/fetch commands, FTP-over-
mTCP specifics), see vcctrl's own `vcctrl-rig-hazards` and
`vcctrl-common-workflows`, and this hub's `docs/hardware-testing.md` for
the interim CLI sequence (`vcctrl stage-file` / `send-file` / `get-file`
etc.).

## Package before you stage

Before a build ever reaches the rig:

- **Delta-only staging.** Send only what changed since the last known-good
  package, not a full re-bundle every time -- keeps transfer time down and
  makes it obvious from the transfer log what actually moved.
- **Per-run log-tag naming.** Name each run's log output with something
  that won't collide across repeated runs (a tag derived from run
  identity, not a fixed filename) -- a fixed name means the second run of
  a session silently overwrites the first run's evidence before it's been
  collected.
- **8.3-filename and CRLF discipline for anything that boots/executes on
  the DOS side** (BAT launchers especially) -- a name or line-ending that
  doesn't survive DOS's own file conventions doesn't fail loudly, it just
  doesn't do what you expected when the DOS side tries to run it.
- **sha256 + `strings` verify the binary before bundling it**, not just
  after it lands on the rig -- catching a bad build before it's staged is
  cheaper than discovering it after a transfer round-trip.
- **A minimal two-step handoff** (stage, then a single send/run command)
  keeps a manual real-hardware session low-friction for whoever's driving
  it interactively, rather than requiring a long checklist per transfer.

## Stage -> send -> verify

1. **Stage** the local binary/package, identified unambiguously (a build
   directory can contain more than one candidate if a previous build
   wasn't cleaned -- see `dos-realhw-verification`'s stale-cache material
   for why that happens).
2. **Send with an explicit destination directory.** A transfer
   capability's default destination is often a generic inbox (e.g.
   `C:\XFER\IN`), not wherever the game actually runs from. A naive send
   silently "succeeds" while dropping the file in the wrong place, and
   whatever runs next reads stale content from the real install directory
   without any error anywhere in the chain. Look up (or ask) the live
   install directory per port explicitly; never assume the transfer
   capability infers it.
3. **Verify with a sha256 round trip** -- hash the local file, hash what
   landed, compare. A size match alone is not sufficient; two different
   builds can coincidentally match in size.

## DOS 8.3 filename mapping

A long/mixed-case filename gets mapped to DOS's 8.3, all-caps convention
on the way over. The safe failure mode is a transfer that's **refused
before typing** when a name can't map cleanly -- the dangerous failure
mode is a silent, unexpected truncation that produces a file with a
different name than intended, which then doesn't match whatever the next
step expected to find. Prefer already-DOS-safe names for anything staged,
so there's no mapping ambiguity to reason about at all.

## Same-destination collision, generalized

Two transfer operations targeting the same destination path without a
collect/verify step in between is the same shape of bug regardless of
what's being transferred -- a log dump, a result file, or an arbitrary
staged binary. If operation B can start before operation A's output has
been fetched and confirmed, B can silently overwrite what A produced
before anyone got to look at it. Build a collect-then-verify barrier
between any two transfers that could plausibly target the same path,
the same way `dos-hardware-validation`'s RUNMANIFEST section guards
against a tick-tagged dump colliding with a previous run's.
