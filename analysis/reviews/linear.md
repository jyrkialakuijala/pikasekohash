# PikasekoHash-256 (final design): linear trails, algebraic degree, related keys

*Editorial note: this report was written in the development repository. Script and log names refer to
files there. Internal variant names are replaced by plain descriptions; FOLD 0 is the linear sum fold of the
previous finalization, FOLD 4 the nonlinear fold of the final design (SPEC.md, section 8), and FINAL n or
B16_FINAL = n means n final steps. Technical content and numbers are unchanged.*

October 2026. Viewpoint: linear cryptanalysis (Matsui-style trails, correlation matrices), cube testers and
degree bounds (numeric mapping / division-property tradition), related-key differentials. Design: the final design
(absorption + 6 final steps + FOLD 4; defaults of the development source); reduced variants with `-DB16_FINAL=n`
(`build.sh`). Labels: **proven** (argument or closed MILP of a sound relaxation), **measured** (exact computation on
the real function over seeds), **argued**. `ref.py` (step from the specification) agrees with the step function (`pikaseko256_steps` here) at
t = 0, 1, 2, 3, 5.

## Summary table

| # | question | result | status |
|---|---|---|---|
| L1 | one step, nonzero input and output mask | min **5** active S-boxes at all phases (two AES rounds of one register; key bypasses free) | proven; value-feasible |
| L2 | mask dual of the local collision: zero input mask, nonzero output mask (biased output combination; exists because the step is not a permutation) | **60** at phases 0, 2 (optimum, value-feasible); **>= 50** at phases 1, 3 (model optimum, optimal patterns value-infeasible) | proven lower bounds |
| L3 | two steps, all masks nonzero | **>= 15** at phase 1 (and 3 by relabelling); phase 0 not completed | proven |
| L4 | N steps | pairs from an odd phase >= 15, other steps >= 5; a vanishing mask forces a zero-input step (>= 50) | proven |
| L5 | last injection -> output (2 + 6 steps + FOLD 4) | **>= 55** (5 + 5 + >= 40 + >= 5); single-trail correlation <= 2^-165 | proven (truncated model, counting) |
| L6 | direct MILP 2-3 steps / hash | HiGHS dual bounds 1-3 after 30 min, best trails 670-3260 | measured (solver limit) |
| A1 | cube tester, FOLD 4, 0 final steps | **distinguisher**: lsb of output lanes 2, 3 (sometimes 0, 1) zero-sum over 2^15 messages, keyed and unkeyed, no state knowledge, 16/16 | measured |
| A2 | 1, 2, 3 final steps | nothing (cubes 15-28 bits) | measured |
| A3 | degree upper bound (numeric mapping) | 1 step <= 1 (top bits); 2 steps <= 6 (15-24 top bits) or <= 13 (64 spread bits); 3 steps <= 42 for 48 spread bits: full state zero sum over 2^48; no output bit provably balanced even at 0 finals | proven |
| R1 | related-key difference cancelled by the first absorption? | every key difference activates all 16 registers before the first packet; step 0 >= 4 x 14 = **56** (<= 2^-336); >= 4 x 36 = **144** to confine to the feeds | proven (one-group MILPs closed) |
| R2 | related-key internal collision, first absorption, feeds free | >= 56; full MILP bound 14, best 836 | proven / measured |
| P | products | mask through bit i: corr <= 2^-1 (bit 0), 2^-3 (bits 3-4), 2^-6.1 (bit 9); degree of bits 0..9 = 2, 2, 4, 5, 6, 7, 8, 10, 12, 14 | measured (exhaustive) |

Margin: a 2^256-data linear distinguisher on one trail needs > 43 active S-boxes; six final steps give 55, four
would give 40. The cube tester covers 2 of 8 post-injection steps (linear fold: 3). Related keys > 2^256 at step 0.

## 1. Linear trails

### 1.1 Model (`lin_milp.py`)

