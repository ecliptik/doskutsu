<!--
Template: ports/<name>/LICENSE-REVIEW.md (or the port repo's own root).
Required before building any release-packaging automation for a port.
See sdl-dos-ports' docs/licensing.md for the policy this implements.
-->

# License review: <NAME>

## Engine source

License:
Redistribution terms (source):
Redistribution terms (compiled binary):
Notes:

## DOS-specific patches (this port's own `patches/<engine>/`)

License: <!-- typically inherits the engine's license -->
Notes:

## SDL3 / SDL3_mixer / SDL3_image + `shared/patches/`

License: zlib (see sdl-dos-ports' THIRD-PARTY.md)
Notes:

## Game assets

Graphics license:
Music license:
Font license:
Redistribution rights:
Can this port ship any game data at all? <!-- default assumption: no.
     Use the open-engine + user-supplied-data model unless you have
     explicit permission otherwise. -->

## Conclusion

<!-- What can actually be distributed, and under what conditions.
     If uncertain, default to: engine only, user supplies their own
     legally obtained game data. -->
