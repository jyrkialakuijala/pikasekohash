# PikasekoHash-256 specification

Version 0 (experimental). This document defines the function completely. An
implementation that follows it reproduces the test vectors in section 11.
The reference implementations are `src/pikaseko256_portable.c` (portable C)
and `src/pikaseko256_avx512.c` (AVX-512 and VAES); both agree with this text.

## 1. Notation

* All integers are unsigned. `+` on 64-bit words is addition modulo 2^64.
  `^` is bitwise xor. `rotl64(x, n)` rotates a 64-bit word left by n bits
  (n = 0 leaves it unchanged).
* A *vector* is 512 bits, held as eight 64-bit lanes `x[0] .. x[7]`.
  On vectors, `+` and `^` act lane by lane.
* The *bytes* of a vector are numbered 0..63: byte 8 l + m is byte m
  (m = 0 least significant) of lane l. This is little-endian order.
* A vector is also four 128-bit *blocks*: block b (b = 0..3) is lanes 2b and
  2b + 1, that is bytes 16b .. 16b + 15. Byte n of a block (n = 0..15) is
  byte 16b + n of the vector.
* `rot1(x)`: block b of the result is block (b + 1) mod 4 of x.
  `rot2(x)`: block b of the result is block (b + 2) mod 4 of x.
  (In lanes: `rot2(x)[l] = x[(l + 4) mod 8]`.)
* `aesenc(x, k)`: one AES encryption round on each of the four blocks of x,
  with the corresponding block of k as round key, exactly as the x86
  instruction AESENC (and VAESENC per 128-bit lane):

      for each block b:
          state = block b of x, as an AES state (FIPS 197):
                  byte n of the block is state row (n mod 4), column (n div 4)
          state = MixColumns(ShiftRows(SubBytes(state)))
          block b of the result = state ^ (block b of k)

  SubBytes, ShiftRows and MixColumns are those of AES (FIPS 197). There is no
  key schedule; k is used as given.
* `mul32(a, b)`: lane by lane, `(a[l] mod 2^32) * (b[l] mod 2^32)` as a full
  64-bit product.

## 2. Constants

Three 32-bit words, the first 32 fractional bits of pi, e and the golden
ratio phi:

    PI  = 0x243F6A88
    E   = 0xB7E15162
    PHI = 0x9E3779B9

Four step constants C[0..3], each a vector (lanes 0..7 left to right). They
are 64-bit words 96..127 of the hexadecimal expansion of the fractional
part of pi (16 hex digits per word, word 0 = 0x243f6a8885a308d3):

    C[0] = 0x62fb1341cee4c6e8 0xef20cada36774c01 0xd07e9efe2bf11fb4 0x95dbda4dae909198
           0xeaad8e716b93d5a0 0xd08ed1d0afc725e0 0x8e3c5b2f8e7594b7 0x8ff6e2fbf2122b64
    C[1] = 0x8888b812900df01c 0x4fad5ea0688fc31c 0xd1cff191b3a8c1ad 0x2f2f2218be0e1777
           0xea752dfe8b021fa1 0xe5a0cc0fb56f74e8 0x18acf3d6ce89e299 0xb4a84fe0fd13e0b7
    C[2] = 0x7cc43b81d2ada8d9 0x165fa26680957705 0x93cc7314211a1477 0xe6ad206577b5fa86
           0xc75442f5fb9d35cf 0xebcdaf0c7b3e89a0 0xd6411bd3ae1e7e49 0x00250e2d2071b35e
    C[3] = 0x226800bb57b8e0af 0x2464369bf009b91e 0x5563911d59dfa6aa 0x78c14389d95a537f
           0x207d5ba202e5b9c5 0x832603766295cfa9 0x11c819684e734a41 0xb3472dca7b14a94a

## 3. State

The state is sixteen vectors r0 .. r15 (8192 bits). A step counter t starts
at 0 and increases by one with every step, through absorption and
finalization.

## 4. Key and initialization

The key K is eight 64-bit words K[0..7] (512 bits). When a key is given as
64 bytes, K[i] is bytes 8i .. 8i + 7, little-endian. The unkeyed hash is the
keyed hash with K = 0 (all words zero); passing no key means K = 0.

For i = 0..15 and l = 0..7, with the two factors computed modulo 2^32 and
multiplied as a full 64-bit product:

    a      = (PHI + i * PI) mod 2^32
    b      = (PHI + l * E)  mod 2^32
    r_i[l] = (a * b)  ^  rotl64(K[(l + i) mod 8], (4 i) mod 64)

## 5. The step

Step t operates on the whole state. Let

    s   = 2^(t mod 4)                  stride: 1, 2, 4, 8, 1, 2, ...
    c   = 2 s, except c = 1 when s = 8 cross offset: 2, 4, 8, 1
    rot = rot1 if t is even, rot2 if t is odd

