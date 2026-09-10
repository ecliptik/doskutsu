# Multi-agent coordination: who actually drives the rig

Surfaced by a real live-fire campaign, not a hypothetical: a team-lead
spawned a `realhw`-style specialist per its charter, and that specialist
assumed rig access (MCP tool, CLI, whatever the environment provides)
would just be callable. It wasn't -- a freshly spawned agent does not
inherit the spawning session's rig connection. Two concrete failure modes
followed from that gap, and both are worth guarding against explicitly
rather than leaving each new campaign to rediscover them.

## Rig access is session-local, not inherited by a spawned subagent

Whatever mechanism a session uses to actually drive the rig (an MCP
server connection, a CLI with credentials configured, whatever this
environment provides) is tied to *that session*. A subagent spawned to
help with a task does not automatically get the same access, even if the
spawning session has it. Don't assume a spawned specialist can just call
the rig directly -- confirm it actually has access before handing it a
charter that depends on it.

**The intended pattern**: a single named session is the one that actually
drives the rig (by convention in this environment, a session dedicated to
the harness -- e.g. one literally named for it). Everyone else -- team-
lead, a spawned `realhw` specialist, anyone else who needs a real-hardware
action taken -- routes through that session via peer messaging: hand it
the populate/cell spec (what to stage, what to set/forbid, what to
collect), and receive the raw job records and logs back, per
`dos-hardware-validation`'s "hand off raw data, not a narrated summary"
rule. Don't try to reach the rig directly from a spawned subagent that
wasn't given that access explicitly.

**If a spawned agent discovers it lacks direct rig access**: stop and
report the blocker up (or route the request through peer messaging to
whichever session actually drives the rig), rather than fabricating a
result or improvising a workaround. A real session did exactly this and
it was the right call -- the failure mode this guards against is a
subagent inventing plausible-looking "results" rather than admitting it
couldn't actually run the thing.

## Exactly one coordinator drives the rig per campaign

If more than one caller can independently reach the session that drives
the rig, two callers can dispatch conflicting or duplicate work against
the *same physical machine* -- multiple reboots, overlapping runs,
whichever request happens to arrive first winning by accident rather than
by design. This actually happened: a spawned specialist discovered a
peer-messaging path it hadn't known about, and independently messaged the
same rig-driving session its own team-lead had already dispatched a full
runbook to. It was caught, but only because the rig-driving session
happened to pause on a human-confirmation gate before executing and
noticed two differently-addressed senders -- that's a lucky catch by a
human in the loop, not a structural guarantee, and nothing stops it from
going uncaught next time.

**The rule**: exactly one named coordinator per real-hardware campaign
should be the one dispatching to the rig-driving session. Every other
agent that needs something done on the rig during that campaign routes
its request *through* that coordinator rather than reaching the rig-
driving session independently. State this explicitly in whatever charter
or brief spawns specialists for real-hardware work (see
`shared/agents/team-lead.md` and `shared/agents/realhw.md`) -- don't
leave "who's allowed to talk to the rig right now" to be worked out ad
hoc mid-campaign.

If the rig-driving session itself receives a second live request against
the same rig while one is already in flight, the safe default is to hold
and flag rather than execute whichever arrived first -- treat a second
concurrent request as a coordination failure to surface, not routine
queueing.
