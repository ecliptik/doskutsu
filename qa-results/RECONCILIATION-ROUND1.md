# qa-analyze.sh vs ROUND1-CELLS.csv -- reconciliation (2026-08-11)

**Verdict: the two independent extractions agree on every measured value,
across all 89 round-1 cells. `extract-cells.sh` can be retired.**

Run:

    tools/qa-analyze.sh -f csv \
      qa-results/2026-08-10-POD83-picogus-v170-sfx \
      qa-results/2026-08-11-Am5x86-full \
      qa-results/2026-08-11-DX250-full \
      qa-results/2026-08-11-DX266-full \
      qa-results/2026-08-11-POD83-vibra-VB1 \
      qa-results/2026-08-11-POD83-video-lanes

89 rows out, 89 rows in the canonical CSV, same cell/dataset keys, no row
present in one and missing from the other.

## Field-by-field, all 89 rows

| column | disagreements |
|---|---|
| `backend` | 0 |
| `music_pump` | 0 |
| `flips` | 0 |
| `render_s` | 0 |
| `per_loop_fps` | 0 |
| `overhead_s` | 0 |
| `median_fps` | 0 |
| `route` | 0 |
| **`cpu`** | **89 -- see below, and it is not an error** |

This matters because those two extractions were written independently, by
different sessions, from the raw logs. Every published campaign number rests
on `per_loop_fps` and `overhead_s`, and they agree exactly, cell for cell.
That is the check the hand-written awk never got.

## The `cpu` column: a real epistemic difference, not a bug in either

The tool emits `-` for all 89 rows. The canonical CSV names a CPU.

Both are behaving correctly, and the difference is worth understanding rather
than patching away:

- `qa-analyze.sh` sources `cpu` from the **`.NFO` hardware manifest** that the
  sweep BATs now write beside the logs. Round-1 logs PREDATE those manifests
  (they were added 2026-08-11, after round 1 was captured), so there is no
  witness to read and the tool declines to invent one.
- `ROUND1-CELLS.csv` fills the column from the **log-tag prefix convention**
  (`G`=POD-83, `A`=Am5x86-133, `6`=486DX2-66, `5`=486DX2-50), which is the
  operator's declaration transcribed, not evidence from the run.

So the tool is the more conservative of the two: it will not state a CPU it
cannot witness. Nothing in a round-1 log proves which machine produced it --
that is the known gap (`POST-BENCHMARK-PLAN.md` Tier 1.7), now partly closed
by the `.NFO` manifests and due to be closed properly by the engine-side
CPU witness in the batched binary.

**Consequence for round 2:** the column populates automatically, from the
manifest, with no change to the tool. When the engine witness also lands,
the two can be cross-checked -- and a disagreement between the declared and
the measured CPU means the wrong sweep argument was passed and every number
in that run is mislabelled.

**Consequence for round 1:** the CPU attribution of the existing 89 cells
rests on the operator's declaration alone. It is very probably right; it is
simply not evidenced, and the tool is right not to pretend otherwise.

## Gate behaviour

The run exits 1, which is correct -- gates fired on the known-bad data the
index already excludes (the aborted Mach64 lane's truncated route, and the
pump cells whose `inter_flip` medians are invalid by construction). A clean
exit here would have meant the gates were not working.