The sixteen register indices form four groups. For each of the four indices
i in 0..15 with `(i & s) == 0` and `(i & c) == 0`, the group is

    i,   j = i ^ s,   i2 = i ^ c,   j2 = i2 ^ s

(two pairs, (i, j) and (i2, j2)). All keys of a group are computed from the
state before the step:

    k   = r_i  ^ r_j
    k2  = r_i2 ^ r_j2                (in the group with i = 0 only: k2 = r_i2 ^ r_j2 ^ C[t mod 4])
    kk  = k ^ k2

and the four registers are replaced by

    r_i'  = aesenc(aesenc(r_i,  k2), rot(k))
    r_i2' = aesenc(aesenc(r_i2, k ), rot(k2))
    r_j'  = aesenc(aesenc(r_j,  kk), rot(k2))
    r_j2' = aesenc(aesenc(r_j2, kk), rot(k))

The step constant is applied in exactly one group per step, the group that
contains r0, and it enters k2 before kk is formed (so kk contains it too).
The four groups are independent; the order in which they are computed does
not matter. After all groups, t increases by one.

Groups per stride, as (i, j, i2, j2):

| t mod 4 | s | c | groups |
|---|---|---|---|
| 0 | 1 | 2 | (0,1,2,3) (4,5,6,7) (8,9,10,11) (12,13,14,15) |
| 1 | 2 | 4 | (0,2,4,6) (1,3,5,7) (8,10,12,14) (9,11,13,15) |
| 2 | 4 | 8 | (0,4,8,12) (1,5,9,13) (2,6,10,14) (3,7,11,15) |
| 3 | 8 | 1 | (0,8,1,9) (2,10,3,11) (4,12,5,13) (6,14,7,15) |

Write `STEP()` for one step at the current t followed by t = t + 1.

## 6. Packets and padding

Let the message M have n bytes. The padded message is

    M || 0x80 || 0x00 ... 0x00 || L

where L is n (the length in bytes, modulo 2^64) as eight bytes,
little-endian, and the number of zero bytes is the smallest number that
makes the total length a multiple of 64. The padded message has

    N = ceil((n + 9) / 64) = floor(n / 64) + 1 + (1 if n mod 64 >= 56 else 0)

64-byte packets P_0 .. P_{N-1}. Lane l of a packet is its bytes 8l .. 8l + 7,
little-endian. The length is always lane 7 of the last packet. When the
0x80 byte falls at byte 56 or later of a packet (n mod 64 >= 56), the last
packet holds only zeros and the length. The empty message is one packet
(0x80, zeros, length 0).

## 7. Absorption

The feed registers, multiplicand registers and product targets are

    f = (0, 5, 10, 15)
    m = (1, 4, 11, 14)        (m_k = f_k ^ 1)
    g = (3, 6, 9, 12)

The packets are taken in order, four at a time: group number u holds packets
P_{4u} .. P_{min(4u+3, N-1)}. The last group may hold np = 1, 2 or 3 packets;
all others hold np = 4. One group is absorbed as follows, where Q_0..Q_{np-1}
are its packets:

    STEP()                                     step t
    for k in 0..np-1:  r_{f_k} = r_{f_k} + Q_k  (lane-wise addition mod 2^64)
    STEP()                                     step t+1
    for k in 0..np-1:  q_k = mul32(r_{m_k}, r_{f_k})   (from the state after step t+1)
    STEP()                                     step t+2
    for k in 0..np-1:  r_{g_k} = r_{g_k} ^ q_k

A group of np < 4 packets uses only the first np feeds, multiplicands and
targets; it still costs three steps. Groups are absorbed one after the other;
the step counter continues across groups, so group u starts at t = 3u.

## 8. Finalization

After the last group, six more steps:

    STEP() six times                           t = 3 ceil(N/4) .. 3 ceil(N/4) + 5

Then the fold, with all additions lane-wise modulo 2^64:

    a = r0 + r1 + ... + r15
    s = aesenc(aesenc(a, r1 + r2), rot1(r3 + r4))
    out[l] = s[l] + s[l + 4]                   for l = 0..3

(`out` is lanes 0..3 of s + rot2(s).) The step counter is not used by the
fold; rot1 here is fixed.

## 9. Output

The output is four 64-bit words out[0..3]. The canonical 32-byte digest is
out[0], out[1], out[2], out[3], each written little-endian. The 64-bit hash
`pikaseko64` is out[0].

