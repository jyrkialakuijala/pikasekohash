# PikasekoHash-256: security and quality analysis

This document collects what is known about PikasekoHash-256 version 0 as
defined in [SPEC.md](SPEC.md). It summarises the designer's own measurements
and five targeted reviews made in October 2026 (reports in
[analysis/reviews/](analysis/reviews/)). None of this replaces outside
cryptanalysis.

Labels, as in the reviews:

* **proven**: an exact argument, or a solver run that closed the gap in a
  stated model. A bound proven in a truncated model is a bound in that model.
* **measured**: computed on the real function (logs and scripts in the
  development repository; the one-step collisions are reproduced in this one).
* **argued**: reasoning consistent with the measurements, no complete
  experiment.

Step counts below are counted from the step that follows a packet injection.
After the last packets, 2 steps of the last absorption and 6 final steps
follow: 8 steps in total.

## Summary

* No collision, second preimage or preimage of the hash is known. No
  distinguisher of the full hash is known.
* The step function is not injective. Collisions of one step are constructed
  in constant time from a chosen state (measured). So free-start collisions
  of the compression function and of the finalization are practical. They do
  not give a hash collision because the chaining values involved are not
  reachable from the fixed initial value.
* The semi-free-start collision of one absorption (same chaining value,
  different packets) is the main open question.
* Differential, linear and related-key bounds exist in byte-level truncated
  models (proven in those models). They count probability only for steps the
  attacker cannot steer; with a chosen state an attacker solves a whole step.
* The largest number of post-injection steps covered by any output
  distinguisher found is 2 of 8 (margin 6 steps).
* In the unkeyed hash, the multiplications can be steered by the attacker
  and add no non-linearity the attacker must pay for.
* The zero key is the unkeyed hash. There is no domain separation yet.

## 1. Design rationale

* **Issue rate, not latency, sets the budget.** On Zen 5 the bulk loop runs
  about 3.3 vector instructions per cycle and is bound by the two AES pipes.
  A step is 32 AES rounds on 512-bit registers, 12 xors and 8 block
  rotations. Sixteen independent AES chains per step hide the 4-cycle AES
  latency; smaller states (eight or four registers) were measured
  latency-bound and slower per byte.
* **Grouping.** A step joins four registers. Every register's two round keys
  come from the other registers of its group, and the second key is rotated
  by one or two blocks, which is the only cross-block data movement. The
  strides cycle through 1, 2, 4, 8, so the groups change every step, and the
  rotation alternates between one and two blocks. With a fixed two-stride
  pattern the block hop locks to the register move and 5/8 of the
  (register, block) positions are never reached (measured).
* **Xor keys.** Keys are formed with xor, so the key layer is GF(2)-linear
  and the classical byte-level wide-trail model is exact for the step except
  for the products. An earlier version used 64-bit adds; the change cost no
  speed and slightly lowered diffusion (entropy after three steps 0.907
  against 0.921, measured).
* **Step constant.** One 512-bit constant from pi enters one group per step.
  Without it the step commutes with 16 linear maps of the state (block
  rotations, the swap of lanes inside a block, column rotations) and the
  all-equal state is invariant. With it, none of the 16 maps commutes with
  the step and the all-equal state breaks in the first step (measured, 0 of
  64 random states). No key makes the keyed initial value all-equal: the
  7680 linear conditions on the 512 key bits are inconsistent (proven,
  rank argument). The constant cancels in differences and zero sums and does
  not change any trail bound.
* **Feeds.** Packets enter r0, r5, r10, r15. This set meets every group of
  every stride exactly once, so no two packets share a pair or a group in
  any step, and no key ever contains two packets. Every injection is
  followed by three steps, the full-diffusion depth (section 2).
* **Products.** One 32 x 32 -> 64-bit product per packet, from the feed
  register and its neighbour f ^ 1 after the step that follows the
  injection, xored into r3, r6, r9 or r12 after one more step. Both operands
  are AES outputs, never raw input. With a secret key they add integer
  non-linearity outside the AES algebra. Unkeyed, they can be steered
  (section 7).
* **Finalization.** Six steps after the last absorption (two full-diffusion
  lengths beyond its two remaining steps), then a fold through two AES rounds.
  A linear sum fold, as in the previous finalization, left an exact zero-sum
  on output bit 0 after one final step.

## 2. Diffusion (measured)

Bytes of the 1024-byte state that differ after n steps, starting from a
one-bit state difference (`tools/diffusion.c`, all bit positions and phases):

| steps | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---:|---:|---:|---:|---:|---:|
| mean | 28 | 219 | 891 | 1020 | 1020 | 1020 |
| worst | 22 | 181 | 754 | 1010 | 1009 | 1010 |

