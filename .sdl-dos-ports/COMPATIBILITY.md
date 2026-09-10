# Compatibility matrix

This is the richer, human-maintained companion to the summary table in
[`README.md`](README.md) and the machine-readable [`ports.yaml`](ports.yaml).
Update it as ports progress past `BACKLOG`.

| Project | Boots | Playable | 486DX2-66 | Pentium 75 | Notes |
|---|---|---|---|---|---|
| doskutsu | yes | yes | yes (~25 FPS) | yes (headroom) | reference port; see `gallery/doskutsu/` |
| Adventure Game Studio | — | — | — | — | unclaimed |
| VVVVVV | — | — | — | — | unclaimed |
| Meritous | — | — | — | — | unclaimed |
| OpenJazz | — | — | — | — | unclaimed |
| POWDER | — | — | — | — | unclaimed |
| Kobo Deluxe | — | — | — | — | unclaimed |
| Blob Wars: Metal Blob Solid | — | — | — | — | unclaimed |
| Passage | — | — | — | — | unclaimed |
| SuperTux 0.1.x | — | — | — | — | unclaimed |

Columns:
- **Boots**: executable starts, SDL initializes, reaches a title/menu.
- **Playable**: a representative slice of the actual game is playable
  start-to-finish for that slice (not necessarily the full game — see that
  port's own `dos_status` in `ports.yaml` for the finer-grained ladder).
- **486DX2-66 / Pentium 75**: playable at an acceptable frame rate on that
  reference machine, per `HARDWARE.md`. Only fill in a number backed by a
  real-hardware or vcctrl-captured measurement, not an emulator estimate.

This table intentionally lags `ports.yaml`'s `dos_status` field — a port can
be `RESEARCH` or `COMPILES` in `ports.yaml` well before it has anything
worth a row here.