Backward mask propagation. Group inputs a, b, c, d (i, j, i2, j2), F = MC.SR.SB, k = a^b, k2 = c^d^C, kk = k^k2:

    U_i = F(a)^k2,  Y_i = F(U_i)^rot(k)       U_i2 = F(c)^k,  Y_i2 = F(U_i2)^rot(k2)
    U_j = F(b)^kk,  Y_j = F(U_j)^rot(k2)      U_j2 = F(d)^kk, Y_j2 = F(U_j2)^rot(k)

With output masks alpha, round-2 S-box input masks beta, round-1 masks gamma:

    mask(k)  = beta_i2 ^ rot^T(alpha_i ^ alpha_j2) ^ beta_j ^ beta_j2
    mask(k2) = beta_i  ^ rot^T(alpha_i2 ^ alpha_j) ^ beta_j ^ beta_j2
    inputs: a: gamma_i ^ mask(k), b: gamma_j ^ mask(k), c: gamma_i2 ^ mask(k2), d: gamma_j2 ^ mask(k2)

Byte-level truncated: MC^T branch 5 per column; one cost per active S-box (|corr| <= 2^-3); truncated xors allow
every cancellation; step constant changes signs only; lane adds by a sound top-byte rule; products may absorb or
pass masks at no cost (sound relaxation; real cost in section 4).

Refinement TIGHT=1 (default, sound): the MC^T images of key masks are xors of images already in the model, and
each (mask, image) pair obeys the branch number. Without it the zero-input optimum was a spurious 20
(value-infeasible).

Value-level check (`gfcheck.py`): in the trace-dual representation MC^T is the GF(2^8) transpose; a truncated
trail is value-feasible iff no required-nonzero byte lies in the row space of its zero conditions (LAT not
checked).

### 1.2 Results (`milp1.log`, `milp3.log`, `lc_loop_*.log`)

* L1: 5 at every phase (4 bytes -> 1 S-box -> 1 byte -> 4 S-boxes in one register; masks through raw keys and
  rot(k) free). Differential minimum is 6, because a single byte keys its own register.
* L2: the 36-S-box local collision is a non-injectivity; its Fourier dual is a non-surjectivity. Phases 0, 2:
  optimum 60, value-feasible (152 nonzero requirements); phases 1, 3: model optimum 50, all optimal patterns
  tried (2 solves + 6 cut iterations) value-infeasible. Correlation <= 2^-180 (60) / 2^-150 (50).
* L3: one-step model plus a proven bound on the previous step (each active column of the in-between mask forces
  5 - |column| round-2 S-boxes plus one round-1 S-box): >= 15, optimal, at phases (1, 2) and (3, 0).
* L4, L5: fold with nonzero output mask >= 5; two absorption steps >= 5 each (pair bound not applied across the
  product xor); six finals = three pairs >= 40; total >= 55.

## 2. Algebraic degree and cube testers

### 2.1 Cube testers (`cubeout.c`, `cubes.log`, `cubes_big.log`, `cubes_first.log`)

Messages: G prefix groups, then 3 packets + padding (length 256 G + 247), cube in the last absorption. Keyed
cubes: bit 63 of a lane or a whole top byte (no state knowledge). Unkeyed: known state, feeds set by
P = X - r_f. Validated: FOLD 0 with 1 final step reproduces the four zero lsbs of the earlier review.

| steps after injection (B16_FINAL) | cubes | result |
|---|---|---|
| 2 (0) | top bit of lanes 0-4 of packets 0-2 (d = 15), unkeyed, keyed, 1 prefix group | **bits 128 and 192 zero 16/16** (random 0.004) |
| 2 (0) | 16 known-state random bits | 2 resp. 4 lsbs zero 16/16 |
| 2 (0) | 24-bit byte cubes | random |
| 3 (1) | all above + 28-bit cubes | random |
| 4 (2), 5 (3) | 15-24-bit cubes | random |

Mechanism at 2 steps (`foldprobe.c`, `foldconst.c`): blocks 1-3 of s2 are exact zero sums over the cube (386 of
512 bits) although every block of a, k1, s1, s2 varies; lanes 2, 3 = block 1 + block 3, lsb = xor of two
balanced bits. An integral property surviving two AES rounds, not low degree. Gone one step later.

