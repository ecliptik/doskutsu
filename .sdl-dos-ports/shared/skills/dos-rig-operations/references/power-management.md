# Power management

Lower bug-density than input/capture/transfer, but the stakes when it's
wrong are physical rather than just "the run is invalid" -- so the rules
here are short and non-negotiable rather than judgment calls.

For the literal smart-plug scoping and command surface, see vcctrl's own
`vcctrl-rig-hazards`.

## The plug's scoping is visibility to a human, not a hardware interlock

A power-control system scoped so a given control can't hit the wrong
*plug* still can't verify which physical *machine* is actually seated on
the other end of that plug's cabling. Scoping prevents you from
accidentally power-cycling a different rig by picking the wrong control;
it does not prove the cabling matches what the config claims. Treat plug
scoping as a safety rail against picking the wrong control, not as proof
of which machine you're actually about to affect -- if there's ever real
doubt about what's physically wired to a given control, resolve that
before acting, don't infer it from the config alone.

## A power cycle should be unconditional regardless of reported state

A machine that's wedged (hung, frozen, unresponsive) can still report
"on" through whatever status channel exists -- a wedged machine isn't
necessarily a powered-off one. Don't skip a power cycle because the
reported state already looks like what you want; if the goal is a known-
clean boot, cycle power unconditionally rather than trusting a status
read that a hang can trivially falsify.

## Confirm-string discipline

Any power action that isn't trivially reversible (a full power-off, not
just a reboot) should require an explicit confirm step, not fire on the
first request -- the cost of an accidental power-off on a real machine
(interrupted writes, a card that needs a clean reboot to re-enumerate
correctly) is higher than the cost of one extra confirmation prompt.

## A working reset chord can take far longer to register than expected

A soft-reboot chord (Ctrl-Alt-Del or equivalent) that's genuinely going
to work can still take a long time to actually register on real hardware
-- measured as long as ~85 seconds on one real rig. A short timeout that
gives up before that window has elapsed and reports "the machine never
reset" is not evidence the chord was swallowed or the hardware is at
fault -- it's evidence the timeout was too impatient. Before concluding a
reset chord failed, confirm the wait window was actually long enough for
a real (not emulated) reboot cycle; don't let a short-timeout false
negative get diagnosed as a hardware or input-injection problem (see
`input-injection.md`'s landed-vs-dropped material for the general
version of this mistake).

## A PS/2 hang looks like a lock problem until you check

If a keyboard/mouse action against the rig appears to hang, the actual
cause can be either a genuine PS/2-level hardware hang *or* the session's
own input lock being held by something else entirely (see
`agent-coordination.md`) -- and from the caller's side, both look
identical: the action just doesn't complete. **Check whether the lock is
actually free before concluding it's a hardware fault.** Diagnosing a
lock conflict as a hardware hang leads to power-cycling a machine that
was never actually broken; diagnosing a real hardware hang as "just a
lock issue" leads to waiting indefinitely on something that will never
resolve itself.

**Recovery policy once the cause is genuinely ambiguous**: one power
cycle, then stop. Attempt a single power-cycle recovery; if that doesn't
resolve it, stop and escalate to a human rather than repeatedly retrying
power actions against a real machine on the assumption that the next
attempt will be the one that works. Repeated blind power-cycling against
unresolved ambiguity risks compounding whatever's actually wrong (a card
that needs a clean shutdown to re-enumerate, interrupted writes) rather
than fixing it.