## 10. Pseudocode

    function PikasekoHash256(M, K = 0):
        n = byte length of M
        for i in 0..15, l in 0..7:
            r_i[l] = ((PHI + i*PI) mod 2^32) * ((PHI + l*E) mod 2^32)
                     ^ rotl64(K[(l+i) mod 8], 4i mod 64)
        t = 0
        P = packets of M || 0x80 || zeros || le64(n)       (N packets, section 6)
        for u in 0 .. ceil(N/4) - 1:
            Q  = P[4u .. min(4u+3, N-1)];  np = |Q|
            step(t); t += 1
            for k < np: r[f[k]] += Q[k]
            step(t); t += 1
            for k < np: q[k] = mul32(r[m[k]], r[f[k]])
            step(t); t += 1
            for k < np: r[g[k]] ^= q[k]
        repeat 6 times: step(t); t += 1
        a = sum of r_0 .. r_15
        s = aesenc(aesenc(a, r1 + r2), rot1(r3 + r4))
        return (s[0]+s[4], s[1]+s[5], s[2]+s[6], s[3]+s[7])

    function step(t):
        s = 1 << (t mod 4);  c = (s == 8) ? 1 : 2*s
        rot = (t even) ? rot1 : rot2
        new = copy of r
        for i in 0..15 with (i & s) == 0 and (i & c) == 0:
            j = i ^ s;  i2 = i ^ c;  j2 = i2 ^ s
            k  = r[i] ^ r[j]
            k2 = r[i2] ^ r[j2] ^ (i == 0 ? C[t mod 4] : 0)
            kk = k ^ k2
            new[i]  = aesenc(aesenc(r[i],  k2), rot(k))
            new[i2] = aesenc(aesenc(r[i2], k ), rot(k2))
            new[j]  = aesenc(aesenc(r[j],  kk), rot(k2))
            new[j2] = aesenc(aesenc(r[j2], kk), rot(k))
        r = new

Step count: a message of n bytes uses 3 ceil(N/4) + 6 steps, N as in section 6.
Messages of up to 247 bytes (N <= 4) use 9 steps.

## 11. Test vectors

Message bytes: byte i of the message is i mod 256. "key 1..8" is
K = (1, 2, 3, 4, 5, 6, 7, 8). Each line gives out[0] .. out[3] in hex.

    unkeyed  len  0  4ce9ce973810692d 933bdf92669b1d6e 277e33f96dacc156 9b8ea902b68c1524
    key 1..8 len  0  b51bcf8389ec302d f308920a30644943 8bc96cf0c183894b b8b03fa938843fa0
    unkeyed  len  1  e7c9b70e06ab6227 7f12a8928cc8e7f0 a9c7f2d3f32cc7ba 17f4a037b67cd148
    key 1..8 len  1  9207d99b7e4cc590 7e25960605692074 f9f411794eeb900d eb9fe6abe5f7ac71
    unkeyed  len 55  9e81b0fae8ed6dc0 03dd7d2aeae33099 e230a765098761a7 51a2ca0db29f1ba0
    key 1..8 len 55  8f50d80647a4b127 cfde76a52061df93 234c5c9f9f603c67 b389aa7cfbc7d26f
    unkeyed  len 56  e14d664c8d8d2211 2984b77b33f8b7e3 523c99d419775ad5 a89c748918c88069
    key 1..8 len 56  5404f02ed70670fc 8f0f62cceaf42e13 1ca41f0cc11a41ca c3d485b4d1d46e02
    unkeyed  len 64  4e6e370a080d35d6 80be6c20bbc346cf 276bf04fe3acc9c1 4eccdedbbddd4e4c
    key 1..8 len 64  4f1c2beb2f35bd19 78c22dc619d5d41a 800349c7c7546c05 520b9835c6ed42a5

As a 32-byte digest, the unkeyed hash of the empty message is

    2d69103897cee94c 6e1d9b6692df3b93 56c1ac6df9337e27 24158cb602a98e9b

(written as bytes in order, grouped by eight). The complete list, including
longer messages and a checksum over all lengths 0..2048, is
`test/test_vectors.txt`.

## 12. Notes for implementers

* The step is not a permutation of the state (section 4 of ANALYSIS.md).
  Implementations must not assume invertibility.
* The key enters only through the initial xor. There is no domain separation
  yet: the zero key gives the unkeyed hash.
* The AVX-512 implementation maps a vector to one zmm register: aesenc is
  VAESENC, rot1 and rot2 are VSHUFI64X2 with immediates 0x39 and 0x4E,
  mul32 is VPMULUDQ, the keys are VPXORQ (the step-constant key is one
  three-input VPTERNLOGQ).
* An implementation with 128-bit AES instructions applies AESENC per block;
  the only operation that moves data between blocks is the block rotation
  of the second-round key (and the final rot1, rot2 of the fold).
* `pikaseko256_steps(state, t0, nsteps)` in `include/pikaseko256.h` runs
  nsteps steps from step index t0 on a state given as 128 words, word
  8i + l = r_i[l]. It is meant for analysis and for checking an
  implementation of section 5 in isolation.
