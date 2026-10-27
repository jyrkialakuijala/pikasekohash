# PikasekoHash-256: a wide-state AES-round hash for AVX-512

Jyrki Alakuijala

October 2026 (version 0, draft)

## Abstract

PikasekoHash-256 is an experimental 256-bit hash function, keyed or unkeyed,
built from the AES round instruction and 32-bit multiplication on 512-bit
vector registers. Its state is 8192 bits held in sixteen AVX-512 registers.
Each step mixes the registers in four groups of four through two AES rounds
per register, keyed by xors of the other registers of the group, and a
stride schedule makes a one-bit difference reach every register in three
steps. Message packets of 64 bytes enter four at a time into a transversal
set of registers, and one 32 x 32 -> 64-bit product per packet, formed from
AES-mixed state, is xored in two steps later. Six final steps and a fold
through two AES rounds produce the output. On a Zen 5 core it hashes
24 GB/s in bulk, 1.7 times HighwayHash and twice single-threaded BLAKE3,
and any message up to 128 bytes takes 36 ns. It passes every statistical
test we applied. Our own cryptanalysis found no attack on the hash, but it
found that the step function is not injective in a constructive way:
collisions of one step are built in constant time from a chosen state, so
free-start collisions of the compression function are practical. Whether
semi-free-start collisions are also practical is open. PikasekoHash-256 is a
research design, offered for analysis, not for production use.

## 1 Introduction

Fast keyed hashes such as SipHash [15] and HighwayHash [1] were designed to stop
hash flooding: an attacker who does not know the key cannot choose colliding
inputs. Fast cryptographic hashes such as BLAKE3 [2] aim at collision
resistance without any key. Between them lies a gap on modern x86 hardware:
on AMD Zen 5, VAES executes two AES rounds per cycle on 512-bit registers,
each four 128-bit blocks, yet no widely used hash spends that budget on a
large state.

PikasekoHash asks what a hash looks like if it is shaped around that
instruction mix from the start. Three choices follow.

1. A wide state. Sixteen 512-bit registers (8192 bits), far more than the
   256-bit output, and enough independent AES chains per step to hide the
   AES latency.
2. Mixing by keyed AES rounds between registers. A register's round keys are
   xors of other registers, so an AES round moves information between
   registers as well as within a block, and a rotation of the second key
   moves it between the four blocks of a register.
3. A small amount of arithmetic non-linearity. One 32 x 32 -> 64-bit
   multiply per packet runs beside the AES rounds. Its operands are AES
   outputs, never raw input, which avoids the zero-multiplicand weakness of
   designs that multiply input directly [5].

Building a hash from the AES round is not new. ECHO [16], a SHA-3
candidate, kept a 2048-bit state of sixteen 128-bit words and applied two
AES rounds to each word between word-level shifts and mixes. Haraka v2 [17]
and Areion [19] build short-input hashes from fixed permutations of a few AES
blocks, and Simpira v2 [18] builds permutations of any multiple of 128 bits
from AES rounds in Feistel structures. All of these use AES rounds inside
permutations with fixed round keys or constants, and rest on wide-trail and
permutation-based arguments. PikasekoHash differs in two ways that cost it
those arguments: its round keys are taken from the state (xors of other
registers), as in the state updates of AEGIS [11] and Rocca [12], and its
step is not a permutation (section 4.2). In exchange it uses only AES rounds,
xors and one block shuffle per key, with no permutes between steps.

The design started from HighwayHash's multiply-and-zipper-merge round and
kept its multiply; the zipper permutation added nothing measurable once AES
rounds did the byte mixing, and was dropped.

Section 2 specifies the function, section 3 explains the choices, section 4
reports the security analysis, including the free-start collisions, and
sections 5 to 7 cover statistics, speed and implementation. The complete
specification, test vectors, implementations and the review reports are in
the repository that accompanies this paper.

## 2 Specification

This section is a summary; SPEC.md in the repository is the normative
definition.

