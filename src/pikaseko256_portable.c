// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256, portable C reference. Follows SPEC.md step by step.
//
// The AES rounds are bitsliced: a 512-bit register (four AES blocks) is held as eight
// 64-bit "planes", plane i holding bit i of all 64 bytes. SubBytes is then a fixed
// Boolean circuit (the 115-gate circuit of Boyar and Peralta) evaluated with 64-bit
// AND/XOR on the planes, and ShiftRows, MixColumns and the block rotations become shifts
// and masks by constant amounts. The state stays bitsliced from step to step; only the
// registers that take part in the additions and products of absorption, and the final
// fold, are converted to the normal form and back.
//
// Constant time: there are no tables, and no array index, branch, loop bound or shift
// amount depends on the data or the key. The only branches depend on the message length,
// on the step index and on whether a key pointer is given. (The 32 x 32 -> 64-bit
// multiplication of mul32 is assumed to take constant time, as on current 64-bit CPUs.)
//
// Byte order: the message is read into 64-bit lanes byte by byte, and all other
// operations act on uint64_t values, so the result does not depend on the host.
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_consts.h"

typedef struct { uint64_t l[8]; } Reg;    // a 512-bit register in normal form: 8 lanes
typedef struct { uint64_t p[8]; } Slice;  // the same register bitsliced: 8 planes

// ---- Bitsliced layout ----
//
// Byte n = 4 col + row of block b is byte 16 b + n of the register (SPEC.md, section 1).
// Its bit i is bit 16 row + 4 col + b of plane i: a plane holds the four rows in four
// 16-bit fields, each field the four columns in four nibbles, each nibble the four blocks.
//
// In normal form the same bit is bit 8 m + i of lane l, with 8 l + m = 16 b + 4 col + row.
// Numbering the 512 bits as 64 word + bit, both layouts are placements of the nine index
// bits i0 i1 i2 row0 row1 col0 col1 b0 b1 (positions 0..5: bit within the word,
// positions 6..8: word number):
//
//   position:     0   1   2     3     4     5     6     7   8
//   normal form:  i0  i1  i2    row0  row1  col0  col1  b0  b1
//   bitsliced:    b0  b1  col0  col1  row0  row1  i2    i0  i1
//
// The conversion exchanges index bits six times: three exchanges between a bit within
// the word and a bit of the word number, then three within each word. The bitsliced word
// number comes out as i2 + 2 i0 + 4 i1.

// Bit q + d of *lo <-> bit q of *hi, for every q in m.
static inline void Exchange(uint64_t* lo, uint64_t* hi, int d, uint64_t m) {
  const uint64_t t = ((*lo >> d) ^ *hi) & m;
  *hi ^= t;
  *lo ^= t << d;
}

// Bit q <-> bit q + d of x, for every q in m.
static inline uint64_t DeltaSwap(uint64_t x, int d, uint64_t m) {
  const uint64_t t = (x ^ (x >> d)) & m;
  return x ^ t ^ (t << d);
}

// Within a word, positions 2..5 hold col1 row0 row1 col0; make it col0 col1 row0 row1 by
// exchanging index bits 2 <-> 5, 3 <-> 5 and 4 <-> 5. (Bit q <-> bit q + 2^b - 2^a, for
// q with index bit a set and index bit b clear.)
static inline uint64_t PermuteToSlice(uint64_t x) {
  x = DeltaSwap(x, 28, 0x00000000f0f0f0f0ull);  // 2 <-> 5
  x = DeltaSwap(x, 24, 0x00000000ff00ff00ull);  // 3 <-> 5
  return DeltaSwap(x, 16, 0x00000000ffff0000ull);  // 4 <-> 5
}
static inline uint64_t PermuteFromSlice(uint64_t x) {  // the inverse
  x = DeltaSwap(x, 16, 0x00000000ffff0000ull);
  x = DeltaSwap(x, 24, 0x00000000ff00ff00ull);
  return DeltaSwap(x, 28, 0x00000000f0f0f0f0ull);
}

