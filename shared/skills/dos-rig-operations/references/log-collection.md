# Log collection: arrival is not health

Collecting a log off the rig is not the same question as "is this a valid
log of what actually happened." Conflating the two produces false
confidence -- a collector that only checks "did the fetch error" can
report success on a log that's missing, stale, or truncated.

## Three failure shapes that look identical from "the fetch didn't error"

- **Never written.** The run never produced a log at all (crashed before
  logging, wrong path configured, feature that logs it isn't wired up).
- **Transfer failed silently.** The log exists on the rig but the pull
  didn't actually retrieve it (see `file-transfer.md`'s destination and
  verification discipline -- the same sha/size-verify rules apply to
  fetching a log as to staging a binary).
- **Written but incomplete.** The log exists and transferred, but the run
  was cut short (crash, hang, power event) partway through writing it, so
  what arrived is a truncated fragment that can look superficially normal.

Distinguish these by checking the rig's own directory listing for the
file (size, existence) as an independent fact, not by trusting "the
fetch command returned success" -- that only proves the transfer
mechanism worked, not that a complete, meaningful log exists to fetch.

## DOS 8.3 log-tag naming constraints

Log filenames are subject to the same 8.3, uppercase constraints as any
other DOS filename (see `file-transfer.md`) -- keep tags short and
DOS-safe from the start rather than discovering a naming collision or
silent truncation after a run.

## When there's no structured block at all

If a run produced a log but it doesn't have a structured, greppable
result block (a RUNMANIFEST-style block or equivalent -- see
`dos-hardware-validation/references/runmanifest-log.md`), fall back to
grepping for known-good indicator lines (a boot banner, a specific
milestone log line) as a **weaker substitute**, and label it as weaker
explicitly in whatever you report. An indicator-line match proves the
engine reached that point in execution; it does not carry the metrics or
completeness guarantees a structured block does. Don't silently upgrade
an indicator-line match to the same confidence level as a structured
result just because it's more convenient to report.
