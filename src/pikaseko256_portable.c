// PikasekoHash-256, portable C reference. Follows SPEC.md step by step.
// Byte-order independent; the AES S-box is computed (inversion in GF(2^8) by a fixed
// addition chain), so there are no tables and no secret-dependent memory accesses or branches.
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_consts.h"

typedef struct { uint64_t l[8]; } Reg;  // one 512-bit register: 8 lanes, 4 AES blocks

// ---- AES round (the vaesenc instruction on one 128-bit block) ----

static uint8_t GfMul(uint8_t a, uint8_t b) {  // constant time
  uint8_t p = 0;
  for (int i = 0; i < 8; i++) {
    p ^= (uint8_t)(-(b & 1) & a);
    a = (uint8_t)((a << 1) ^ (-(a >> 7) & 0x1b));
    b >>= 1;
  }
  return p;
}

static uint8_t SubByte(uint8_t x) {
  // x^254 = x^-1 (0 maps to 0): 2, 3, 6, 12, 15, 30, 60, 120, 126, 252, 254.
  uint8_t x2 = GfMul(x, x), x3 = GfMul(x2, x), x6 = GfMul(x3, x3), x12 = GfMul(x6, x6);
  uint8_t x15 = GfMul(x12, x3), x30 = GfMul(x15, x15), x60 = GfMul(x30, x30), x120 = GfMul(x60, x60);
  uint8_t x126 = GfMul(x120, x6), x252 = GfMul(x126, x126), y = GfMul(x252, x2);
  uint8_t s = y;
  for (int r = 1; r <= 4; r++) s ^= (uint8_t)((y << r) | (y >> (8 - r)));
  return s ^ 0x63;
}

static uint8_t Xtime(uint8_t a) { return (uint8_t)((a << 1) ^ (-(a >> 7) & 0x1b)); }

// One AES encryption round on a 16-byte block (byte 4c + r = row r of column c), then xor key.
static void AesRound(uint8_t s[16], const uint8_t k[16]) {
  uint8_t t[16];
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) t[4 * c + r] = SubByte(s[4 * ((c + r) & 3) + r]);  // SubBytes, ShiftRows
  for (int c = 0; c < 4; c++) {  // MixColumns
    uint8_t* a = t + 4 * c;
    uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3], all = a0 ^ a1 ^ a2 ^ a3;
    a[0] = a0 ^ all ^ Xtime(a0 ^ a1);
    a[1] = a1 ^ all ^ Xtime(a1 ^ a2);
    a[2] = a2 ^ all ^ Xtime(a2 ^ a3);
    a[3] = a3 ^ all ^ Xtime(a3 ^ a0);
  }
  for (int i = 0; i < 16; i++) s[i] = t[i] ^ k[i];
}

static void LanesToBytes(const uint64_t* l, uint8_t* b, int n) {
  for (int i = 0; i < n; i++) for (int j = 0; j < 8; j++) b[8 * i + j] = (uint8_t)(l[i] >> (8 * j));
}
static void BytesToLanes(const uint8_t* b, uint64_t* l, int n) {
  for (int i = 0; i < n; i++) {
    uint64_t v = 0;
    for (int j = 7; j >= 0; j--) v = (v << 8) | b[8 * i + j];
    l[i] = v;
  }
}

// aesenc on all four blocks of a register.
static Reg Aes(Reg x, Reg k) {
  uint8_t xb[64], kb[64];
  LanesToBytes(x.l, xb, 8); LanesToBytes(k.l, kb, 8);
  for (int b = 0; b < 4; b++) AesRound(xb + 16 * b, kb + 16 * b);
  BytesToLanes(xb, x.l, 8);
  return x;
}

