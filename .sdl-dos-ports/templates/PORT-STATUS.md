<!--
Template: a new port repo's STATUS.md. Copy this to the port repo root
(or ports/<name>/STATUS.md if the port keeps multiple sub-targets) and
fill in every field. Keep it in sync with this hub's ports.yaml entry for
the same port.
-->

```yaml
name:
upstream:
upstream_revision:
upstream_license:
language:
original_sdl_version:
target_sdl_version:
graphics_api:
native_resolution:
color_depth:
audio_stack:
input_stack:
filesystem_dependencies:
threading:
networking:
external_dependencies:
copyrighted_assets:
dos_status:
dos_minimum_target:
dos_recommended_target:
maintainer:
```

`dos_status` values (same ladder as `ports.yaml` in the sdl-dos-ports hub):

```
BACKLOG
RESEARCH
BLOCKED
BOOTSTRAP
COMPILES
STARTS
TITLE_SCREEN
PLAYABLE
FULL_GAME
OPTIMIZING
RELEASE_READY
```

Difficulty score (also tracked in the hub's `ports.yaml`):

```
1 = trivial
2 = easy
3 = moderate
4 = difficult
5 = major engine-port effort
```
