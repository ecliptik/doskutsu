# qa-results -- raw real-hardware QA + benchmark data

Raw logs from real-hardware runs on the reference machine ("g2k"), kept so a
result can be re-examined later rather than re-measured. One directory per
run: `<date>-<cpu>-<sound card>/`.

Each cell log carries, without needing anything else:

- `inter_flip_ms=` per frame -- the framerate source (median + p95)
- `>> Entering stage N` -- the replayed route, for checking TAS fidelity
- the audio backend that actually initialised, vs what the CFG asked for
- `dur=` in the closing runmanifest line
- warnings the operator could not have seen on screen

`QA.TAS` is the input recording that drove the run. Keep it with the logs:
without it the routes and durations cannot be interpreted.

These are engine logs and an input recording -- no Cave Story assets.

## Runs

| Directory | Machine | Binary | Notes |
|---|---|---|---|
| `2026-08-06-tas-roundtrip` | POD-83 | 0282 diag | Record-vs-replay trace pair that found the TAS input-hold bug (nx 0283). `TR` = record, `TP` = replay. |
| `2026-08-06-POD83-picogus` | POD-83 + PicoGUS + S3 ViRGE | v1.6.5 `b421e5a52fea` | First benchmark round, 9 cells. 4 of 9 truncated the reel's last two transitions -- see CAMPAIGN-NOTES.md. |