**Notation.** A register is eight 64-bit lanes, or four 128-bit AES blocks
(block b = lanes 2b, 2b + 1). `+` is lane-wise addition modulo 2^64 and `^`
is xor. aesenc(x, k) is one AES encryption round [4] (SubBytes, ShiftRows,
MixColumns, then xor with the round key) on every block of x with the
corresponding block of k, exactly the x86 instruction VAESENC. rot1 and rot2
rotate the blocks of a register by one and two positions. mul32(a, b)
multiplies the low 32 bits of each lane into a 64-bit lane.

**Initial value.** From the 32-bit words pi = 0x243F6A88, e = 0xB7E15162 and
phi = 0x9E3779B9, with the factors reduced modulo 2^32, lane l of register
i is

    r_i[l] = (phi + i*pi) * (phi + l*e)  ^  rotl64(K[(l + i) mod 8], 4i)

for a 512-bit key K; the unkeyed hash uses K = 0. The 128 products are
distinct and dense. The initial value costs one multiply per register and
needs no table.

**Step.** Step t uses stride s = 2^(t mod 4), cycling 1, 2, 4, 8, and cross
offset c = 2s (c = 1 when s = 8). The registers form four groups
{i, j = i^s, i2 = i^c, j2 = i2^s}. In each group, with all inputs taken
before the step and rot = rot1 on even and rot2 on odd steps:

    k = r_i ^ r_j     k2 = r_i2 ^ r_j2 (^ C[t mod 4] in the group of r0)     kk = k ^ k2
    r_i  <- aesenc(aesenc(r_i,  k2), rot(k))
    r_i2 <- aesenc(aesenc(r_i2, k ), rot(k2))
    r_j  <- aesenc(aesenc(r_j,  kk), rot(k2))
    r_j2 <- aesenc(aesenc(r_j2, kk), rot(k))

C[0..3] are four 512-bit constants taken from the hexadecimal digits of pi.
A step is 32 AES rounds on 512-bit registers, 12 xors and 8 block rotations.

**Absorption.** The padded message is a sequence of 64-byte packets,
absorbed four at a time. With feed registers f = (0, 5, 10, 15), multiplicand
registers m = (1, 4, 11, 14) and product targets g = (3, 6, 9, 12), a group
P_0..P_3 starting at step t is

    step t;    r_{f_k} += P_k
    step t+1;  q_k = mul32(r_{m_k}, r_{f_k})
    step t+2;  r_{g_k} ^= q_k

A last group of fewer than four packets uses the first feeds, multiplicands
and targets only.

**Padding and output.** Padding is Merkle-Damgård strengthening: the byte
0x80, zeros, and the message length in bytes as a little-endian 64-bit word
in the last lane of the last packet (one extra packet when the 0x80 falls at
byte 56 or later). After the last group come six more steps, then

    a = r_0 + r_1 + ... + r_15
    s = aesenc(aesenc(a, r_1 + r_2), rot1(r_3 + r_4))
    output = lanes 0..3 of s + rot2(s).

A message of up to 247 bytes takes 9 steps; in bulk, 256 bytes take 3 steps.

## 3 Design rationale

**Throughput sets the budget.** On Zen 5 the AES rounds run on two vector
pipes, one of which the multiplies and the block shuffles share, while xors and
additions run beside them. The AES rounds alone fill 48 of the about 58
cycles that the bulk loop takes per 256 bytes (section 6), so the design
counts AES rounds, shuffles and multiplies, and spends xors freely. A
state of sixteen registers gives sixteen independent two-round AES chains
per step, which hides the four-cycle AES latency; states of eight and four
registers were measured latency-bound and slower per byte.

**Cross-pair keys.** An earlier butterfly keyed each register by its own
pair's sum and reached only eight registers in three steps. Keying the lower
register of each pair first by the other pair's key, and the upper register
by the group key kk, makes a step join four registers. With the stride cycle
1, 2, 4, 8, a one-bit difference reaches all sixteen registers in three
steps. Every register of a group follows a different key sequence, so no
equality between two registers survives a step.

