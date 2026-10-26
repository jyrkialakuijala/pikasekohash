# Margin re-measurement of the final design (6 final steps, nonlinear fold)

*Editorial note: this report was written in the development repository. Script and log names refer to
files there. Internal variant names are replaced by plain descriptions; FOLD 0 is the linear sum fold of the
previous finalization, FOLD 4 the nonlinear fold of the final design (SPEC.md, section 8), and FINAL n or
B16_FINAL = n means n final steps. Technical content and numbers are unchanged.*

Scope: the previous finalization (4 final steps, FOLD 0 linear sum fold) against the final design (6 final steps, FOLD 4 = the sum through two
AES rounds keyed by r1+r2 and rot1(r3+r4)); absorption unchanged. Both folds measured side by side at
B16_FINAL = 0, 1, 2, 3 (plus 4 for FOLD 0, 6 for FOLD 4). Steps counted after the last injection: 2 + B16_FINAL.
Reused tools of the earlier review: diffbias.c, hashcube2.c, hashcube3.c, the cube.c method, the 256-bit battery. New: spec4.c
(independent model, bit-exact against every library build: logs/eqcheck.log), outcube.c, outcube_lib.c, anf.c,
deriv.c, foldtest.c, inner.c. "Measured" = run here (logs/); "argued" = reasoning consistent with measurements.

## Margin table

