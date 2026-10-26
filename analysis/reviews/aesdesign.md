# PikasekoHash-256 (final design) from the AES-round hash / AEAD state-update point of view

*Editorial note: this report was written in the development repository. Script and log names refer to
files there. Internal variant names are replaced by plain descriptions; FOLD 0 is the linear sum fold of the
previous finalization, FOLD 4 the nonlinear fold of the final design (SPEC.md, section 8), and FINAL n or
B16_FINAL = n means n final steps. Technical content and numbers are unchanged.*

Viewpoint: Grøstl, ECHO, Haraka v2, Areion, AEGIS and Rocca. All numbers come from the byte-level truncated model
of the earlier review's `tdiff.py` (differential viewpoint) (one S-box per active byte entering an AES round, MixColumns branch
number 5, truncated xor, the step constant cancels). "Proven" = the solver closed the gap in this model, so it
bounds single characteristics with AES S-box DP <= 2^-6; clustering ignored. Labels: [measured] solver output;
[proven] closed solve or composition of closed solves; [argued]; [from memory] recalled literature, not checked.
<= 4 threads.

Note added when saving (from the rebound review, rebound.md): truncated-model optima need not be realisable for this
key-in-state step (the 36-S-box one-step pattern of the earlier review is infeasible), but the bounds stay valid lower bounds.
The S-box counts translate into probabilities only for steps the attacker does not control by message
modification; with a chosen state, ~90 S-boxes of one step are solved in O(1).

## 0. Findings, most important first

1. **Local-collision bounds of the final design [proven]** (difference injected in absorption n, cancelled by absorption n+1):
   >= 100 active S-boxes at every phase in the strict product model (earlier review: 59-80); >= 64 in any product model
   (steered, zeroed or cancelling), since the steps t+1 + t+2 bound does not involve the products; >= 36 in step
   t+3 alone (earlier review: 21) - with t+1 and t+2 free still 2^-216; with only t+1 free >= 42. Internal collisions
   over any number of absorptions (strict): >= 100.
2. **The permutation-step variant lowers every one of these bounds [proven]**: local collision >= 65
   (phases 0/2), >= 45 (1/3); last step alone >= 20 (2^-120, below birthday); one active feed costs 10 in its
   first step against 27. Its lower half is keyed only by the upper half, so a one-register output is cheap.
3. **The permutation-step variant's absorption is not a permutation either [argued]**: with delayed products (operands read after step t+2) the consuming step is
   y = P(x) xor E(Q(x)), Q computed from x - not a bijection in general. A Feistel placement
   y = P(x xor E(Q(x))) is invertible (targets {3,6,9,12} disjoint from operands) but puts the multiply on the
   critical path.
4. **AEGIS/Rocca in the same model [measured; update functions from memory]**: AEGIS-128L 51 (T = 5), AEGIS-128
   56 (T = 6), AEGIS-256 70 (T = 7), Rocca 25 (T = 5 optimal). The "5 active S-boxes" recollection is the AES
   2-round branch number, not an AEGIS local collision. Those designs bound keyed forgery (secret state);
   PikasekoHash's distinguishing property is three message-free steps between injections, giving the proven
   last-step bound 36.
5. **Finalization [proven/argued]**: six final steps carry >= 81 S-boxes (earlier review: B4 >= 54 + B2 >= 27, 2^-486)
   for any trail not ending in an internal collision. Six is adequate; keyed-sum fold right for a
   non-invertible state; truncation better only with a real permutation and a sponge claim.

## 1. Local collisions, AEGIS/Rocca style

### 1.1 Method

The direct model of the earlier review (`tdiff.py lc 3 P`, strict, `--costfrom 3`) did not move with <= 2 threads (CP-SAT 600 s:
bound 0, best 102; HiGHS 600 s: bound 1; logs `t_*.log`). Split into pieces HiGHS closes on one thread; C1, C2, C3
= S-boxes in steps t+1, t+2, t+3.