**Xor keys.** The keys were first formed with 64-bit additions. Xor keys
cost no speed and make the key layer GF(2)-linear, so the standard
byte-level wide-trail model [8] describes the step exactly apart from the
products; diffusion after three steps dropped slightly (mean flip entropy
0.907 against 0.921).

**Rotations.** A rotated key lags its xor by one shuffle, so each register's
first key is a plain xor and its second a rotated one; no first AES round
waits for a shuffle. Alternating rot1 and rot2 with the four strides makes
the block hop independent of the register hop; a fixed two-stride pattern
left five eighths of the (register, block) positions unreachable.

**Step constant.** With xor keys and no constant the step commutes with 16
linear maps of the state (block rotations, lane swaps within blocks and
32-bit column rotations), and a state whose registers are all equal stays
all equal, because every key is then zero. One constant per step, xored into
one key, removes all of these: none of the maps commutes with the step and
the all-equal state breaks in the first step. No key makes the keyed initial
value all-equal (7680 linear conditions on the 512 key bits, inconsistent).
The constant cancels in differences and zero sums, so it changes no trail
bound and no integral. ChaCha [6] and BLAKE3 [2] likewise rely on
constants to break the symmetries of their rounds.

**Transversal feeds.** The four packets enter registers 0, 5, 10 and 15,
which meet every group of every stride exactly once. No key and no group
ever holds two packets, so one packet difference cannot cancel another
inside a key. An earlier feed set {0, 4, 8, 12} allowed exactly that at two
of the four phases.

**One multiply per packet, from state only.** A multiply is linear over the
integers in each operand: if an operand is the raw packet, the attacker
controls an additive difference of the product, and a zero multiplicand
drops the other operand. Both operands of q_k are AES outputs that contain
the packet through one step. Experiments on earlier versions with zero to eight multiplies per
group showed that more multiplies cost speed without improving any
measurement, and no OR mask on the multiplicands (as in HighwayHash [1])
was needed. Section 4.6 shows the limit of this argument in the unkeyed
hash.

**Finalization length.** We follow the rule that the output transformation
should run at least as long as it takes a difference to reach 99% of the
state bytes, preferably twice. That takes four steps (section 4.1). The last
packet is followed by two steps of its absorption and six final steps, eight
in all, twice the diffusion length. The last products enter before the
first final step, so they get all six. Moving the product injection later,
into the finalization, was measured and produced a statistical artefact at
reduced final steps, so the finalization contains no multiplications.

**Fold.** A plain sum of the registers is a linear projection of the final
state; with one final step it left an exact zero sum on output bit 0 over
2^15 messages. Passing the sum through two AES rounds keyed by sums of other
registers makes the output a nonlinear function of the final state.

## 4 Security analysis

We make no security proof. This section reports the measurements and
arguments of the designer and of five targeted reviews (rebound attacks,
linear and algebraic attacks, practical attacks and implementation, the
AES-based hash and AEAD design tradition, and a re-measurement of the output
margins), whose reports are in the repository. Results are labelled
*proven* (an exact argument, or a solver run that closed the gap in a stated
model), *measured* (computed on the real function), or *argued*.

### 4.1 Diffusion

Bytes of the 1024-byte state that differ after n steps, from a one-bit
difference, mean and worst case over all 8192 positions (measured):

| steps | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---:|---:|---:|---:|---:|---:|
| mean | 28 | 220 | 891 | 1020 | 1020 | 1020 |
| worst | 22 | 182 | 751 | 1009 | 1011 | 1011 |

1020 of 1024 is the value for a random difference. After three steps every
register is reached from any start. From a one-bit packet difference, the
mean binary entropy of the state bit flip probabilities is 0.25, 0.91 and
0.9998 after two, three and four steps. No packet difference can be
cancelled by the next absorption's additions (0 of 16 feed and phase cases),
and the 2048 single-bit packet differences of one absorption have full rank
2048 over GF(2) on the state (measured).

