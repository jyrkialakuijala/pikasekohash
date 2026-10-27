# Cryptanalysis challenges

Concrete targets on PikasekoHash-256 version 0 and reduced versions of it,
ordered from the most likely to fall to the full function. Each says what is
known, so that a result can be placed. Results are credited in ANALYSIS.md;
see [SECURITY.md](../SECURITY.md) for how to report. There is no prize.

Reduced versions change only the number of final steps f (6 in the full
function). The steps after the last packet injection are 2 + f: the last two
steps of the last absorption, then the final steps.

## Checking a solution

The Python reference [ref/pikaseko256.py](../ref/pikaseko256.py) computes
every variant below:

    python3 ref/pikaseko256.py --hex <message hex> [--key <64 key bytes hex>] [--final f]

and its functions `step(r, t)` and `absorb(r, t, packets)` compute one step
and one absorption on a state given as sixteen 64-byte vectors, as in
SPEC.md. The C entry point `pikaseko256_steps` in
`include/pikaseko256_analysis.h` runs steps on a state of 128 words, and
`tools/verify_step_collisions.c` shows how a state-level claim is checked
(the one-step collisions of `analysis/data/`).

## 1. Semi-free-start collision of one absorption

Find a state X (sixteen vectors), a step index t and two different groups of
four packets P, P' such that `absorb(X, t, P) == absorb(X, t, P')`.

Known: no such pair. A collision after the first step following the
injection is impossible (ANALYSIS.md, section 5); a collision needs two
linked inbound phases over the next two steps, with ample degrees of
freedom. Whether this is practical is the main open question of the design.
A solution does not give a hash collision by itself (X must also be
reachable from the initial value), but it removes the basis of any
collision claim.

## 2. Free-start collision of one absorption with fewer steps

The same with the absorption shortened to two steps (no product, injection
after the first step): find X, t, P != P' with equal states after step t+1.

Known: impossible in one step after the injection (section 5); this is the
easiest form of challenge 1 that is still open as stated.

## 3. Output distinguisher with more than two steps after the last injection

For the unkeyed or keyed function with f final steps, give a distinguisher of
the 256-bit output from a random function, with its data and time.

Known: f = 0 (2 steps after injection): second-order derivatives and zero
sums of 2^8 to 2^15 messages (section 6). f = 1 (3 steps): nothing found
with cubes up to 2^32 and derivatives up to order 5. The full function has
f = 6. A distinguisher for f = 1 halves the stated margin.

## 4. Collision or preimage of the reduced hash

Unkeyed, f = 0, 1 or 2: a collision of the 256-bit output (target below
2^128), a collision of the 64-bit output `pikaseko64` (target below 2^32,
the generic cost), or a preimage of either.

Known: nothing below the generic costs.

## 5. Key recovery or forgery on the reduced keyed hash

Keyed with a uniform 512-bit key, f = 0 or 1: recover the key, or predict the
output for a new message, from chosen-message queries.

Known: nothing. The key enters only the initial value (SPEC.md, section 4).

## 6. Any result on the full function

A collision, second preimage or preimage of the full hash, or a
distinguisher of the full output, keyed or unkeyed. See the security targets
in ANALYSIS.md.
