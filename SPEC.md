# PikasekoHash-256 specification

Version 1 (experimental). This document defines the function completely. An
implementation that follows it reproduces the test vectors in section 11.
Version 1 differs from version 0 (release 0.0.2) in the domain word and the
key handling (section 4), the timing of the products (section 7) and the
64-bit hash (section 9); the step, the constants, the padding and the fold
are unchanged.
The reference implementations are `src/pikaseko256_portable.c` (portable C),
`ref/pikaseko256.py` (plain Python, written from this text)
and `src/pikaseko256_avx512.c` (AVX-512 and VAES); all three agree with this text, as
do the AVX2 + VAES, AES-NI and ARMv8 versions (`src/pikaseko256_simd.h`).

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

## 4. Key, domain word and initialization

The function has an unkeyed and a keyed form, each with a 256-bit and a
64-bit digest. A key is a byte string of any length, possibly empty.

**Key words.** A key of up to 64 bytes is used as it is; a longer key is
replaced by its unkeyed 256-bit digest (32 bytes, this specification), and
the flag `hashed` is set. The key bytes, padded with zero bytes to 64, give
eight 64-bit words K[0..7]: K[i] is bytes 8i .. 8i + 7, little-endian. The
unkeyed form has K = 0.

**Domain word.** A 64-bit word D, bytes from least significant:

| byte | value |
|---|---|
| 0 | the version, 1 |
| 1 | flags: bit 0 set for the keyed form, bit 1 set when the key was hashed |
| 2 | the key length in bytes as used: 0..64 (32 for a hashed key; 0 unkeyed) |
| 3 | the digest length in bytes: 32 or 8 |
| 4..7 | zero |

So the unkeyed 256-bit hash has D = 0x20000001 and the unkeyed 64-bit hash
D = 0x08000001; a keyed hash with an empty key has D = 0x20000101 (256 bits).
Every form, key length and digest length gives a different initial value.

**Initial value.** For i = 0..15 and l = 0..7, with the two factors computed
modulo 2^32 and multiplied as a full 64-bit product:

    a      = (PHI + i * PI) mod 2^32
    b      = (PHI + l * E)  mod 2^32
    r_i[l] = (a * b)  ^  rotl64(K[(l + i) mod 8], (4 i) mod 64)

and then

    r_2[0] = r_2[0] ^ D

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

![One group of the step, and the groups for each t mod 4](docs/step.svg)

(The figure, generated by `tools/step_svg.py`, illustrates this section; the
text above is normative.)

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
P_{4u} .. P_{min(4u+3, N-1)}. The last group holds np = 1, 2, 3 or 4 packets;
all others hold np = 4. Each group leaves np products q_0..q_{np-1} pending;
they are xored into the state after the next step, which is the first step
of the next group or the first final step (section 8). One group is
absorbed as follows, where Q_0..Q_{np-1} are its packets:

    STEP()                                      step t
    xor in the pending products of the previous group, if any:
        r_{g_k} = r_{g_k} ^ q_k   for each of them
    for k in 0..np-1:  r_{f_k} = r_{f_k} + Q_k  (lane-wise addition mod 2^64)
    STEP()                                      step t+1
    STEP()                                      step t+2
    for k in 0..np-1:  q_k = mul32(r_{m_k}, r_{f_k})   (pending; from the state after step t+2)

The pending products of a group use the state two steps after its packets
entered, so in the unkeyed hash they are not one inverse AES round away from
the packets (ANALYSIS.md, section 7). A group of np < 4 packets uses only the
first np feeds, multiplicands and targets; it still costs three steps.
Groups are absorbed one after the other; the step counter continues across
groups, so group u starts at t = 3u.

## 8. Finalization

After the last group, six more steps; the pending products of the last group
are xored in after the first of them:

    STEP()                                      t = 3 ceil(N/4)
    r_{g_k} = r_{g_k} ^ q_k   for the pending products
    STEP() five times                           t = 3 ceil(N/4) + 1 .. 3 ceil(N/4) + 5

Then the fold, with all additions lane-wise modulo 2^64:

    a = r0 + r1 + ... + r15
    s = aesenc(aesenc(a, r1 + r2), rot1(r3 + r4))
    out[l] = s[l] + s[l + 4]                   for l = 0..3

(`out` is lanes 0..3 of s + rot2(s).) The step counter is not used by the
fold; rot1 here is fixed.

## 9. Output

