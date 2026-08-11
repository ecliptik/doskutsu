# POD83 picogus v167

**SUPERSEDED -- do not quote**

Binary v1.6.7 `101d95c16522`, a different binary from round 1. The GUS and AdLib cells are corrupted by the 0286 unsigned-wrap bug, fixed in 0288: 485+ ordinary frames were misread as load stalls. `GC4 G31 G51 G17` were clean at the time, but predate round 1's binary and are not comparable to it.

Round definitions, the canonical round-1 datasets, and how to read a cell log:
[`../README.md`](../README.md).