| distinguisher on the output | previous finalization (FOLD 0, 4 finals = 6 steps) | final design (FOLD 4, 6 finals = 8 steps) |
|---|---|---|
| per-bit output-difference bias (diffbias.c) | 2 steps (FINAL 0, phase 0 or 2; 2^12 pairs, chi2 3x10^4, 2 bits never flip); reproduced | **none at 2 steps**: all four phases, 2^16 pairs, mean chi2 255.6-257.0, max 322-359, no constant bit; none at 3-8 steps |
| first-order derivative on a top bit | 2 steps: every top-bit flip leaves 2 lane lsbs unchanged, P = 1 (2 messages) | none |
| low-order derivative / zero-sum on output lsbs (keyed) | **3 steps** (FINAL 1): third-order derivative 0 on one lane lsb, P = 1, from **8 messages** (new; the earlier review's 2^15 zero-sum reproduced 32/32) | **2 steps** (FINAL 0): second-order derivative 0 on one lane lsb, P = 1, from **4 messages** (44-56 of 276 variable pairs, all phases, keyed and unkeyed); 2^8 messages give all four lane lsbs |
| zero-sum / integral at the next step | 3 steps, as above | **none at 3 steps** (FINAL 1): cubes 2^8-2^32 (13 shapes), derivatives order 1-5 over all subsets of 24 top-bit variables |
| state integrals (absorption only) | full state 3 steps, one block 4 | unchanged (8192/8192 balanced at 3; one block 4161 vs 4096 at 4); do not reach the output beyond 2 steps with FOLD 4 |
| 256-bit battery (full) | FINAL 0 FAIL (14 tests); FINAL 1, 2 pass | FINAL 0 **PASS**, FINAL 1 **PASS**, FINAL 2 pass |
| **largest step count with any output distinguisher** | **3 of 6 (margin 3)** | **2 of 8 (margin 6)** |

State-level reach (4 steps, one block) is a property of the absorption: 4 steps of margin on the state with 8.

## 1. Output-bit bias (logs/run_db.log, logs/db_*.txt)

| build | steps | L (phase) | N pairs | mean chi2 | max chi2 | const bits | verdict |
|---|---:|---|---|---:|---:|---:|---|
| FOLD 0 FINAL 0 | 2 | 200 (0) / 712 (2) | 2^12 | 32036 / 3010 | 47347 / 46927 | 2 | broken (as in the earlier review) |
| FOLD 0 FINAL 0 | 2 | 456 (3) / 968 (1) | 2^14 / 2^12 | 255.9 / 256.5 | 342 / 363 | 0 | random |
| FOLD 4 FINAL 0 | 2 | 200 / 712 / 456 / 968 | 2^12, 2^16 | 255.6-257.0 | 322-359 | 0 | random |
| FOLD 0 FINAL 1 | 3 | all | 2^16 | 255.5-256.1 | 328-362 | 0 | random |
| FOLD 4 FINAL 1 | 3 | all; L=200 at 2^18 | 2^16, 2^18 | 255.4-256.3 | 335-361 | 0 | random |
| FOLD 0 / 4 FINAL 2 | 4 | 200 (+712) | 2^14 | 255.6-256.2 | 337-355 | 0 | random |
| FOLD 4 FINAL 3, 6 | 5, 8 | 200 | 2^14 | 255.5 / 256.3 | 330 / 349 | 0 | random |

Where the FINAL 0 bias goes (inner.c): the final-state sum keeps block 2 biased (chi2 8x10^6), s after the two
AES rounds too (8.5x10^5); output lanes 0, 1 = s block 0 + s block 2, and random s block 0 masks it (lane chi2
92, 60). At FINAL 1 blocks of r1+r2 and r3+r4 are still biased (chi2 7000-15000 at 2^22), output random.

## 2. Zero-sum and integral distinguishers

Cube shapes: `top:L[:K]` (bit 63 of lanes 0..L-1 of K packets; xor = add, works keyed), `randbits:D`,
`randbytes:D` (unkeyed, state known), the sets of the earlier review.

| final steps (steps) | FOLD 0 | FOLD 4 |
|---|---|---|
| 0 (2) | all four lsbs zero for every cube d = 8..24; 15-77 always-zero bits | all four lsbs zero for top:8 (d=24), top:7, top:8:2, **top:8:1 (d=8)**, 16/16 keyed; random for randbits/randbytes up to 2^24 |
| 1 (3) | all four lsbs zero for top:5, top:8, randbits >= 20, byte set | **nothing** up to 2^24 (16 settings); 2^28, 2^32 random |
| 2 (4) | nothing up to 2^32 | nothing up to 2^32 |
| 3 (5), 6 (8) | nothing | nothing |

Scripts of the earlier review unchanged: hashcube3 FOLD 4 FINAL 0: 7/32, 10/32 (1/4 expected given two deterministic lsbs);
FINAL 1/2/3/6: 0-4 of 32 (random 2). hashcube2 FOLD 4 FINAL 0/1/2: 2, 1, 0 of 8 (random 0.5).
**The bit-0 zero-sum at 1 final step disappears with FOLD 4.**

### The remaining 2-step distinguisher (FINAL 0, FOLD 4)

deriv.c: D_u D_v lsb(out lane) = 0 in all 256 samples for pairs of top-bit variables at lanes {l, l+4} (same
position in blocks b, b+2), keyed and unkeyed. anf.c: state after two steps has degree <= 3; s has degree <= 7;
every s lane misses one variable entirely; output lane l = s_l + s_{l+4}, whose lsb is linear in the two. Higher
bits get the carry and are random. Mechanism (argued): block-3 variables reach block 0 only at bit 63 through the
last step's outer key, xored into two registers of a group, and 2^63 + 2^63 = 0 cancels in the sum; k1 has no
rotation and k2 is xored after the last round, so nothing in the fold mixes blocks {0,2} with {1,3}. At FINAL 1
degrees are 7-8 (d=8) and 15-16 (d=16): random.

## 3. The fold itself (foldtest.c, random final states)

| property | FOLD 0 | FOLD 4 |
|---|---|---|
| dependence of lanes 0, 1 (incl. 64-bit hash) | blocks 0, 2 only, sparse | blocks 0, 2 of every register (flip 0.50); blocks 1, 3 of r3, r4 only linearly via k2 (0.02); blocks 1, 3 of the other 14 registers: none |
| lanes 2, 3 | blocks 1, 3 | symmetric |
| single-bit state difference | 128 output bits constant | 128 bits constant (other half); active half random |
| additive kernel r_i += d, r_j -= d | 120/120 pairs, P = 1 | **68/120**: (r1,r2), (r3,r4), the 66 pairs outside r1..r4, P = 1 for every d |
| same bit, same lane, two registers (xor) | all pairs P = 1/2 (bit 63: 1) | same 68 pairs |
| k2-only difference | trivial | P = 0.24 for b < 60, 1 at b = 63 |
| out lsb vs xor of 32 state lsbs | correlation 1 | 0.002 |
| max single-bit correlation | 0.0195 (noise 0.021) | 0.0198 |

Findings:
1. **Three depths**: the sum gets two AES rounds, k1 one, k2 none (xored after the last round, then added).
   A final-state difference keeping sum r_i and r1+r2 unchanged passes through xor/add only (k2-only
   differences collide with P = 1/4). Needs a structured final-state difference, which six final steps do not
   provide (argued).
2. **Exact additive kernel** of codimension 1536 (sum, r1+r2, r3+r4 unchanged) against 256 for FOLD 0; lowest
   weight 2 (bit 63, same lane, the 68 pairs). Inherent to a compressing sum fold.
3. No cancellation between the sum and its keys beyond item 2 (byte differences in (r1,r3), (r2,r4), (r0,r1),
   (r0,r3): 0 of 8192 collide).
4. **The fold does not mix block halves**: the 64-bit hash never sees blocks 1, 3 of 14 registers. Not
   exploitable with six final steps (argued); root of the 2-step distinguisher.
5. The output lsbs are xors of two s lsbs; no measurable linear correlation with state bits.

Candidate repair, model only (spec4.c FOLD 6; not in the library, not timed): feed s1 xor rot1(s1) into the
second AES round. Removes the FINAL 0 derivatives of order 1-3 (0/24, 0/276, 0/2024) and the top-bit zero-sums
2^8-2^24 (logs/deriv.log, logs/fold6.log). Cost: one block rotation and one xor.

## 4. Battery (full run of the 256-bit battery, tools/battery.c in this repository; logs/classic_*.txt)

| build | verdict | worst 32-bit z | notes |
|---|---|---|---|
| FOLD 4, FINAL 0 | PASS | -3.8 | seedsparse tail p 0.017 |
| FOLD 4, FINAL 1 | PASS | +3.1 | bic len 8 max corr 0.088 |
| FOLD 0, FINAL 0 | FAIL (14 tests) | +92606 | reference |

The battery passes FOLD 4 at 2 steps, where 4 messages break it: it is not sensitive to this structure.

## Files (in the development repository)

build.sh (obj/n{0,1,2,3,6}.o = -DFOLD=4 -DB16_FINAL=n, obj/o{0..4}.o = FOLD 0, obj/def.o defaults), spec4.c/.h,
eqcheck.c, diffbias.c, hashcube2.c, hashcube3.c, outcube.c, outcube_lib.c, anf.c, deriv.c, foldtest.c, inner.c,
run_db.sh, run_cube.sh, run_cube2.sh, run_anf.sh, run_deriv.sh, run_rest.sh, run_classic.sh; logs in logs/.
Flags: -O2 -fopenmp -mavx512f -mavx512bw -mavx512vl -mavx512vbmi -mvaes -maes, OMP_NUM_THREADS=4.

Limits: no division-property or MILP bound, so "nothing at 3 steps" on the output is empirical (cubes to 2^32,
derivatives to order 5); the section-2 mechanism is argued from ANF supports; timing not measured.