### 4.2 The step is not injective: free-start collisions

Because a register's keys depend on registers that the same step
overwrites, the step is not a permutation; scaled-down models of the group
map behave like a random function (image fraction 0.632, measured). Finding
a collision of one step is nevertheless far easier than a generic collision
on a 1024-bit random function.

**One-step collisions (measured).** Two states that differ in 54 bytes of
the four registers of one group give identical outputs of one step. The
characteristic has 90 active S-boxes, and all of them are solved
deterministically rather than paid for by probability. The first-round
(middle) keys of the AES rounds are raw xors of other registers of the
group, in the same block and byte position. Where one register of a pair is
inactive at a byte, that key byte is a free linear handle, set after the
first-round values are fixed. This is an inbound phase of the rebound
attack [9] over the super-S-boxes of two AES rounds [10], and the
state-derived keys help it rather than hinder it. The construction succeeds
at the first attempt at odd steps (rotation by two blocks) for every group,
including the group with the step constant. For even steps a lower bound of
76 active S-boxes is proven and a 78-S-box characteristic exists; its values
were not completed. Five explicit pairs are in the repository with a
program that checks them.

**Half-invertibility (measured).** Given the two pair keys of a group and
any target outputs for its two lower registers, eight inverse AES rounds
give an input with exactly those outputs. An attacker who controls the
state before a step chooses half of the state after it.

**Consequences (measured).** An absorption that starts at an odd step
absorbs the same packets from two different chaining values into the same
state, and two different states before the first final step give the same
digest after the six final steps and the fold. These are free-start
collisions of the compression function and of the finalization at full
round count. They do not give a collision of the hash, because the two
colliding chaining values are not known to be reachable from the fixed
initial value. In the tradition of the SHA-3 competition this is a serious
certificational weakness: no argument may rest on the collision resistance
of the compression function, and no permutation-based (sponge) proof [7]
applies.

**Truncated models overestimate what exists (proven).** The byte-level
truncated model had reported a one-step local collision with 36 active
S-boxes. It is infeasible: it needs an exact relation between round-1
S-box output differences that the model cannot see. A sharper model that
keeps the invertibility of MixColumns proves at least 57 (odd steps) and 76
(even steps) active S-boxes for any one-step collision; its 57-point is
itself infeasible. The exact minimum lies in [57, 90] for odd steps and in
[76, 78] for even steps.

### 4.3 Semi-free-start collisions (open)

The decisive question is whether the chaining value can stay fixed while
only the packets differ.

* Step t+1, the first after an injection, cannot produce a collision: a
  difference that enters a group through one register cannot vanish in
  that step, and the transversal feeds put at most one packet into each
  group (proven).
* A collision at step t+2 needs two linked inbound phases, over steps t+1
  and t+2, with at least two active feed registers (argued).
* The degrees of freedom suffice by a wide margin: about 2400 bits of
  conditions against 8192 free bits (argued). If only one of the two steps
  can be covered by an inbound phase, the remaining conditions cost about
  2^-1260; if both can, the attack is probably practical.

We could not decide this, and we make no collision-resistance claim until
it is settled.

### 4.4 Trail bounds and their limits

The following bounds are proven in byte-level truncated models (one active
S-box per active byte entering an AES round, MixColumns branch number 5,
every cancellation allowed). A bound of B active S-boxes bounds a single
differential characteristic by 2^-6B and a single linear trail by 2^-3B;
clustering and hulls are ignored.

