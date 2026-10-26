# Rebound-style review of PikasekoHash-256 (final design)

*Editorial note: this report was written in the development repository. Script and log names refer to
files there. Internal variant names are replaced by plain descriptions; FOLD 0 is the linear sum fold of the
previous finalization, FOLD 4 the nonlinear fold of the final design (SPEC.md, section 8), and FINAL n or
B16_FINAL = n means n final steps. Technical content and numbers are unchanged.*

Viewpoint: rebound attacks and their descendants (inbound/outbound, super-S-box, start-from-the-middle,
multi-inbound), as applied to Grøstl, ECHO, Whirlpool, SHAvite-3 and the short-input AES hashes. The one
question: using all degrees of freedom of the unkeyed hash, can we build a collision, semi-free-start (SFS)
collision, free-start collision, near-collision or distinguisher cheaper than generic, and over how many steps?

Design analysed: the final design as implemented in the development source (6 final steps, FOLD 4). Tags:
**measured** (run here on the real step, logs in this directory), **proven** (exact algebra or a closed solver
run), **argued** (reasoning, no complete experiment).

All scripts check themselves against the C reference: `pk.py` (step model, equal to
the portable step function (`pikaseko256_steps` here) for t = 0..7) and `pkhash.py` (the whole unkeyed hash, equal to
the portable unkeyed hash on 11 lengths from 0 to 520 bytes). `libpk.so` is the portable reference of the development code +
`consts.c` built unchanged. Independent check after the review: all five `collision_t*.txt` pairs collide in
one step on a fresh build of the current source.

## 0. Summary

| # | finding | tag |
|---|---|---|
| 1 | **One-step collisions are constructed in O(1).** Two 8192-bit states that differ in 54 bytes (four registers of one group) give identical outputs of one step. The construction is a two-round inbound through all four registers of the group: every one of its 90 active S-boxes is solved, none is paid for probabilistically. It worked at the first attempt for t = 1, 3, 5, 7, 9 and all four groups, including group 0 (step constant). | measured |
| 2 | **Free-start collisions on the full compression function and on the full finalization, in O(1).** If an absorption starts at an odd step, two different chaining values absorb any four packets into the same state. Two different states before the first final step give the same 256-bit digest after the 6 final steps and FOLD 4. | measured |
| 3 | The 36-S-box one-step local collision of the earlier review (Rijndael viewpoint) is **not realisable**: it needs σ_j = σ_i ⊕ σ_i2 (round-1 S-box output differences), and its supports violate this; the truncated MixColumns model does not see the relation. A more exact "pre-L" model (§2.2) has proven optima 57 (odd steps) and 76 (even steps), lower bounds on real one-step collisions. Its 57 point is itself infeasible (19 of 43 active bytes forced to zero). Cheapest realised: 90 S-boxes (odd steps). A 78-S-box characteristic exists, values over-constrained (§2.4). | proven / measured |
| 4 | **Half-invertibility.** For any pair keys k = r_i ⊕ r_j, k2 = r_i2 ⊕ r_j2 and any target outputs y_i, y_i2, eight inverse AES rounds per group give an input with exactly those outputs. Half of the state after a step (8 of 16 registers) is free to an attacker who controls the state before it. | measured |
| 5 | Super-S-box view: each register's two AES rounds form an AES super-box per column; its middle key is a raw xor of other registers' bytes in the same block, no rotation. Summed over the middle key, a fully active (Δin, Δout) has exactly 2^32 (x, K1) solutions with the real S-box (8 of 8 samples). The key-in-state structure does not hinder the inbound: round-2 conditions are met afterwards through the linear handles u = r_i ⊕ r_j and w = r_i2 ⊕ r_j2. | measured / argued |
| 6 | SFS collision (same chaining value, packet differences only), one absorption: **not found and not excluded**. Cannot happen in step t+1 (proven). Step t+2 needs two linked inbounds (steps t+1 and t+2). Degrees of freedom allow it by a wide margin (about 2400 bits of conditions against 8192 free bits); the obstacle is algorithmic. | proven (t+1) / argued |
| 7 | FOLD 4 adds no near-collision route to the full design. Its 256 output bits split into two 128-bit halves, each a function of half of the fold's input blocks, and it has a large additive kernel; both matter only with 0-1 final steps, where free-start collisions are already O(1). | argued |

