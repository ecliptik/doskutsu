# Input injection: landed vs. dropped

Highest bug-density area of the whole rig-operations surface -- two of
four real harness bugs found in one validation session traced here. The
core problem: PS/2 keyboard injection can report "no error" while the
target machine received something other than what was sent, and a
harness that treats "no error" as "fully and correctly typed" will act on
a false premise.

For the literal mechanics (how injection works, what the MCP/CLI surface
looks like), see vcctrl's own `vcctrl-rig-hazards` and the "Typing text
and pressing keys" section of `vcctrl-common-workflows`. What follows is
the hazard class every port session needs to know about, so a fresh
session doesn't rediscover it the hard way.

## BIOS keyboard-buffer truncation

A command typed past roughly 15 characters can get silently cut by the
BIOS keyboard buffer -- the injection call reports success, but only a
prefix of the string actually reached the guest. A harness that assumes
"no error from the inject call" means "the whole string arrived" will
then send the *next* command, which types over whatever unconsumed
remainder was still sitting in the buffer -- producing a garbled command
that looks like operator error rather than a truncation bug.

Don't try to detect or wait out the truncation -- there is no reliable
signal for "DOS finished consuming the last command" to engineer a
chunking or buffer-drain scheme around; neither primitive exists to build
one on. Instead, treat an ambiguous result from any per-item operation as
reason to stop the whole sequence rather than sending the next item, and
let the caller retry the operation as a whole from a known-clean state.
This is the same rule as the cross-cutting one in this skill's `SKILL.md`
(ambiguous result -> independent read, don't assume state) -- it's stated
concretely here because this is the specific hazard it was first learned
from, and because "just chunk it smaller" is a tempting-but-wrong
workaround that competes with the actual rule rather than following it.

## Caps Lock inverts case with no error signal

If Caps Lock is on (from a previous session, a stray keypress, whatever)
and it never gets checked, every subsequent typed string lands with
inverted case and no error anywhere in the chain -- the inject call
reports success, the string "arrives," it's just wrong. Verify lock-key
state (see below) before typing anything case-sensitive, and again if
there's any reason to think state might have changed.

## Lock-key (LED) reads go stale after a round trip

An LED-state read taken before a sequence of other operations is not
still valid after them -- re-verify immediately before it matters (e.g.
right before typing something case-sensitive), not once at the start of
a session and trusted for the rest of it.

## Boot-menu digit selection needs per-key proof

Selecting an option by typing a digit at a boot menu: a proven "5" (you
independently confirmed the machine responded to a "5" keypress) says
nothing about whether an *unproven* "3" typed moments earlier actually
landed. Each keystroke that matters for machine state needs its own
verification, not one verification standing in for a whole sequence.

## The generalizable rule

When a fetch, populate, or cell operation returns an ambiguous result,
don't assume the machine's state from the command's own reported
success/failure -- take an independent read (a screenshot, an LED check,
whatever's cheapest) before deciding whether to retry, re-verify, or
escalate. See the main `dos-rig-operations` `SKILL.md` for why this rule
is stated once at the top level rather than repeated per-hazard.