The 256-bit digest is the 32 bytes of out[0], out[1], out[2], out[3], each
written little-endian. The 64-bit digest is the 8 bytes of out[0] of the
computation with the 64-bit domain word (section 4); as a number it is
out[0]. It is not a part of the 256-bit digest of the same message.

## 10. Pseudocode

    function PikasekoHash(M, key = none, width = 32):     width: 32 or 8 bytes
        n = byte length of M
        if key is none: K = 0; D = 1 | width << 24
        else:
            hashed = len(key) > 64
            if hashed: key = PikasekoHash(key, none, 32)
            K = words of key || zeros (64 bytes), little-endian
            D = 1 | 1 << 8 | hashed << 9 | len(key) << 16 | width << 24
        for i in 0..15, l in 0..7:
            r_i[l] = ((PHI + i*PI) mod 2^32) * ((PHI + l*E) mod 2^32)
                     ^ rotl64(K[(l+i) mod 8], 4i mod 64)
        r_2[0] ^= D
        t = 0; pending = []
        P = packets of M || 0x80 || zeros || le64(n)       (N packets, section 6)
        for u in 0 .. ceil(N/4) - 1:
            Q  = P[4u .. min(4u+3, N-1)];  np = |Q|
            step(t); t += 1
            for (k, q) in pending: r[g[k]] ^= q
            for k < np: r[f[k]] += Q[k]
            step(t); t += 1
            step(t); t += 1
            pending = [(k, mul32(r[m[k]], r[f[k]])) for k < np]
        step(t); t += 1
        for (k, q) in pending: r[g[k]] ^= q
        repeat 5 times: step(t); t += 1
        a = sum of r_0 .. r_15
        s = aesenc(aesenc(a, r1 + r2), rot1(r3 + r4))
        out = (s[0]+s[4], s[1]+s[5], s[2]+s[6], s[3]+s[7])
        return the first width bytes of le64(out[0]) || .. || le64(out[3])

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

Message byte i is i mod 256; key byte i is i. Digests are given as bytes in
hex; "klen" is the key length in bytes (100 is hashed first).

    unkeyed256 len     0  b1ef4649151f3910e1cbbe7744ce15400fb62790873ac59bd5e9b875282750c7
    unkeyed64  len     0  505331b8934bd5bc
    keyed256 klen   0 len     0  c352fec6c1862efea18b465c20cbc47d35d2245e1eed79610a543d3fca60a868
    keyed256 klen  32 len     0  c6558ab3659f18f979981d0da35e48ae70d5ed68967cbc48a6787b4ca6bb6660
    keyed256 klen 100 len     0  08805c7425eeecde07b0d4b847df13406c10ca4f81bf2eb3f640b6c2f51c1c22
    keyed64  klen  32 len     0  3a390bac1d0e57c8
    unkeyed256 len     1  a8000f4ee8a9e6bc4053aeb4154d790e2e598d432287eaa8d5dc8faa2ba7bdcd
    unkeyed256 len    64  c3405d64d7e20a279f565c3d192ca1425798ecab7bf6241ac496803a7ca75426
    keyed256 klen  32 len    64  4bec62dd15437e787615ae0c93051a6670fc9a0f843deafc2c60b6c1d9422598

The 64-bit hash as a number is the digest read little-endian: for the empty
message, unkeyed, 0xbcd54b93b8315350. The complete list (lengths 0 to 4096,
key lengths 0, 32, 64 and 100, and a checksum over all lengths 0..2048) is
`test/test_vectors.txt`; `ref/pikaseko256.py --print-vectors` writes it.

## 12. Notes for implementers

* The step is not a permutation of the state (section 4 of ANALYSIS.md).
  Implementations must not assume invertibility.
* The key and the domain word enter only through the initial value.
* No packet enters before step 0, so the unkeyed states after step 0 (one per
  digest width) are constants; implementations may start from them
  (`src/pikaseko256_step0.h`).
* The AVX-512 implementation maps a vector to one zmm register: aesenc is
  VAESENC, rot1 and rot2 are VSHUFI64X2 with immediates 0x39 and 0x4E,
  mul32 is VPMULUDQ, the keys are VPXORQ (the step-constant key is one
  three-input VPTERNLOGQ).
* An implementation with 128-bit AES instructions applies AESENC per block;
  the only operation that moves data between blocks is the block rotation
  of the second-round key (and the final rot1, rot2 of the fold).
* `pikaseko256_steps(state, t0, nsteps)` in `include/pikaseko256_analysis.h` runs
  nsteps steps from step index t0 on a state given as 128 words, word
  8i + l = r_i[l]. It is meant for analysis and for checking an
  implementation of section 5 in isolation.