| property | bound |
|---|---|
| local collision over two absorptions, products pass differences only through the multiply | >= 100 |
| the same, whatever the products do (steered, zeroed or cancelling) | >= 64 |
| last step of the cancelling absorption alone, two steps free to the attacker | >= 36 (2^-216) |
| internal collision over any number of absorptions (strict products) | >= 100 |
| six final steps, any trail not ending in an internal collision | >= 81 (2^-486) |
| linear trail, one step / two steps (phases 1 and 3) | >= 5 / >= 15 |
| linear trail, last injection to the output | >= 55 (correlation <= 2^-165) |
| biased output combination of one step (the linear dual of non-injectivity) | >= 50 to 60 |
| related keys: any key difference, step 0 alone | >= 56 (2^-336) |

These numbers charge probability for every step. Section 4.2 shows that an
attacker who chooses the state solves all 90 S-boxes of a step at no cost
and chooses half of the next state, so a bound is meaningful only for the
steps outside the attacker's control. For a semi-free-start attack that may
be only the last step. For comparison, in the same model (with the update functions
transcribed by the reviewer), local collisions of AEGIS-128L [11] need 51
and of Rocca [12] 25 active S-boxes; those
designs are keyed, with a secret state, so the comparison concerns the
models, not security.

Related keys deserve a remark: the key enters only the initial value, every
register contains every key word, and step 0 runs before the first packet,
so every nonzero key difference activates all sixteen registers before any
message bit can cancel it.

### 4.5 Output margins

Steps after the last injection that any distinguisher on the output covers
(measured):

| attack | 4 final steps, linear fold (6 steps) | 6 final steps, AES fold (8 steps) |
|---|---|---|
| per-bit output-difference bias | 2 | none found |
| low-order derivative on lane lsbs, keyed, no state knowledge | 3 (8 messages) | 2 (4 messages) |
| cube or zero sum on the output | 3 (2^15 messages) | 2; nothing at 3 with cubes up to 2^32 and derivatives up to order 5 |
| state integral (absorption only) | 3 full state, 4 one block | same |
| largest step count with any output distinguisher | 3 (margin 3) | 2 (margin 6) |

The remaining two-step distinguisher comes from the fold: its AES rounds act
per block and never mix blocks {0, 2} with {1, 3}, and the second round key
passes through no AES round. Feeding s ^ rot1(s) into the second round
removes it in a model. "Nothing at three steps" is empirical; a numeric
mapping degree bound [13] proves the whole state a zero sum over 2^48 inputs
after three steps but proves no output bit balanced.

### 4.6 Products in the unkeyed hash

The product operands are read after step t+1, one AES round (or a linear
key path) from the freely chosen feed register. With the state public,
inverting that round sets the operands: all four products of an absorption
can be forced to zero in milliseconds (measured). This is not a collision,
since the packet still enters through the addition, but in the unkeyed hash
the products add no non-linearity the attacker must pay for. The
product-independent bounds of section 4.4 are the relevant ones there. With
a secret key the state is unknown and the products cannot be steered.

### 4.7 Other properties

* **Generic attacks.** The 256-bit output limits collisions to 2^128 and
  preimages to 2^256; generic internal-state collisions are far above this.
* **Length extension** does not apply: the output is a compressing function
  of the state after six further steps.
* **Padding.** Merkle-Damgård strengthening with the byte length in the last
  lane is injective without any assumption on the state.
* **Adversarial multiplications** of the kind found in xxh3, HighwayHash and
  aHash [5] do not apply: the operands are AES outputs and the packet always
  enters through the addition.
* **Keyed use.** Across 512 random keys and many structured message
  differences, no key-independent differential or collision appeared
  (measured).
* **Near-collisions.** With the attacker's best single-bit difference in the
  last packet of the unkeyed hash, the output Hamming distance is
  indistinguishable from random even with no final steps (measured).
* **No domain separation.** The zero key is the unkeyed hash, and the 64-bit
  output is the first word of the 256-bit output. A parameter word in the
  initial value is planned.

## 5 Statistical evaluation

Statistical tests cannot establish security, but they rule out gross
mistakes, and they are the main evidence usually offered for fast hashes
such as xxh3 [3]. We applied:

* A 256-bit battery: avalanche, bit independence, differential, sparse key,
  cyclic, two-byte, windowed counter, permutation, seed and length tests,
  with Poisson tails for small expectations. The design passes with six,
  two, one and zero final steps. That it passes at zero final steps, where
  four messages distinguish the output, shows how little such batteries see.
* Structured-input histograms: 2^34 inputs made of a counter in an otherwise
  constant 128-byte message, bucketed by 20 output bits, in two input
  layouts. The bucket counts have standard deviation 1.0007 and 1.0001 times
  the Poisson value, with extremes of -4.8 / +4.7 and -5.0 / +4.6 standard
  deviations. In the first layout SHA-256 gives 0.9992 (-5.2, +4.9), BLAKE3
  1.0004 (-4.8, +4.8), HighwayHash 0.9998 (-4.9, +4.9) and xxh3 0.9991
  (-5.2, +5.1).
* 768 smaller histogram tests (2^24 inputs into 2^16 buckets over 32
  counter positions, three backgrounds, two layouts and four output
  windows), compared with exact Poisson extreme-value distributions: the
  mean minimum count is 190.29 (ideal 190.34), the mean maximum 327.49
  (ideal 327.50); the 768 chi-square p-values are uniform (Kolmogorov-Smirnov
  p = 0.90) and so are the count distributions (p = 0.21).
* Context models of the pictures (neighbour statistics of extreme buckets,
  neighbour correlation, an 8-neighbour linear predictor, the maximum
  autocorrelation over all lags, and 60 further pictures per hash with other
  counter positions) were run on the same absorption with the previous
  finalization, alongside SHA-256, BLAKE3, HighwayHash, xxh3 and rapidhash.
  No hash showed a reproducible deviation, while a weak multiplicative hash
  failed every test.

## 6 Performance

One core of a Ryzen 9 9950X3D (Zen 5, about 5.5 GHz under load), data in
cache, gcc 15. The table measures repeated independent calls on one
16 KiB buffer, with the same harness for all hashes.

| hash | GB/s at 16 KiB |
|---|---:|
| xxh3-64 | 109.3 |
| rapidhash | 60.9 |
| PikasekoHash-256 | 23.5 |
| HighwayHash-256 (AVX2) | 13.6 |
| BLAKE3 (official C, AVX-512, one thread) | 11.8 |
| SHA-256 (OpenSSL, SHA-NI) | 2.8 |

The bulk loop takes about 14.5 cycles per 64 bytes, 24 GB/s at 1 MiB. The
two AES pipes alone need 12 of them (96 AES rounds on 512-bit registers per
256 bytes at two per cycle: 48 cycles per 256 bytes); the shuffle-to-AES recurrence and the
multiplies account for the rest. Every message up to 128 bytes takes 36 ns,
256 bytes 45 ns and 1 KiB 76 ns, measured as the latency of dependent calls;
a key adds about 2 ns. The minimum is nine steps.

## 7 Implementation

The AVX-512 implementation keeps the sixteen registers in zmm registers and
writes each step with a compile-time phase, so strides, rotations and the
step constant are immediates; the bulk loop absorbs 1 KiB (twelve steps) per
iteration. It has no branch or memory access that depends on the key or the
message contents; it branches only on the length and on whether a key is
given, and it reads the tail of the message with masked loads, never beyond
the input.

The portable implementation runs on any CPU and byte order and gives the
same results. It uses a bitsliced AES: each 512-bit register is held as
eight 64-bit bit planes, the S-box is the Boyar-Peralta circuit [14] applied
to all 64 bytes of a register at once, and ShiftRows, MixColumns and the
block rotations are fixed shifts and masks. It has no tables and no
data-dependent branches, indices or shift amounts. It takes about 6 µs per
short message and runs at about 118 MB/s in bulk, roughly 200 times slower
than the AVX-512 code.

## 8 Limitations and future work