No collision of the hash itself (fixed IV) is found. Every collision-type result is free-start or a building
block for SFS. But the free-start results are **complete and practical on the full-round compression function
and on the full finalization**. In the SHA-3-competition tradition this is a serious certificational break: it
removes any Merkle-Damgård-style argument that rests on collision resistance of the compression function.

## 1. Best attack table

| attack | target / steps | complexity | tag |
|---|---|---|---|
| one-step collision (internal, chosen state) | 1 step, odd t (rot2), any group | O(1), deterministic (first attempt in 5 of 5 runs) | measured |
| one-step collision, even t (rot1) | 1 step | proven LB 76 (pre-L model); 78-S-box characteristic exists; values not completed | proven / argued |
| free-start collision, compression function (CV before step t, absorption at t = 3, 9, 15, ...) | 3 steps + products, full | O(1) | measured |
| free-start collision, finalization | 6 final steps + FOLD 4, full | O(1) | measured |
| free-start collision, whole hash from a chosen intermediate state, any message suffix | all remaining steps | O(1) | measured (follows from the rows above) |
| SFS collision, absorption with 1 step after injection | 1 | impossible | proven |
| SFS collision, installed absorption (3 steps), chosen CV | 3 | open; needs a two-step inbound; DoF suffice | argued |
| SFS on reduced finals (0-6 final steps) | absorption + finals | same as above; 0 finals also accept a "pair-cancelling" state difference through the additive fold kernel | argued |
| near-collision of the output via FOLD 4 | fold | nothing beyond the free-start collisions; half-output structure only with ≤ 1 final step | argued |
| collision of the hash (fixed IV) | full | none | - |

Scripts: `sc90.py` (one-step collision), `demo_freestart.py` (free-start, both forms), `halfinv.py`, `ssbox.c`,
`sc_model.py` / `sc_milp.py` (pre-L model, CP-SAT / HiGHS), `sc_check36.py`, `sc_exact57.py`, `sc_build.py`
(generic exact-difference builder), `sc_tt.py` (78-family attempt). Collision pairs: `collision_t{1,3,5,7,9}_g*_s*.txt`
as 128 words X, X' and the common output Y; check with `pikaseko256_steps(X, t, 1)`. `sc90_more.log`
contains an early group-0 run made before the step-constant fix; the fixed run is `sc90_t3_g0.log`.
`sc_pattern_rot1.txt` and `sc_pattern_rot2.txt` were overwritten by the long solver runs;
`sc_pattern_rot2_57.txt` and `sc_pattern_rot1_76.txt` hold the first-run patterns.

## 2. One-step collisions

### 2.1 Exact collision equations

Per group of step t: inputs r_i, r_j, r_i2, r_j2 with input differences a, b, c, d. L = MC∘SR, F(x) = L(SB(x))
one keyless AES round. σ_X, τ_X are the S-box output differences of rounds 1 and 2 of register X, m_X its
round-2 input (mid) difference. ρ is the step's block rotation; L acts per block, so **L commutes with ρ**.
With P = a ⊕ b and Q = c ⊕ d:

    m_i = Lσ_i ⊕ Q,   m_i2 = Lσ_i2 ⊕ P,   m_j = Lσ_j ⊕ P ⊕ Q,   m_j2 = Lσ_j2 ⊕ P ⊕ Q
    y_i = Lτ_i ⊕ ρP,  y_i2 = Lτ_i2 ⊕ ρQ,  y_j = Lτ_j ⊕ ρQ,      y_j2 = Lτ_j2 ⊕ ρP