1020 of 1024 is the expected count for a random difference (each byte equal
with probability 1/256). After three steps every register is reached from
any start. Further measured properties: the mean binary entropy of the bit
flip probabilities from a one-bit packet difference is 0.25, 0.91 and 0.9998
after 2, 3 and 4 steps; no packet difference can be cancelled by the next
absorption's additions (0 of 16 feed/phase cases); chosen differences +d / -d
in two feed registers leave at least 91 non-feed bytes active after one
step; the 2048 single-bit packet differences of one absorption have full
GF(2) rank 2048 on the state.

## 3. Statistical testing (measured)

* The 256-bit battery (`tools/battery.c`: avalanche, bit independence,
  differential, sparse keys, cyclic, two-byte, window, permutation, seed and
  length tests, with Poisson tails for small expectations) passes for the
  final design with 6, 2, 1 and 0 final steps.
* On the previous finalization (same absorption, 4 final steps and a linear
  sum fold), structured-input histograms (2^34 inputs into 2^20 buckets),
  768 small bucket tests and neighbourhood context models put the hash
  inside the random null, alongside HighwayHash, xxh3, rapidhash, SHA-256 and
  BLAKE3 measured the same way.

These results do not measure security. The battery passes at 0 final steps,
where four messages distinguish the output (section 6). Margins below come
from targeted analysis, not from the battery.

## 4. The step is not injective; free-start collisions

The step maps the state to itself but is not a permutation: the round keys
of a register depend on the other registers that the same step overwrites.
In image-size experiments it behaves like a random function (image fraction
0.632, measured), and the linear skeleton of a group has rank 1920-2016 of
2048 (measured).

**One-step collisions (measured).** Two states that differ in 54 bytes of the
four registers of one group give identical outputs of one step. All 90
active S-boxes of the characteristic are solved directly, through linear
handles: the middle (first-round) keys are raw xors of other registers of
the group in the same block. The construction succeeded at the first
attempt at steps t = 1, 3, 5, 7, 9 and in every group, including the group
that receives the step constant. It applies to odd steps (rotation by two
blocks). For even steps a proven lower bound of 76 active S-boxes and a
78-S-box characteristic exist; its values were not completed. The five
pairs are in `analysis/data/step_collision_t{1,3,5,7,9}.txt` and
`tools/verify_step_collisions.c` checks them against the implementation in
this repository (`make test`). One solution leaves about 2^7000 further solutions.

**Half-invertibility (measured).** For any pair keys and any target outputs
of the two lower registers of a group, eight inverse AES rounds give an
input with exactly those outputs. Half of the state after a step can be
chosen at will by an attacker who controls the state before it.

**Free-start collisions (measured).** An absorption that starts at an odd
step (t = 3, 9, 15, ...) absorbs any packets from two different chaining
values into the same state. Two different states before the first final
step give the same 256-bit digest after the six final steps and the fold
(for messages of at most 247 bytes the first final step is t = 3, odd). The
colliding chaining values are not shown to be reachable from the initial
value, so this is not a collision of the hash.

An earlier version of the specification stated that finding a collision
inside a step was as hard as a 1024-bit random-function collision. That
statement was wrong. In the tradition of the SHA-3 competition this is a
serious certificational weakness: no argument may rest on collision
resistance of the compression function, and no permutation-based (sponge)
proof applies. The collision resistance of the hash rests on the fixed
initial value and on the open question of section 5.

**Truncated models overestimate what exists (proven).** The byte-level model
found a 36-S-box one-step local collision; it is infeasible, because it
needs an exact relation between round-1 S-box output differences that the
truncated model cannot see. A sharper model, which keeps the invertibility
of MixColumns, proves at least 57 active S-boxes (odd steps) and 76 (even
steps) for any one-step collision. Its 57-point is itself infeasible. The
exact minimum is in [57, 90] for odd steps and [76, 78] for even steps.

## 5. Semi-free-start collisions (open)

Same chaining value, different packets, one absorption:

* Step t+1 (the first after injection) cannot produce a collision: a
  difference that enters a group through one register cannot vanish in that
  step (proven). The feeds put at most one packet into each group.
* A collision in step t+2 needs two linked inbound phases, over steps t+1 and
  t+2 (argued). The smallest path has two active feed registers.
* The degrees of freedom suffice by a wide margin: about 2400 bits of
  conditions against 8192 free bits (argued). If only one of the two steps
  can be covered by an inbound phase, the cost is about 2^-1260; if both can,
  the attack is probably practical. This is undecided.

No security claim should be made before this question is settled.

## 6. Output margins (measured)

Steps after the last injection that a distinguisher on the output covers.
The previous finalization (4 final steps, linear sum fold) is shown for
comparison.