* The semi-free-start question of section 4.3 must be settled before any
  collision-resistance claim.
* The one-step collisions use the linear middle-key handles and the sharing
  of the group key between the two upper registers. A step that processes or
  rotates the first-round key, followed by a re-run of the collision
  construction, is the next design change. A permutation step was measured:
  it costs 28% speed and lowers several truncated-model bounds, and with
  delayed products its absorption is not a bijection either.
* A domain-separation word, an extendable output mode and a tree mode for
  several cores are planned (the streaming interface exists).
* The AVX2 + VAES, AES-NI and ARMv8 versions are to be measured on the CPUs
  they are meant for, and all versions on Intel cores.
* Bounds with bit-level or super-box models, a division-property bound for
  the output, and above all third-party cryptanalysis are needed.

Until then PikasekoHash-256 is a research design: a fast hash with a large
state, offered as a target for analysis in both its keyed and unkeyed forms.

## References

[1] J. Alakuijala, B. Cox, J. Wassenberg. Fast keyed hash/pseudo-random
function using SIMD multiply and permute. arXiv:1612.06257, 2016.

[2] J. O'Connor, J.-P. Aumasson, S. Neves, Z. Wilcox-O'Hearn. BLAKE3: one
function, fast everywhere. 2020.

[3] Y. Collet. xxHash: extremely fast hash algorithm. 2019-2024.

[4] National Institute of Standards and Technology. FIPS 197, Advanced
Encryption Standard; FIPS 180-4, Secure Hash Standard.

[5] T. D. Ahle. Adversarial examples for fast hash functions. Blog post,
updated 29 September 2026.
https://thomasahle.com/blog/adversarial-examples-for-hashes/

[6] D. J. Bernstein. ChaCha, a variant of Salsa20. 2008.

[7] G. Bertoni, J. Daemen, M. Peeters, G. Van Assche. Cryptographic sponge
functions. 2011.

[8] J. Daemen, V. Rijmen. The Design of Rijndael. Springer, 2002.

[9] F. Mendel, C. Rechberger, M. Schläffer, S. S. Thomsen. The rebound
attack: cryptanalysis of reduced Whirlpool and Grøstl. FSE 2009.

[10] H. Gilbert, T. Peyrin. Super-Sbox cryptanalysis: improved attacks for
AES-like permutations. FSE 2010.

[11] H. Wu, B. Preneel. AEGIS: a fast authenticated encryption algorithm.
SAC 2013.

[12] K. Sakamoto, F. Liu, Y. Nakano, S. Kiyomoto, T. Isobe. Rocca: an
efficient AES-based encryption scheme for beyond 5G. IACR Transactions on
Symmetric Cryptology 2021(2).

[13] M. Liu. Degree evaluation of NFSR-based cryptosystems. CRYPTO 2017.

[14] J. Boyar, R. Peralta. A new combinational logic minimization technique
with applications to cryptology. SEA 2010.

[15] J.-P. Aumasson, D. J. Bernstein. SipHash: a fast short-input PRF.
INDOCRYPT 2012.

[16] R. Benadjila, O. Billet, H. Gilbert, G. Macario-Rat, T. Peyrin,
M. Robshaw, Y. Seurin. SHA-3 proposal: ECHO. Submission to the NIST SHA-3
competition, 2009.

[17] S. Kölbl, M. M. Lauridsen, F. Mendel, C. Rechberger. Haraka v2:
efficient short-input hashing for post-quantum applications. IACR
Transactions on Symmetric Cryptology 2016(2).

[18] S. Gueron, N. Mouha. Simpira v2: a family of efficient permutations
using the AES round function. ASIACRYPT 2016.

[19] T. Isobe, R. Ito, F. Liu, K. Minematsu, M. Nakahara, K. Sakamoto,
R. Shiba. Areion: highly-efficient permutations and its applications to
hash functions for short input. IACR Transactions on Cryptographic Hardware
and Embedded Systems 2023(2).