* **Last-step lemma** (`lastgroup.py`): groups independent, feeds a transversal, so each group of step t+3 must
  confine its output to its own feed register. Minimum over nonzero group input [proven]: base step **36** for
  every role at both rotation parities (`lastgroup_base_t0.log`, `lastgroup_base_t3.log`,
  `lastgroup_base_t0_r0long.log`); permutation step **20** (i, i2) and **31** (j, j2) (`lastgroup_perm_t*.log`);
  with the delayed-product operand constraint unchanged at 20 (i/i2), j/j2 31 or infeasible (`lastgroup_perm_pd3.log`).
* **No vanishing inside an absorption** (`vanish_r2.log`) [proven]: strict model, zero state after t+2
  (products included) infeasible at all phases. Without products (`vanish_r2_noprod.log`): feasible, best
  576-634, bound 12-13 (open).
* **First-two-steps lemma in2** (`in2_*.log`): min C1 + C2 from feed-only input, no end condition, no products;
  holds in every product model. Base: **64** (optimal, phases 0, 1; 2, 3 by phase isomorphism). Permutation:
  **45** at phase 0 (and 2); phase 1 (3) bound 25, best 45 after 5400 s. C2 alone with t+1 free: not closed
  (base bound 6, best 190; perm bound 5, best 33).