// Exchanges of index bit a (positions 0..2, bit q + 2^a with q having index bit a clear)
// with a bit of the word number: masks of the positions with index bit a clear.
#define PIKASEKO_A0 0x5555555555555555ull
#define PIKASEKO_A1 0x3333333333333333ull
#define PIKASEKO_A2 0x0f0f0f0f0f0f0f0full

static Slice ToSlice(Reg x) {
  uint64_t w0 = x.l[0], w1 = x.l[1], w2 = x.l[2], w3 = x.l[3], w4 = x.l[4], w5 = x.l[5], w6 = x.l[6], w7 = x.l[7];
  // Positions 0 <-> 7 (i0 <-> b0): word k <-> word k + 2.
  Exchange(&w0, &w2, 1, PIKASEKO_A0); Exchange(&w1, &w3, 1, PIKASEKO_A0);
  Exchange(&w4, &w6, 1, PIKASEKO_A0); Exchange(&w5, &w7, 1, PIKASEKO_A0);
  // Positions 1 <-> 8 (i1 <-> b1): word k <-> word k + 4.
  Exchange(&w0, &w4, 2, PIKASEKO_A1); Exchange(&w1, &w5, 2, PIKASEKO_A1);
  Exchange(&w2, &w6, 2, PIKASEKO_A1); Exchange(&w3, &w7, 2, PIKASEKO_A1);
  // Positions 2 <-> 6 (i2 <-> col1): word k <-> word k + 1.
  Exchange(&w0, &w1, 4, PIKASEKO_A2); Exchange(&w2, &w3, 4, PIKASEKO_A2);
  Exchange(&w4, &w5, 4, PIKASEKO_A2); Exchange(&w6, &w7, 4, PIKASEKO_A2);
  // Word i2 + 2 i0 + 4 i1 is plane i.
  Slice s;
  s.p[0] = PermuteToSlice(w0); s.p[1] = PermuteToSlice(w2); s.p[2] = PermuteToSlice(w4); s.p[3] = PermuteToSlice(w6);
  s.p[4] = PermuteToSlice(w1); s.p[5] = PermuteToSlice(w3); s.p[6] = PermuteToSlice(w5); s.p[7] = PermuteToSlice(w7);
  return s;
}

static Reg FromSlice(Slice s) {  // the inverse: the same exchanges in reverse order
  uint64_t w0 = PermuteFromSlice(s.p[0]), w2 = PermuteFromSlice(s.p[1]), w4 = PermuteFromSlice(s.p[2]);
  uint64_t w6 = PermuteFromSlice(s.p[3]), w1 = PermuteFromSlice(s.p[4]), w3 = PermuteFromSlice(s.p[5]);
  uint64_t w5 = PermuteFromSlice(s.p[6]), w7 = PermuteFromSlice(s.p[7]);
  Exchange(&w0, &w1, 4, PIKASEKO_A2); Exchange(&w2, &w3, 4, PIKASEKO_A2);
  Exchange(&w4, &w5, 4, PIKASEKO_A2); Exchange(&w6, &w7, 4, PIKASEKO_A2);
  Exchange(&w0, &w4, 2, PIKASEKO_A1); Exchange(&w1, &w5, 2, PIKASEKO_A1);
  Exchange(&w2, &w6, 2, PIKASEKO_A1); Exchange(&w3, &w7, 2, PIKASEKO_A1);
  Exchange(&w0, &w2, 1, PIKASEKO_A0); Exchange(&w1, &w3, 1, PIKASEKO_A0);
  Exchange(&w4, &w6, 1, PIKASEKO_A0); Exchange(&w5, &w7, 1, PIKASEKO_A0);
  Reg x;
  x.l[0] = w0; x.l[1] = w1; x.l[2] = w2; x.l[3] = w3; x.l[4] = w4; x.l[5] = w5; x.l[6] = w6; x.l[7] = w7;
  return x;
}