### 2.2 Degree bounds (`degmap.py`, `degmap.log`)

Numeric mapping per byte with supports: top-bit cubes (15-24 bits): <= 1 after one step, <= 6 after two; with 15
bits 1008/1024 state bytes balanced after three. 48 bits in distinct bytes: <= 3-4, 13, 42 after 1, 2, 3 steps:
whole state a proven zero sum after three steps over 2^48. Through FOLD 4 the bound saturates at 0 finals. Not
tight (misses A1). No bit-level division-property MILP.

## 3. Related keys

IV r_i[l] = C_i[l] ^ rotl64(K[(l+i) mod 8], 4i): every register contains every key lane.
* Any nonzero key difference activates all 16 registers (`rk_kernel.py`, also 10^4 single-bit checks); the first
  packet arrives after step 0 and reaches 4 registers.
* Kernel keys dK[m] = rotr64(d0, 4m), d0 = (h, h) (2^32 - 1 values) give the same D in all registers: all step-0
  key differences vanish, each register goes through a fixed-key bijection, all 16 stay active with P = 1.
* Bounds (`rk_group.py`, `rk_milp.py`): one fully active group >= 14 in step 0; confining its output to the feed
  >= 36 (groups 0, 1); step 0 >= 56 (<= 2^-336); immediate cancellation >= 144 (<= 2^-864). Full first-absorption
  MILP: bound 14, best 836.
* Conclusion: message bits arrive only after the key difference passed 32 AES rounds; related-key collisions and
  differentials through step 0 are beyond 2^256 in this model. Redo if a key ever enters the packets.

## 4. Products

q = mul32(a, b) from the state after t+1, xored into r_g after t+2.
* Linear (`prodbits.py`): bit i depends on a_0..a_i, b_0..b_i; best |corr| 2^-1, 2^-2, 2^-2, 2^-3, 2^-3, 2^-3.7,
  2^-4.3, 2^-4.9, 2^-5.4, 2^-6.1 for i = 0..9. A free branch in the trail model; does not lower the per-step
  bounds; a bit-level model must charge >= 2^-1 per product bit used.
* Degree: bits 0..9 have degree 2, 2, 4, 5, 6, 7, 8, 10, 12, 14; the product bypasses one step, so low bits give
  <= 2 deg(t+1) against <= 49 deg(t+1) for the bypassed step: never raises the degree bound.
* Unkeyed: operands steerable (finding 1 of the earlier review), products are chosen constants.

## 5. Recommendations

1. Keep FOLD 4. The remaining 2-step property is an integral of blocks 1-3 through the fold; a fold mixing block
   0 into every lane (e.g. s + rot1(s)) would probably remove it (not measured).
2. Keep six final steps: four would give a linear bound of 40, below the 43 needed against 2^256 data.
3. Related keys: nothing to change.
4. Sharper multi-step bounds need a stronger solver (Gurobi, CP-SAT with symmetry breaking) on the TIGHT model or
   a super-box model.
5. Record in the designer's cryptanalysis notes: the step has biased output combinations (non-surjectivity), costing >= 50 S-boxes.

## Files

`ref.py`, `stepdump.c`, `lin_milp.py` (modes lc, steps, free, two, hash, fold; env TIGHT, GROUP, CHECK, SHOW),
`gfcheck.py`, `lc_loop.py` (`lc_loop_p3.log` loose, `lc_loop_p1_tight.log` tight), `runjobs.sh`, `jobs*.txt`,
`milp1.log`, `milp2.log` (6-final runs stopped), `milp3.log` (phase-0 run stopped), `cubeout.c`, `run_cubes.sh`,
`run_cubes_big.sh`, `cubes*.log`, `foldprobe.c/.log`, `foldconst.c/.log`, `degmap.py/.log`, `build.sh`,
`rk_milp.py` (`rk_step0.log`, `rk_absorb1.log`), `rk_group.py/.log`, `rk_kernel.py/.log`, `prodbits.py/.log`.
