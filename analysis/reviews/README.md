# Review reports

Five reviews of the final design of PikasekoHash-256 version 0 (as defined in
[SPEC.md](../../SPEC.md)), made in October 2026. Each looks at the design
from one tradition of cryptanalysis. Scripts and logs named in the reports
are in the development repository, which is not part of this release, unless
stated otherwise. The reports can be read without them; the results that can
be checked with this repository are the one-step collision pairs
(`analysis/data/`, `tools/verify_step_collisions.c`), the statistical
battery (`tools/battery.c`) and the diffusion counts (`tools/diffusion.c`). The consolidated
reading is [ANALYSIS.md](../../ANALYSIS.md).

| report | viewpoint |
|---|---|
| [rerun.md](rerun.md) | Re-measurement of the output margins of the earlier review with the final finalization (0-3 final steps); properties of the fold itself. |
| [rebound.md](rebound.md) | Rebound, super-S-box and start-from-the-middle attacks; constructs one-step collisions and practical free-start collisions; the semi-free-start question. |
| [linear.md](linear.md) | Linear trails, cube testers and degree bounds, related keys, the products. |
| [redteam.md](redteam.md) | Hash flooding, key-independent collisions, near-collisions on reduced finalizations, side channels, API misuse. |
| [aesdesign.md](aesdesign.md) | AES-round hash and AEAD design tradition (Groestl, ECHO, Haraka, Areion, AEGIS, Rocca); local-collision and finalization bounds; comparison with a permutation-step variant. |

Some deployment findings in redteam.md (table S-box in the portable code,
lazy S-box initialization, a branch on the key value, unspecified byte
orders) refer to the development code and are fixed in this repository; see
ANALYSIS.md, section 12.

## The earlier review

An earlier review from eight viewpoints (differential trails, algebraic and
integral properties, mode of operation, practical attacks on reduced
versions, and the SHA-2, BLAKE3, Keccak and Rijndael design traditions)
examined the same absorption with the previous finalization (4 final steps
and a linear sum fold). It found no attack on the full hash and no
collision-type attack on a reduced version below 2^128; the collision
characteristics found cost 201-672 active S-boxes. Its main findings were:
the products can be steered in the unkeyed hash (all four forced to zero in
milliseconds); the truncated model admits a one-step local collision of 36
active S-boxes from the key-in-state structure (later shown infeasible, see
rebound.md); three steps per absorption equal the full-diffusion depth, so
the margin per absorption is thin; the linear fold left an exact zero-sum on
output bit 0 after one final step, and output distinguishers reached 3 of
the 6 steps after the last injection; and there was no domain separation.
It also found that the specification of that time named the wrong
multiplicand registers. These findings led to the six final steps and the
nonlinear fold of the final design, which the five reports here examine.