// ---- AES round (the vaesenc instruction on the four blocks of a register) ----

// SubBytes on all 64 bytes at once: the circuit of J. Boyar and R. Peralta, "A new
// combinational logic minimization technique with applications to cryptology" (SEA 2010),
// 32 AND and 83 XOR gates, four of them XNOR. u0 is the most significant bit of the input
// byte, s0 that of the output.
static void SubBytes(uint64_t q[8]) {
  const uint64_t u0 = q[7], u1 = q[6], u2 = q[5], u3 = q[4], u4 = q[3], u5 = q[2], u6 = q[1], u7 = q[0];
  // Top linear layer.
  const uint64_t y14 = u3 ^ u5, y13 = u0 ^ u6, y9 = u0 ^ u3, y8 = u0 ^ u5, t0 = u1 ^ u2;
  const uint64_t y1 = t0 ^ u7, y4 = y1 ^ u3, y12 = y13 ^ y14, y2 = y1 ^ u0, y5 = y1 ^ u6;
  const uint64_t y3 = y5 ^ y8, t1 = u4 ^ y12, y15 = t1 ^ u5, y20 = t1 ^ u1, y6 = y15 ^ u7;
  const uint64_t y10 = y15 ^ t0, y11 = y20 ^ y9, y7 = u7 ^ y11, y17 = y10 ^ y11, y19 = y10 ^ y8;
  const uint64_t y16 = t0 ^ y11, y21 = y13 ^ y16, y18 = u0 ^ y16;
  // Nonlinear middle: the inversion.
  const uint64_t t2 = y12 & y15, t3 = y3 & y6, t4 = t3 ^ t2, t5 = y4 & u7, t6 = t5 ^ t2;
  const uint64_t t7 = y13 & y16, t8 = y5 & y1, t9 = t8 ^ t7, t10 = y2 & y7, t11 = t10 ^ t7;
  const uint64_t t12 = y9 & y11, t13 = y14 & y17, t14 = t13 ^ t12, t15 = y8 & y10, t16 = t15 ^ t12;
  const uint64_t t17 = t4 ^ t14, t18 = t6 ^ t16, t19 = t9 ^ t14, t20 = t11 ^ t16;
  const uint64_t t21 = t17 ^ y20, t22 = t18 ^ y19, t23 = t19 ^ y21, t24 = t20 ^ y18;
  const uint64_t t25 = t21 ^ t22, t26 = t21 & t23, t27 = t24 ^ t26, t28 = t25 & t27, t29 = t28 ^ t22;
  const uint64_t t30 = t23 ^ t24, t31 = t22 ^ t26, t32 = t31 & t30, t33 = t32 ^ t24, t34 = t23 ^ t33;
  const uint64_t t35 = t27 ^ t33, t36 = t24 & t35, t37 = t36 ^ t34, t38 = t27 ^ t36, t39 = t29 & t38;
  const uint64_t t40 = t25 ^ t39, t41 = t40 ^ t37, t42 = t29 ^ t33, t43 = t29 ^ t40, t44 = t33 ^ t37;
  const uint64_t t45 = t42 ^ t41;
  const uint64_t z0 = t44 & y15, z1 = t37 & y6, z2 = t33 & u7, z3 = t43 & y16, z4 = t40 & y1;
  const uint64_t z5 = t29 & y7, z6 = t42 & y11, z7 = t45 & y17, z8 = t41 & y10, z9 = t44 & y12;
  const uint64_t z10 = t37 & y3, z11 = t33 & y4, z12 = t43 & y13, z13 = t40 & y5, z14 = t29 & y2;
  const uint64_t z15 = t42 & y9, z16 = t45 & y14, z17 = t41 & y8;
  // Bottom linear layer, including the affine constant 0x63 (the four negations).
  const uint64_t t46 = z15 ^ z16, t47 = z10 ^ z11, t48 = z5 ^ z13, t49 = z9 ^ z10, t50 = z2 ^ z12;
  const uint64_t t51 = z2 ^ z5, t52 = z7 ^ z8, t53 = z0 ^ z3, t54 = z6 ^ z7, t55 = z16 ^ z17;
  const uint64_t t56 = z12 ^ t48, t57 = t50 ^ t53, t58 = z4 ^ t46, t59 = z3 ^ t54, t60 = t46 ^ t57;
  const uint64_t t61 = z14 ^ t57, t62 = t52 ^ t58, t63 = t49 ^ t58, t64 = z4 ^ t59, t65 = t61 ^ t62;
  const uint64_t t66 = z1 ^ t63, t67 = t64 ^ t65;
  const uint64_t s0 = t59 ^ t63, s6 = ~(t56 ^ t62), s7 = ~(t48 ^ t60), s3 = t53 ^ t66;
  const uint64_t s4 = t51 ^ t66, s5 = t47 ^ t65, s1 = ~(t64 ^ s3), s2 = ~(t55 ^ t67);
  q[7] = s0; q[6] = s1; q[5] = s2; q[4] = s3; q[3] = s4; q[2] = s5; q[1] = s6; q[0] = s7;
}