| attack | previous finalization (6 steps) | final design (8 steps) |
|---|---|---|
| per-bit output-difference bias | 2 | none found at 2 steps or more |
| low-order derivative on lane lsbs (keyed, no state knowledge) | 3 (third order, 8 messages, P = 1) | 2 (second order, 4 messages, P = 1) |
| cube / zero-sum on the output | 3 (2^15 messages) | 2 (2^8 to 2^15); nothing at 3 with cubes up to 2^32 and derivatives up to order 5 |
| integral on the state (absorption only) | 3 full state, 4 one block | same |
| largest step count with any output distinguisher | 3 (margin 3) | 2 (margin 6) |

The 2-step distinguisher comes from the fold (section 8). "Nothing at 3
steps" is empirical: there is no division-property or degree bound that
proves it. The numeric-mapping degree bound gives degree at most 42 after
three steps for 48 spread variables, so the whole state is a proven zero sum
over 2^48 inputs after three steps (proven); no output bit is provably
balanced.

## 7. Products in the unkeyed mode (measured)

The product operands are read after step t+1, one AES round (or a linear key
path) from the freely chosen feed register. With the state known (unkeyed
hash), inverting that round sets the operands: all four products of an
absorption are forced to zero in milliseconds. This is not a collision (the
packet still enters through the addition), but in the unkeyed mode the
products add no non-linearity an attacker must pay for. All unkeyed bounds
below are stated for "any product behaviour" where this matters. With a
secret key the state is unknown and the products cannot be steered.

Reading the operands one step later (after step t+2) was measured to cost
nothing and was argued to remove the steering, but it conflicts with the
current finalization in one statistic at two final steps; it is not part of
version 0.

## 8. The fold (measured and argued)

The fold is s = aesenc(aesenc(a, r1 + r2), rot1(r3 + r4)) on the sum a of
all registers, then out = lanes 0..3 of s + rot2(s).

* It removes the linear zero-sum of a plain sum fold: the correlation
  between an output lsb and the xor of 32 state lsbs is 0.002 (1 for the
  plain sum).
* The two AES rounds act per block and the fold never mixes blocks {0, 2}
  with {1, 3}. Output lanes 0 and 1 (and the 64-bit hash) depend
  non-linearly only on blocks 0 and 2; lanes 2 and 3 on blocks 1 and 3. The
  second key rot1(r3 + r4) passes through no AES round.
* Additive kernel: differences that keep the sum, r1 + r2 and r3 + r4 fixed
  (for example +d in one register and -d in another, for 68 of the 120
  register pairs) leave the output unchanged with probability 1. This is
  inherent to a compressing sum fold. It needs a structured difference in
  the final state, which six final steps do not provide (argued).
* These properties are the source of the 2-step distinguisher and matter
  only with 0 or 1 final steps, where free-start collisions are already
  trivial. Feeding s ^ rot1(s) into the second AES round removes the 2-step
  distinguisher in a model (not timed, not part of version 0).

## 9. Differential bounds (proven in the truncated model)

Byte-level truncated-differential model: one active S-box per active byte
entering an AES round, MixColumns branch number 5, truncated xor (every
cancellation allowed), the step constant cancels. A bound of B active
S-boxes bounds a single characteristic by 2^-6B (AES differential
probability at most 2^-6); clustering is ignored. Local collision means: a
difference injected by one absorption and cancelled by the next.

| property | bound |
|---|---|
| local collision, products strict (they pass differences only through the multiply) | >= 100 |
| local collision, any product behaviour (steered, zeroed or cancelling) | >= 64 |
| last step of the cancelling absorption alone (two steps free to the attacker) | >= 36 (2^-216) |
| same with only one step free | >= 42 |
| internal collision over any number of absorptions (strict products) | >= 100 |
| six final steps, any trail not ending in an internal collision | >= 81 (2^-486) |
| one-step collision (sharper model, section 4) | >= 57 odd steps, >= 76 even steps |

Caveat: these numbers count probability for every step. Section 4 shows that
an attacker who chooses the state solves all 90 S-boxes of a step at no
cost, and that half of the state after a step is free. A bound is therefore
meaningful only for the steps outside the attacker's control. In a
semi-free-start setting the attacker controls the chaining value and the
packets, and the real cost of the last step may be the only cost. The
best characteristics found have 438-672 active S-boxes; these are upper
estimates, not bounds.

## 10. Linear and related-key bounds (proven in the truncated model)

Linear (backward mask propagation, one cost per active S-box, |correlation|
at most 2^-3 per S-box):

| property | bound |
|---|---|
| one step, nonzero input and output mask | >= 5 |
| biased output combination of one step (zero input mask, nonzero output mask; exists because the step is not surjective) | >= 60 (phases 0, 2), >= 50 (phases 1, 3) |
| two steps, all masks nonzero | >= 15 (phases 1, 3; phase 0 not completed) |
| last injection to output (2 + 6 steps + fold) | >= 55, single-trail correlation <= 2^-165 |