A collision (all y = 0) holds **iff**, with supports and DDT-compatibility as the only nonlinear constraints:

    τ_i = τ_j2 = τ := ρ L^-1 P,      τ_i2 = τ_j = τ' := ρ L^-1 Q
    m_i = L(σ_i ⊕ ρ^-1 τ'),  m_i2 = L(σ_i2 ⊕ ρ^-1 τ),  m_j = L(σ_j ⊕ ρ^-1(τ⊕τ')),  m_j2 = L(σ_j2 ⊕ ρ^-1(τ⊕τ'))
    supp m_i = supp m_j2 = supp τ,   supp m_i2 = supp m_j = supp τ'

Consequences (proven):
* P ≠ 0 and Q ≠ 0 (if c = d = 0 then τ' = 0, so τ = 0, so a = 0). Each pair {i, j} and {i2, j2} needs an
  active register. **A difference that enters a group through one register cannot vanish in that step**
  (the single-feed lemma of the earlier review (differential viewpoint), as an exact statement for collisions).
* If a group's round 2 is inactive in a block, then σ_j = σ_i ⊕ σ_i2 exactly in that block, and likewise
  σ_j2 = σ_i ⊕ σ_i2.

### 2.2 The 36-S-box trail of the earlier review (Rijndael viewpoint) is infeasible (proven)

That trail (yz_base.txt, phase 0, group {8, 9, 10, 11}) has round 2 inactive in block 0, so it needs
σ_9 = σ_8 ⊕ σ_10, hence supp(b) ⊆ supp(a) ∪ supp(c) = {6, 10} ∪ {13, 14}; the trail has supp(b) =
{4, 5, 7, 8, 9, 11}. Its input supports in the pre-L model: INFEASIBLE (`sc_check36.py`).

The truncated byte model (the designer's trail model (`milp.py`), tdiff.py, wt.py) treats Lσ_j = Lσ_i ⊕ Lσ_i2 as a 3-term
truncated xor of independent MixColumns outputs and never sees that L is invertible. Its bounds stay valid
lower bounds, but its optimal characteristics need not exist; for this key-in-state structure the gap is large.

**Pre-L model** (`sc_model.py`; MILP `sc_milp.py`): supports of a..d, τ, τ', pre-L mids μ_X = σ_X ⊕ ρ^-1(·),
P̄ = Lρ^-1τ and Q̄; each linear map once; the equalities of §2.1 built in. A valid relaxation, not exact.
* HiGHS optimum: **57 at odd steps (rot2), 76 at even steps (rot1)** (`sc_milp_rot2.log`, `sc_milp_rot1.log`):
  proven lower bounds on the active S-boxes of any one-step collision. CP-SAT found the same values.
* The exact GF(2^8) system of the 57 pattern has dimension 5 over its 43 unknown bytes; 19 required-active
  bytes are forced to zero (`sc_exact57.log`), so that pattern is infeasible. Other 57-patterns may exist.

Minimum: [57, 90] at odd steps, [76, 78] at even steps. For attacks the number does not matter (§2.3).

### 2.3 Construction (measured): the 90-S-box "disjoint" family, odd steps

X9 = {0,2,3,4,5,7,8,9,10} (byte index 4·column + row). Its first three columns met with the first three
diagonals form an L-closed pattern. With rot2 (blocks 0 and 2 swap), τ on X9 of block 0 and τ' on X9 of
block 2 solve §2.1 by

    a = (a0, 0)      b = (a0, Lτ)      c = (0, c2)      d = (Lτ', c2)        [(block 0, block 2)]

with σ_j = τ' in block 0 and σ_j2 = τ in block 2. Active S-boxes: round 1 9 + 18 + 9 + 18, round 2 4 × 9.

Value phase:
* The round-2 condition of r_i at byte p of block 0 is a condition on M(r_i)[p] ⊕ w[p], with
  w = r_i2 ⊕ r_j2 (⊕ step constant in group 0). r_i2 is inactive in block 0, so w[p] is a free linear handle.
* r_j2's condition involves u ⊕ w; u = r_i ⊕ r_j is pinned to about four values by the round-1 solutions, so
  r_j2 needs a small search per column.
* Block 2 likewise with roles swapped. All other bits are free.

Result (`sc90_t1_g1_s1.log`, `sc90_more.log`, `sc90_t3_g0.log`, `collision_*.txt`): t = 1, 3 (group 0, step
constant), 5, 7, 9; inputs differ in 54 bytes, C step outputs equal, first attempt each time. One solution
leaves about 2^7000 more for free.

This refutes the statement in the specification of that time that finding a collision inside a step "is a 1024-bit random-function
collision".

### 2.4 Even steps and the 78-S-box family (argued)

With rot1 the disjoint placement fails (all three offsets checked by hand). The family τ = τ' on X9 of one
block, ρ^-1τ in the next, works for both rotations:

    a = (Z, Lτ),  b = (Z, 0),  c = (Z', Lτ),  d = (Z', 0)

(Z, Z' of 6 bytes, two per diagonal): 78 S-boxes against the proven even-step bound 76. Differences found at
once (`sc_tt.py`); values over-constrained: about 216 bits of freedom against about 252 bits of conditions per
characteristic (about 2^-36), since kk = k ⊕ k2 leaves only two key forms per byte. Not completed
(`sc_tt_t3_s7.log`). Free-start attacks need only odd steps.

## 3. Free-start collisions (measured)

`demo_freestart.py`, `demo_freestart.log`:
* **Compression function.** CV = state before the absorption's first step t = 3m; for m odd the first step is
  odd. X ≠ X' from `collision_t3_g0_s2.txt` (or `collision_t9_g1_s5.txt`) as CVs: absorb_group(X, t, P) =
  absorb_group(X', t, P) for random packets P.
* **Finalization.** A message of at most 247 bytes ends its absorption at t = 3, so the first final step is
  odd; the two states give the same digest after 6 final steps and FOLD 4 (word 0 b0c1e3517b3661c2 for the
  t = 3 pair, 4884c7aa2582c4d0 for t = 9).

Free-start, not SFS: the CVs differ and are not shown reachable from the IV. No hash collision.

## 4. Super-S-box view and inbound phases

* Per block of one register: SB, SR, MC, ⊕K1, SB, SR, MC, ⊕K2 - one layer of four AES super-boxes with middle
  key K1 (raw xor of other group registers in the same block, not rotated: k2 for r_i, k for r_i2, kk for
  r_j, r_j2), then L and K2 = ρ(raw pair sum).
* `ssbox.c`, `ssbox.log`: for random fully active 4-byte (Δin, Δout), exactly 2^32 (x, K1) pairs in 8 of 8.
* State-derived keys help the inbound: where one register of the pair is inactive at a byte, the middle-key
  byte is a free linear handle. The only coupling is kk = k ⊕ k2. Self-keyed upper registers (kk contains r_j)
  cost a small per-column search.
* **Half-invertibility** (`halfinv.py`, 20/20 on the C step):

      r_i = F^-1(F^-1(y_i ⊕ ρk) ⊕ k2),   r_i2 = F^-1(F^-1(y_i2 ⊕ ρk2) ⊕ k),   r_j = r_i ⊕ k,   r_j2 = r_i2 ⊕ k2

  (k2 includes the step constant in group 0): 4096 bits of the state after a step chosen at will; the other
  4096 bits are functions of the 1024 key bits per group, also free.

## 5. Semi-free-start collision on one absorption

Differences enter only feed registers 0, 5, 10, 15 after step t: one register per group of step t+1.
* **Step t+1 cannot collide** (proven, §2.1).
* **Step t+2** (argued): an active group G1 of step t+1 has all four outputs active; they split into two pairs
  of different step-t+2 groups H; an H-group is fully active only if two G1 feeds (differing in bit β+2) are
  both active. Smallest SFS path: two active feeds.
* Shapes: a feed active on X9 of one block gives (i: 18, i2: 9, j: 9, j2: 18) in role i, (9, 9, 9, 18) in role
  j, (9, 18, 18, 9) in role i2, (9, 9, 18, 9) in role j2; one H-pair always matches the (9, 18) input of the
  90-family, the other not. Mixed solutions (e.g. with the 78-family shape (15, 6)) plausible, not searched.
* Products: an active feed makes Δq ≠ 0 unless the multiplicand has zero low halves in both messages: a
  256-bit value condition per active feed, cheap via half-invertibility.
* Degrees of freedom: about 630 + 1260 + 512 ≈ 2400 bits of conditions against 8192 free bits.
* **Estimate:** if only one step can be covered by an inbound, about 2^-1260, far beyond generic; if both, the
  SFS collision is probably practical. Undecided; the main open question.

## 6. Reduced finals

* Free-start: practical for any number of final steps plus the fold (§3).
* SFS on reduced finals = §5.
* With 0 final steps FOLD 4 also accepts state differences in its additive kernel (Σ Δ⁺r_i = 0,
  Δ⁺(r1 + r2) = 0, Δ⁺(r3 + r4) = 0), e.g. two registers outside {1..4} with opposite additive differences;
  the step makes such pairs naturally (y_i, y_j2 share ρP). Matters only for a 0-final variant.

## 7. FOLD 4 and near-collisions

* s_b = AES²(a_b; k1_b, k2_{b+1}); output lanes 0..1 = s_0 + s_2, lanes 2..3 = s_1 + s_3. Each 128-bit output
  half depends only on blocks {0, 2} of a and k1 and blocks {1, 3} of k2 (or the complement). Odd steps keep
  {0, 2}/{1, 3} closed, so for 0-1 final steps a difference confined to blocks {0, 2} outside r3, r4 gives a
  128-bit half-collision (argued). Irrelevant with 6 final steps.
* Low-weight output differences through one super-box per block (~2^-24 for a one-column output) are a
  property of two AES rounds, reachable only from sparse final-state differences, ruled out by six steps.
* FOLD 4 removes the linear zero-sum of the old fold and creates no new near-collision option.

## 8. Recommendations

1. **Make the step injective, preferably a permutation** (the two-half permutation-step variant, +28%). Non-injectivity
   gives O(1) free-start collisions of the full compression function and finalization and is the natural SFS
   target. With a permutation step every collision must come from the message injection.
2. Otherwise break the features the construction uses: the free linear middle-key handles (rotated or
   S-box-processed K1) and the kk = k ⊕ k2 sharing; the "noself" key map of the earlier review (Rijndael viewpoint). Re-run `sc90.py`-style
   constructions on any candidate.
3. **Specification**: remove "finding one is a 1024-bit random-function collision"; state that one-step collisions
   are constructive in O(1), free-start collisions are practical, and security rests on the fixed IV and SFS.
4. **Tooling**: truncated byte models overestimate what exists here; confirm characteristics by exact GF(2^8)
   algebra (`sc_build.py`) or real pairs; use the pre-L reformulation (36 -> 57/76 proven).
5. SFS margin is thinner than the diffusion numbers suggest (half-invertibility). Delayed products (operands read after step t+2) do not affect
   §2-§3. A fourth step per absorption or a permutation step closes the SFS question with margin.
6. Keep FOLD 4.

## 9. Limits

No SFS collision built; even-step one-step collisions not completed in values; exact one-step minimum open
([57, 90] odd, [76, 78] even); linear, related-key and keyed-mode questions outside this review.