// ShiftRows on one plane: new column c of row r is old column c + r (mod 4), so the
// 16-bit field of row r rotates right by 4 r bits. Done in two stages: fields 1 and 3
// rotate by 4 bits, then fields 2 and 3 by 8 bits (an exchange of their two bytes).
static uint64_t ShiftRows(uint64_t x) {
  x = (x & 0x0000ffff0000ffffull) | ((x >> 4) & 0x0fff00000fff0000ull) | ((x << 12) & 0xf0000000f0000000ull);
  const uint64_t t = (x ^ (x >> 8)) & 0x00ff00ff00000000ull;
  return x ^ t ^ (t << 8);
}

static uint64_t Rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }  // 0 < n < 64

// One plane of MixColumns and AddRoundKey: a, s, x2 and k are the same plane of the
// ShiftRows output, of s (below), of 2 s and of the round key.
static uint64_t MixPlane(uint64_t a, uint64_t s, uint64_t x2, uint64_t k) {
  return x2 ^ s ^ a ^ Rotr(s, 32) ^ k;  // row r: 2 s_r ^ s_r ^ a_r ^ s_r+2
}

// aesenc(x, k) on bitsliced registers.
static Slice Aes(Slice x, const Slice* k) {
  SubBytes(x.p);
  const uint64_t a0 = ShiftRows(x.p[0]), a1 = ShiftRows(x.p[1]), a2 = ShiftRows(x.p[2]), a3 = ShiftRows(x.p[3]);
  const uint64_t a4 = ShiftRows(x.p[4]), a5 = ShiftRows(x.p[5]), a6 = ShiftRows(x.p[6]), a7 = ShiftRows(x.p[7]);
  // s_r = a_r ^ a_r+1 (rows mod 4): the 16-bit row fields rotate by one.
  const uint64_t s0 = a0 ^ Rotr(a0, 16), s1 = a1 ^ Rotr(a1, 16), s2 = a2 ^ Rotr(a2, 16), s3 = a3 ^ Rotr(a3, 16);
  const uint64_t s4 = a4 ^ Rotr(a4, 16), s5 = a5 ^ Rotr(a5, 16), s6 = a6 ^ Rotr(a6, 16), s7 = a7 ^ Rotr(a7, 16);
  // MixColumns: row r becomes 2 a_r ^ 3 a_r+1 ^ a_r+2 ^ a_r+3 = 2 s_r ^ a_r+1 ^ s_r+2.
  // Multiplication by 2 in GF(2^8) moves bit i to bit i + 1, and bit 7 to bits 0, 1, 3, 4.
  Slice y;
  y.p[0] = MixPlane(a0, s0, s7, k->p[0]);
  y.p[1] = MixPlane(a1, s1, s0 ^ s7, k->p[1]);
  y.p[2] = MixPlane(a2, s2, s1, k->p[2]);
  y.p[3] = MixPlane(a3, s3, s2 ^ s7, k->p[3]);
  y.p[4] = MixPlane(a4, s4, s3 ^ s7, k->p[4]);
  y.p[5] = MixPlane(a5, s5, s4, k->p[5]);
  y.p[6] = MixPlane(a6, s6, s5, k->p[6]);
  y.p[7] = MixPlane(a7, s7, s6, k->p[7]);
  return y;
}

