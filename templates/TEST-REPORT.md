<!--
Template: a QA/smoke-test report for a port, either a DOSBox-X/86Box
automation run or a real-hardware session. Use one per session/sweep.
-->

# Test report

## Session
Date:
Environment: <!-- DOSBox-X / 86Box / real hardware (rig + hardware ID) -->
Build commit:
`shared/` submodule pin:

## Checks

| Check | Result | Notes |
|---|---|---|
| Executable starts | | |
| SDL initializes | | |
| Video mode opens | | |
| Input event reaches engine | | |
| Audio device initializes | | |
| Asset loads | | |
| Reaches title/first room | | |
| Save file writes | | |
| Save file reloads | | |
| Clean exit | | |

## Deviations from expected behavior

<!-- anything that didn't match the last known-good run -->

## Artifacts

<!-- log paths, screenshot/video references (vcctrl shot/burst/record),
     sha256 of the tested binary -->