A 2^256-data linear distinguisher on one trail needs more than 43 active
S-boxes. Six final steps give 55; four would give 40.

The products: output bit i of a product depends on operand bits 0..i, with
best |correlation| from 2^-1 (bit 0) to 2^-6.1 (bit 9) (measured,
exhaustive). They are a free branch in the model and do not lower the
per-step bounds.

Related keys: the key enters only the initial value, every register contains
every key word, and step 0 runs before the first packet. Every nonzero key
difference activates all 16 registers before the first packet arrives. Step
0 costs at least 56 active S-boxes (2^-336); confining the difference to the
feed registers costs at least 144 (proven in the model). In keyed use, 512
random keys and many difference patterns showed no key-independent
differential and no key-independent collision (measured).

## 11. Other properties

* **Length extension** does not apply: the output is a compressing function
  of the 8192-bit state after six further steps.
* **Generic attacks**: the 256-bit output limits collisions to 2^128 and
  preimages to 2^256. Internal-state collisions are bounded generically by
  the wide state (about 2^3072, argued), far above the output limit.
* **Padding**: Merkle-Damgard strengthening with the byte length in the last
  lane is injective without any assumption on the state (proven; checked for
  all lengths 0..1200 and long ones). The implementations read only the
  message bytes (masked loads in the AVX-512 code).
* **Adversarial multiplications** of the kind found in xxh3, HighwayHash and
  aHash (a zero multiplicand that drops input) do not apply: the operands
  are AES outputs, the packet always enters through the addition, and no key
  contains two packets.
* **Near-collisions**: with the attacker's best single-bit difference in the
  last packet, the output Hamming distance is indistinguishable from random
  even with 0 final steps (measured, 2^16 messages per setting).

## 12. Deployment properties

* **Constant time.** The AVX-512 implementation has no branches and no
  memory accesses that depend on the key or the message contents; it
  branches only on the length and on whether a key pointer is given. Its
  instructions (VAESENC, VPMULUDQ, VPXORQ, VPTERNLOGQ, VSHUFI64X2, VPADDQ,
  masked loads) have operand-independent latency on current x86 cores
  (argued from public documentation). The portable implementation computes
  the AES S-box arithmetically (inversion in GF(2^8) by a fixed addition
  chain, without tables and without data-dependent branches), so it has no
  table lookups indexed by secret data; whether a given compiler preserves
  this is not verified. There is no lazy initialization.
* **Length is not hidden.** The running time depends on the message length.
* **No domain separation.** The zero key, the NULL key and the unkeyed hash
  are the same function, and the 64-bit hash is the first word of the 256-bit
  hash. An application that ends up with an all-zero key gets the public
  hash and a forgeable MAC without any error.
* **Byte orders** are defined in SPEC.md: key words and output words are
  little-endian; the portable code is independent of host byte order.
* **No streaming interface** and no run-time CPU dispatch.

## 13. Open problems and planned changes

1. **Semi-free-start collision of one absorption** (section 5): attempt the
   two-step inbound before any security claim.
2. **Remove the linear middle-key handles.** The one-step collisions use the
   raw xor middle keys and the sharing of kk between the upper registers. A
   step change that processes or rotates the first-round key, followed by a
   re-run of the one-step collision construction, is planned. The
   alternative, a permutation step, was measured 28% slower and lowers the
   truncated-model bounds (last step alone >= 20); with delayed products its
   absorption is not a bijection either, so it would need a redesign.
3. **Domain separation word**: a parameter word (version, keyed flag, output
   length, mode) xored into the initial value, so that keyed with the zero
   key differs from unkeyed and the 64-bit and 256-bit outputs are separated.
4. **Streaming interface** (init, update, final) with the same schedule.
5. Product timing: read the operands after step t+2 to remove unkeyed
   steering, once its statistical artefact at two final steps is understood.
6. A fold that mixes the block halves (section 8).
7. Bounds with bit-level models, a super-box model or a stronger solver; a
   division-property bound for the output; measurements on Intel cores;
   implementations for AES-NI with AVX2 and for ARMv8 crypto extensions.

Any of the changes 2, 3, 5 or 6 changes the test vectors.

## 14. Review history

Two review rounds were made by the designer in October 2026. The first
(eight viewpoints) examined the same absorption with the previous
finalization. Its findings led to the current finalization: six final steps
and the nonlinear fold. The second round (five reviews in
[analysis/reviews/](analysis/reviews/)) examined the final design and found
the one-step collisions. Permutation-step variants were measured during
development and not adopted.