static Reg Xor(Reg a, Reg b) { for (int i = 0; i < 8; i++) a.l[i] ^= b.l[i]; return a; }
static Reg Add(Reg a, Reg b) { for (int i = 0; i < 8; i++) a.l[i] += b.l[i]; return a; }
// rot_n: block b of the result is block (b + n) mod 4 of x.
static Reg Rot(Reg x, int n) {
  Reg o;
  for (int b = 0; b < 4; b++) { o.l[2 * b] = x.l[2 * ((b + n) & 3)]; o.l[2 * b + 1] = x.l[2 * ((b + n) & 3) + 1]; }
  return o;
}
static Reg Mul32(Reg a, Reg b) {  // low 32 bits of each lane, full 64-bit products
  for (int i = 0; i < 8; i++) a.l[i] = (uint64_t)(uint32_t)a.l[i] * (uint32_t)b.l[i];
  return a;
}

// ---- the step ----

static void Step(Reg r[16], unsigned t) {
  const int s = 1 << (t & 3), c = (s == 8) ? 1 : 2 * s, rn = (t & 1) ? 2 : 1;
  for (int i = 0; i < 16; i++) {
    if (i & (s | c)) continue;  // i is the lowest member of its group
    const int j = i ^ s, i2 = i ^ c, j2 = i ^ c ^ s;
    const Reg k = Xor(r[i], r[j]);
    Reg k2 = Xor(r[i2], r[j2]);
    if (i == 0) { Reg C; memcpy(C.l, kPikasekoStepConst[t & 3], 64); k2 = Xor(k2, C); }
    const Reg kk = Xor(k, k2), rk = Rot(k, rn), rk2 = Rot(k2, rn);
    const Reg ni = Aes(Aes(r[i], k2), rk), ni2 = Aes(Aes(r[i2], k), rk2);
    const Reg nj = Aes(Aes(r[j], kk), rk2), nj2 = Aes(Aes(r[j2], kk), rk);
    r[i] = ni; r[i2] = ni2; r[j] = nj; r[j2] = nj2;
  }
}

static const int kFeed[4] = {0, 5, 10, 15}, kMult[4] = {1, 4, 11, 14}, kTarget[4] = {3, 6, 9, 12};

// Absorb np (1..4) packets starting at step t; returns the next step index.
static unsigned Absorb(Reg r[16], unsigned t, const Reg* P, int np) {
  Step(r, t++);
  for (int k = 0; k < np; k++) r[kFeed[k]] = Add(r[kFeed[k]], P[k]);
  Step(r, t++);
  Reg q[4];
  for (int k = 0; k < np; k++) q[k] = Mul32(r[kMult[k]], r[kFeed[k]]);
  Step(r, t++);
  for (int k = 0; k < np; k++) r[kTarget[k]] = Xor(r[kTarget[k]], q[k]);
  return t;
}

static void Init(Reg r[16], const uint64_t key[8]) {
  for (int i = 0; i < 16; i++)
    for (int l = 0; l < 8; l++) {
      const uint32_t a = PIKASEKO_PHI + (uint32_t)i * PIKASEKO_PI, b = PIKASEKO_PHI + (uint32_t)l * PIKASEKO_E;
      uint64_t v = (uint64_t)a * b;
      if (key) {
        const uint64_t x = key[(l + i) & 7];
        const int n = (4 * i) & 63;
        v ^= n ? (x << n) | (x >> (64 - n)) : x;
      }
      r[i].l[l] = v;
    }
}

void pikaseko256_portable(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]) {
  const uint8_t* m = (const uint8_t*)data;
  Reg r[16];
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
    t = Absorb(r, t, P, np);
  }
  for (int f = 0; f < PIKASEKO_FINAL_STEPS; f++) Step(r, t++);
  // Fold: a = sum of the registers, two AES rounds keyed by r1 + r2 and rot1(r3 + r4),
  // then lanes 0..3 of s + rot2(s).
  Reg a = r[0];
  for (int i = 1; i < 16; i++) a = Add(a, r[i]);
  Reg s = Aes(Aes(a, Add(r[1], r[2])), Rot(Add(r[3], r[4]), 1));
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
  Reg r[16];
  for (int i = 0; i < 16; i++) memcpy(r[i].l, state + 8 * i, 64);
  for (unsigned e = 0; e < nsteps; e++) Step(r, t0 + e);
  for (int i = 0; i < 16; i++) memcpy(state + 8 * i, r[i].l, 64);
}