static Slice XorS(Slice a, Slice b) { for (int i = 0; i < 8; i++) a.p[i] ^= b.p[i]; return a; }

// rot_n (n = 1, 2): block b of the result is block b + n (mod 4) of x, so every nibble
// (the four blocks of one byte position) rotates right by n bits.
static Slice RotS(Slice x, int n) {
  const uint64_t lo = n == 1 ? 0x7777777777777777ull : 0x3333333333333333ull;
  for (int i = 0; i < 8; i++) x.p[i] = ((x.p[i] >> n) & lo) | ((x.p[i] << (4 - n)) & ~lo);
  return x;
}

// ---- Normal-form operations ----

static void BytesToLanes(const uint8_t* b, uint64_t* l, int n) {
  for (int i = 0; i < n; i++) {
    uint64_t v = 0;
    for (int j = 7; j >= 0; j--) v = (v << 8) | b[8 * i + j];
    l[i] = v;
  }
}

static Reg Add(Reg a, Reg b) { for (int i = 0; i < 8; i++) a.l[i] += b.l[i]; return a; }
static Reg Mul32(Reg a, Reg b) {  // low 32 bits of each lane, full 64-bit products
  for (int i = 0; i < 8; i++) a.l[i] = (uint64_t)(uint32_t)a.l[i] * (uint32_t)b.l[i];
  return a;
}

// ---- The step, on the bitsliced state ----

static void SliceConsts(Slice cs[4]) {  // the step constants C[0..3], bitsliced
  for (int n = 0; n < 4; n++) {
    Reg c;
    memcpy(c.l, kPikasekoStepConst[n], 64);
    cs[n] = ToSlice(c);
  }
}

static void Step(Slice r[16], unsigned t, const Slice cs[4]) {
  const int s = 1 << (t & 3), c = (s == 8) ? 1 : 2 * s, rn = (t & 1) ? 2 : 1;
  for (int i = 0; i < 16; i++) {
    if (i & (s | c)) continue;  // i is the lowest member of its group
    const int j = i ^ s, i2 = i ^ c, j2 = i ^ c ^ s;
    const Slice k = XorS(r[i], r[j]);
    Slice k2 = XorS(r[i2], r[j2]);
    if (i == 0) k2 = XorS(k2, cs[t & 3]);
    const Slice kk = XorS(k, k2), rk = RotS(k, rn), rk2 = RotS(k2, rn);
    const Slice ni = Aes(Aes(r[i], &k2), &rk), ni2 = Aes(Aes(r[i2], &k), &rk2);
    const Slice nj = Aes(Aes(r[j], &kk), &rk2), nj2 = Aes(Aes(r[j2], &kk), &rk);
    r[i] = ni; r[i2] = ni2; r[j] = nj; r[j2] = nj2;
  }
}

static const int kFeed[4] = {0, 5, 10, 15}, kMult[4] = {1, 4, 11, 14}, kTarget[4] = {3, 6, 9, 12};

// Absorb np (1..4) packets starting at step t; returns the next step index. The additions
// and products need the normal form; the xor of q is the same in either form.
static unsigned Absorb(Slice r[16], unsigned t, const Reg* P, int np, const Slice cs[4]) {
  Step(r, t++, cs);
  for (int k = 0; k < np; k++) r[kFeed[k]] = ToSlice(Add(FromSlice(r[kFeed[k]]), P[k]));
  Step(r, t++, cs);
  Slice q[4];
  for (int k = 0; k < np; k++) q[k] = ToSlice(Mul32(FromSlice(r[kMult[k]]), FromSlice(r[kFeed[k]])));
  Step(r, t++, cs);
  for (int k = 0; k < np; k++) r[kTarget[k]] = XorS(r[kTarget[k]], q[k]);
  return t;
}

