# Emulator tiering, and build-host tooling traps

## 86Box is conditional, not a default gate

DOSBox-X is the default automation gate for every port (see
`docs/testing.md`). 86Box is a *conditional* second opinion, not a second
default gate to run on every change -- reach for it specifically when the
question at hand is a chip-specific behavior that DOSBox-X's generic VBE
emulation can't exercise (a specific video chipset's register-level
quirk, for instance). Running 86Box on everything regardless of whether
the question calls for it wastes cycles without adding signal; running it
only when DOSBox-X's emulation genuinely can't answer the question is
the right scope.

## A source-only "emulator-only" diagnosis needs an empirical co-witness

If the working hypothesis is "this bug only reproduces in DOSBox-X and
won't happen on real hardware" (or the reverse), that conclusion needs an
*empirical* check before it's trusted for a ship decision -- not just a
source-level argument for why it should be emulator-specific. A confident
DOSBox-X-only diagnosis of a scheduler-starvation bug was refuted when
86Box reproduced the identical wedge -- the bug wasn't emulator-specific
at all, the initial diagnosis was a plausible-sounding story that hadn't
actually been checked against a second, independent implementation.
Running the same repro against 86Box (or, ultimately, real hardware) is
the check; a confident-sounding source argument is not a substitute for
it.

## Build-host tooling traps (X11/DOSBox-X specific)

Two gotchas that have nothing to do with the port under test and
everything to do with the build host's own X11/screenshot tooling, worth
knowing so they don't get misdiagnosed as a DOS-side bug:

- **A dead `DISPLAY=:0` reads exactly like a boot wedge.** If the X
  session behind a DOSBox-X window has died or isn't actually running,
  any screenshot/interaction attempt against it can fail in a way that's
  indistinguishable at a glance from the DOS guest itself being hung.
  Check the X session is actually alive before concluding the guest is
  wedged.
- **`scrot`'s auto-suffix defeats a naive "latest screenshot" glob.**
  `scrot`'s default `_NNN`-style auto-numbering on repeated captures to
  the same base name means a glob/sort that assumes the most recently
  *named* file is the most recent *capture* can pick up a stale frame
  instead -- sort by actual mtime, or use an explicit unique output path
  per capture, rather than trusting filename ordering.