* **Single-feed first-step lemma** (`inlemma_perm.log`; base from the earlier review's `lclemma.log`): base 27 (j, j2) and
  32 (i, i2); permutation 10 (j, j2) and 28 (i, i2).

Composition [proven in the model]: total >= in2 + 36 (base) or in2 + 20 (permutation).

| design | strict products | any product behaviour | step t+3 alone | best characteristic found |
|---|---|---|---|---|
| final design | **>= 100** | **>= 64** | **>= 36** (2^-216) | 438-672 (earlier review) |
| delayed-product timing on the base step | >= 100 | >= 64 | >= 36 | - |
| permutation-step variant | >= 65 (0/2), >= 45 (1/3) | not computed | **>= 20** (2^-120) | 352 (phase 0, CP-SAT) |

Multi-absorption internal collisions (final design, strict): first absorption pays >= 64; cannot close before t+3;
the closing step costs >= 36 per active group: >= 100. For the permutation-step variant: >= in2 + 20.

### 1.2 AEGIS / Rocca (`aegis_lc.py`, `aegis_rocca.log`, `aegis128l.log`, `rocca6_long.log`)

Zero state difference, message difference at step 0, free later message differences, zero state after T
updates. Updates from memory: AEGIS S'_0 = R(S_last, S_0 xor M), S'_k = R(S_{k-1}, S_k) (128L: second block at
S_4); Rocca: four AES rounds and four xors per update, X0 into S0, X1 into S4.

| design | state bits | msg bits / update | AES block-rounds per msg byte | shortest LC | min active S-boxes |
|---|---|---|---|---|---|
| AEGIS-128 | 640 | 128 | 0.31 | T = 6 | 56 (T = 6, 7 optimal) |
| AEGIS-128L | 1024 | 256 | 0.25 | T = 5 | 51 (T = 5, 6 optimal; T = 7 best 51) |
| AEGIS-256 | 768 | 128 | 0.375 | T = 7 | 70 |
| Rocca | 1024 | 256 | 0.125 | T = 4 (36) | 25 (T = 5 optimal); T = 6, 7 best 27, open |
| PikasekoHash (final design) | 8192 | 2048 / absorption | 1.5 | one absorption | >= 100 strict, >= 64 any products |

[from memory] Published forgery bounds ~26 (AEGIS), mid-twenties (Rocca); check before quoting. With a known key
those designs are open to message modification (conditions of a 51-S-box characteristic ~306 bits vs 1280
message bits); [from memory] known-key / key-commitment state-collision attacks exist. PikasekoHash's three
uncontrolled steps per injection are what AEGIS-style updates lack.

## 2. Finalization against Haraka, Areion and Grøstl

* Differential margin [proven]: output collision without internal collision needs a nonzero difference through
  6 final steps: >= B4 + B2 = 54 + 27 = 81 (2^-486), even with the two post-injection steps attacker-controlled.
  Four final steps gave >= 54.
* Distinguisher margin: integral/bias reach 3 steps (4 on part of a block); 8 steps follow the last injection.
  Six adequate; not below 4 final steps (2 + 4 = two diffusion depths).
* Permutation-step variant: no computed B2/B4 for the permutation step (earlier review, Rijndael viewpoint, Feistel step: 1-step min 4, 2-step best 43, bound 12);
  its finalization bound is unknown and probably lower.
* Sum fold vs truncation [argued]: Grøstl trunc(P(x) xor x); Haraka v2 / Areion feed-forward and fixed 256-bit
  selection; ECHO BigFinal xors columns (a sum fold) [from memory]. Feed-forward exists because P is invertible;
  here the 8192 -> 256 compression suffices. Lane adds break xor-linearity; the AES rounds remove the linear
  zero-sum. A truncation is the squeezing function sponge proofs assume, worth it only with a real permutation.
  Keep FOLD 4 for the final design; with a permutation design use a Haraka-style truncation (one lane from each block,
  registers from different groups).

## 3. The final design versus the permutation-step variant as the basis for a published claim

| | final design | permutation-step variant |
|---|---|---|
| local collision, strict | >= 100 | >= 65 / >= 45 |
| local collision, any products | >= 64 | not computed |
| last step, two steps free | >= 36 (2^-216) | >= 20 (2^-120) |
| only t+1 free | >= 42 | >= 24 |
| multi-absorption internal collision | >= 100 | >= in2 + 20 |
| vanishing difference inside a step | possible, not reachable within one absorption (strict) | impossible |
| final 6 steps | >= 81 | unknown |
| sponge / permutation argument | no | not as built |
| product steering (unkeyed) | yes; >= 64 ignores products | removed by delayed products [argued] |

Opinion [argued]: the permutation-step variant costs +28% for structural cleanliness, not bounds; its permutation property is lost by
the delayed-product timing. For a published claim: the final design with delayed products once the window-test artifact of that combination at 2
final steps is resolved, stating >= 100 strict, >= 64 product-agnostic, >= 36 last step. If a permutation is required,
redesign it first (raise its 20 and 10 costs above 36 and 27, Feistel-placed products, recompute B2/B4).

## 4. Recommendations

1. The designer's cryptanalysis notes: replace "last step >= 21" with the composed bounds (>= 100 strict, >= 64 product-agnostic,
   >= 36 last step, >= 100 multi-absorption). The lemmas are short HiGHS solves (`lastgroup.py`,
   `lcmodel.py --R 2 --noend`). (Done: ANALYSIS.md, section 9.)
2. Do not call the permutation-step variant "conservative" in trail terms: "invertible step, lower proven bounds".
3. If the permutation-step variant is kept, Feistel-place the products or document that the absorption is not a bijection.
4. Keep 6 final steps and FOLD 4 for the final design.
5. Open: in2 for the permutation step at phases 1/3; product-agnostic bound for the permutation-step variant; B2/B4 of the permutation step;
   a C check of the permutation-step model (transcribed from the development source); Rocca T >= 6.

## 5. Files (in the development repository)

Scripts: `lcmodel.py` (generalised LC model: perm step, product timing, --costfrom, --endzero, --noend),
`lastgroup.py`, `inlemma.py`, `aegis_lc.py`, `lcrun.py`. Logs: `lastgroup_*.log`, `vanish_r2*.log`, `in2*.log`,
`inlemma_perm.log`, `aegis_rocca.log`, `aegis128l.log`, `rocca6_long.log`, `t_*.log`, the CP-SAT log of the permutation-step local collision
(result line copied by hand after an accidental overwrite). Imports `tdiff.py`, `tohighs.py` and their pylib/ from the earlier review (differential viewpoint).