static void Init(Slice r[16], const uint64_t key[8]) {
  for (int i = 0; i < 16; i++) {
    Reg x;
    for (int l = 0; l < 8; l++) {
      const uint32_t a = PIKASEKO_PHI + (uint32_t)i * PIKASEKO_PI, b = PIKASEKO_PHI + (uint32_t)l * PIKASEKO_E;
      uint64_t v = (uint64_t)a * b;
      if (key) {
        const uint64_t y = key[(l + i) & 7];
        const int n = (4 * i) & 63;
        v ^= n ? (y << n) | (y >> (64 - n)) : y;
      }
      x.l[l] = v;
    }
    r[i] = ToSlice(x);
  }
}

void pikaseko256_portable(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]) {
  const uint8_t* m = (const uint8_t*)data;
  Slice cs[4];
  SliceConsts(cs);
  Slice r[16];
  Init(r, key);
  // Packets: len / 64 full ones, then the padding (1 or 2 packets).
  const size_t full = len / 64, rem = len % 64;
  uint8_t tail[128];
  memset(tail, 0, sizeof tail);
  if (rem) memcpy(tail, m + 64 * full, rem);
  tail[rem] = 0x80;
  const size_t ntail = rem < 56 ? 1 : 2;
  for (int b = 0; b < 8; b++) tail[64 * ntail - 8 + b] = (uint8_t)((uint64_t)len >> (8 * b));
  const size_t npk = full + ntail;
  unsigned t = 0;
  for (size_t g = 0; g < npk; g += 4) {
    const int np = npk - g >= 4 ? 4 : (int)(npk - g);
    Reg P[4];
    for (int k = 0; k < np; k++) {
      const size_t idx = g + k;
      BytesToLanes(idx < full ? m + 64 * idx : tail + 64 * (idx - full), P[k].l, 8);
    }
    t = Absorb(r, t, P, np, cs);
  }
  for (int f = 0; f < PIKASEKO_FINAL_STEPS; f++) Step(r, t++, cs);
  // Fold: a = sum of the registers, two AES rounds keyed by r1 + r2 and rot1(r3 + r4),
  // then lanes 0..3 of s + rot2(s).
  Reg x[16];
  for (int i = 0; i < 16; i++) x[i] = FromSlice(r[i]);
  Reg a = x[0];
  for (int i = 1; i < 16; i++) a = Add(a, x[i]);
  const Slice k1 = ToSlice(Add(x[1], x[2])), k2 = RotS(ToSlice(Add(x[3], x[4])), 1);
  const Reg s = FromSlice(Aes(Aes(ToSlice(a), &k1), &k2));
  for (int l = 0; l < 4; l++) out[l] = s.l[l] + s.l[l + 4];
}

void pikaseko256_unkeyed_portable(const void* data, size_t len, uint64_t out[4]) {
  pikaseko256_portable(data, len, NULL, out);
}

uint64_t pikaseko64_portable(const void* data, size_t len, const uint64_t key[8]) {
  uint64_t out[4];
  pikaseko256_portable(data, len, key, out);
  return out[0];
}

void pikaseko256_steps_portable(uint64_t state[128], unsigned t0, unsigned nsteps) {
  Slice cs[4];
  SliceConsts(cs);
  Slice r[16];
  for (int i = 0; i < 16; i++) {
    Reg x;
    memcpy(x.l, state + 8 * i, 64);
    r[i] = ToSlice(x);
  }
  for (unsigned e = 0; e < nsteps; e++) Step(r, t0 + e, cs);
  for (int i = 0; i < 16; i++) {
    const Reg x = FromSlice(r[i]);
    memcpy(state + 8 * i, x.l, 64);
  }
}
