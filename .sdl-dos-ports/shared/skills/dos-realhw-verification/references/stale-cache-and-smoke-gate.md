# Stale-cache failure shapes, and why a clean smoke isn't proof

## "It's in the binary" is not "it ran"

`strings build/<game>.exe | grep <expected-symbol>` proves the symbol was
linked in. It says nothing about whether the code path containing it was
actually *executed* on the run you're looking at. Always pair a strings
check with a runtime signal (a log line, a banner field) that only
appears when the code genuinely ran -- see
`shared/skills/dos-hardware-validation/references/runmanifest-log.md` for
the structured-log side of this same discipline.

## Stale-build failure shapes

A rebuild that looks clean can still be serving old code. Two concrete,
documented shapes:

- **A stale cached object file survives a source patch landing.** An
  incremental build re-links against a `.o` that was compiled before a
  patch touched the source it came from, because the build system's
  staleness check didn't fire for some reason specific to how that patch
  touched the file (a timestamp that didn't actually move forward, a
  dependency edge the build graph doesn't track). The build reports
  success; the binary doesn't contain the patch.
- **An installed artifact survives a stage-directory clean.** Deleting a
  build stage's working directory (`rm -rf build/<stage>`) doesn't
  guarantee a rebuild, if the Makefile's target keys off an *installed*
  output artifact (e.g. a sysroot library) rather than a source
  prerequisite -- the installed artifact from the previous build is still
  there, so the target looks up-to-date and the stage gets skipped
  entirely.

These are two examples of a broader class (this port's own campaign
documented six distinct shapes total) -- the general lesson is: when in
doubt about whether a build actually picked up a change, do a full clean
build rather than trusting an incremental one's staleness detection. The
cost of an unnecessary clean build is minutes; the cost of shipping a
stale binary as if it were the fix is a wasted real-hardware run at best,
a wrong conclusion at worst.

## Smoke-gate discipline: a clean emulator run is not proof of hardware I/O

A DOSBox-X (or 86Box) smoke passing proves the emulator reached and
executed a given code path under its own emulated device behavior. It
does **not** prove that path correctly exercises real hardware I/O,
because an emulator's device model can silently behave differently than
the real device at exactly the boundary being tested.

Concrete case: a diagnostic probe passed 9/9 clean in DOSBox-X, then froze
real hardware on its very first run. The cause: DOSBox-X's emulated SB16
DSP-reset behaves differently on failure than the real chip does, and
that difference silently routed the probe around the one hardware I/O
sequence the probe existed to test -- the emulator never actually
exercised the code path under the condition that mattered, so a clean
smoke told you nothing about the thing you were trying to verify.

Apply the top-level question from `SKILL.md`: for a smoke test claiming
to verify hardware I/O behavior specifically, what would its *failure*
actually look like if the emulator's device model diverges from real
hardware at exactly the point under test? If the answer is "the emulator
can't produce that divergence at all," the smoke isn't testing what you
think it's testing, and a real-hardware run is required before trusting
the result.
