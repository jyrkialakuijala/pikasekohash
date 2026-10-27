// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256 without AVX-512: the body shared by the AVX2 + VAES build (a 512-bit
// register is two 256-bit vectors), the AES-NI build and the ARMv8 build (four 128-bit
// vectors, one per AES block). The includer defines
//   S               the vector type, W the number of vectors per register (2 or 4)
//   SXor SOr SAdd SAes SMul SLoad SStore   lane-wise operations (64-bit lanes, AES per block)
//   SShl(x, n) SShr(x, n)   64-bit lane shifts by a run-time count 0..64 (64 gives zero)
//   ROT1(x) ROT2(x) the block rotations on a register (R)
//   SUFFIX          the name suffix of the exported functions
//   UNROLL_FINALS   1: the final steps with compile-time phases (faster for AVX2, slower
//                   for AES-NI, whose step is four times the code)
// Same structure as pikaseko256_avx512.c: steps are written with a compile-time phase.
//
// Constant time: no branch or memory access depends on the data or the key; branches and
// copy lengths depend only on the length and on whether a key pointer is given.
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"
#include "pikaseko256_compiler.h"
#include "pikaseko256_consts.h"
#include "pikaseko256_step0.h"
#include "pikaseko256_impl.h"

#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define NAME(x) CAT(x, SUFFIX)
#define UNROLL_W PIKASEKO_UNROLL(4)

typedef struct { S v[W]; } R;  // one 512-bit register

INL R Xor(R a, R b) { R r; UNROLL_W for (int i = 0; i < W; i++) r.v[i] = SXor(a.v[i], b.v[i]); return r; }
INL R Add(R a, R b) { R r; UNROLL_W for (int i = 0; i < W; i++) r.v[i] = SAdd(a.v[i], b.v[i]); return r; }
INL R Aes(R a, R k) { R r; UNROLL_W for (int i = 0; i < W; i++) r.v[i] = SAes(a.v[i], k.v[i]); return r; }
INL R Mul(R a, R b) { R r; UNROLL_W for (int i = 0; i < W; i++) r.v[i] = SMul(a.v[i], b.v[i]); return r; }
// Lane-wise rotation left by n (0..63): a shift by 64 gives zero on every target.
INL R Rotl(R a, int n) { R r; UNROLL_W for (int i = 0; i < W; i++) r.v[i] = SOr(SShl(a.v[i], n), SShr(a.v[i], 64 - n)); return r; }
INL R Load(const void* p) { R r; UNROLL_W for (int i = 0; i < W; i++) r.v[i] = SLoad((const uint8_t*)p + (64 / W) * i); return r; }
INL void Store(void* p, R x) { UNROLL_W for (int i = 0; i < W; i++) SStore((uint8_t*)p + (64 / W) * i, x.v[i]); }

typedef struct { R c[4]; } Consts;  // step constants

static void LoadConsts(Consts* cs) {
  for (int n = 0; n < 4; n++) cs->c[n] = Load(kPikasekoStepConst[n]);
}

// One step with phase p = t mod 4 (a compile-time constant at every call site).
INL void Step(R r[16], const int p, const Consts* cs) {
  const int s = 1 << p, c = (s == 8) ? 1 : 2 * s;
PIKASEKO_UNROLL(16)
  for (int i = 0; i < 16; i++) {
    if (i & (s | c)) continue;
    const int j = i ^ s, i2 = i ^ c, j2 = i ^ c ^ s;
    const R k = Xor(r[i], r[j]);
    const R k2 = (i == 0) ? Xor(Xor(r[i2], r[j2]), cs->c[p]) : Xor(r[i2], r[j2]);
    const R kk = Xor(k, k2);
    const R rk = (p & 1) ? ROT2(k) : ROT1(k), rk2 = (p & 1) ? ROT2(k2) : ROT1(k2);
    const R ni = Aes(Aes(r[i], k2), rk), ni2 = Aes(Aes(r[i2], k), rk2);
    const R nj = Aes(Aes(r[j], kk), rk2), nj2 = Aes(Aes(r[j2], kk), rk);
    r[i] = ni; r[i2] = ni2; r[j] = nj; r[j2] = nj2;
  }
}

INL void StepAny(R r[16], unsigned t, const Consts* cs) {
  switch (t & 3) {
    case 0: Step(r, 0, cs); break;
    case 1: Step(r, 1, cs); break;
    case 2: Step(r, 2, cs); break;
    default: Step(r, 3, cs); break;
  }
}

// As in pikaseko256_avx512.c, the state is kept one step ahead: an absorption at step index
// t starts after step t and ends with step t + 3.

// Absorption of np (1..4) packets at phase p = t mod 4. np depends on the length only.
INL void Absorb(R r[16], const int p, const R* P, const int np, const Consts* cs) {
  r[0] = Add(r[0], P[0]);
  if (np > 1) r[5] = Add(r[5], P[1]);
  if (np > 2) r[10] = Add(r[10], P[2]);
  if (np > 3) r[15] = Add(r[15], P[3]);
  Step(r, (p + 1) & 3, cs);
  R q0 = Mul(r[1], r[0]), q1 = r[0], q2 = r[0], q3 = r[0];
  if (np > 1) q1 = Mul(r[4], r[5]);
  if (np > 2) q2 = Mul(r[11], r[10]);
  if (np > 3) q3 = Mul(r[14], r[15]);
  Step(r, (p + 2) & 3, cs);
  r[3] = Xor(r[3], q0);
  if (np > 1) r[6] = Xor(r[6], q1);
  if (np > 2) r[9] = Xor(r[9], q2);
  if (np > 3) r[12] = Xor(r[12], q3);
  Step(r, (p + 3) & 3, cs);
}

INL void AbsorbAny(R r[16], unsigned t, const R* P, int np, const Consts* cs) {
  switch (t & 3) {
    case 0: Absorb(r, 0, P, np, cs); break;
    case 1: Absorb(r, 1, P, np, cs); break;
    case 2: Absorb(r, 2, P, np, cs); break;
    default: Absorb(r, 3, P, np, cs); break;
  }
}

// Final steps 2..6 (the first ran at the end of the last absorption), from phase p.
INL void Finals(R r[16], const int p, const Consts* cs) {
  Step(r, p, cs); Step(r, (p + 1) & 3, cs); Step(r, (p + 2) & 3, cs);
  Step(r, (p + 3) & 3, cs); Step(r, p, cs);
}

// Absorbs ngroups groups of four full packets from p, starting at step index t; returns
// the next step index. From phase 0 the loop takes sixteen packets per iteration.
static unsigned AbsorbGroups(R r[16], unsigned t, const uint8_t* p, size_t ngroups, const Consts* cs) {
  while (ngroups && (t & 3)) {
    const R P[4] = {Load(p), Load(p + 64), Load(p + 128), Load(p + 192)};
    AbsorbAny(r, t, P, 4, cs);
    t += 3; p += 256; ngroups--;
  }
  for (; ngroups >= 4; ngroups -= 4, p += 1024, t += 12) {
    R P[4];
    P[0] = Load(p); P[1] = Load(p + 64); P[2] = Load(p + 128); P[3] = Load(p + 192);
    Absorb(r, 0, P, 4, cs);
    P[0] = Load(p + 256); P[1] = Load(p + 320); P[2] = Load(p + 384); P[3] = Load(p + 448);
    Absorb(r, 3, P, 4, cs);
    P[0] = Load(p + 512); P[1] = Load(p + 576); P[2] = Load(p + 640); P[3] = Load(p + 704);
    Absorb(r, 2, P, 4, cs);
    P[0] = Load(p + 768); P[1] = Load(p + 832); P[2] = Load(p + 896); P[3] = Load(p + 960);
    Absorb(r, 1, P, 4, cs);
  }
  for (; ngroups; ngroups--, p += 256, t += 3) {
    const R P[4] = {Load(p), Load(p + 64), Load(p + 128), Load(p + 192)};
    AbsorbAny(r, t, P, 4, cs);
  }
  return t;
}

// The last n (0..255) message bytes at m, the padding, the final steps and the fold.
static void Finish(R r[16], unsigned t, const uint8_t* m, size_t n, uint64_t len, const Consts* cs, uint64_t out[4]) {
  const size_t full = n / 64, rem = n % 64, ntail = rem < 56 ? 1 : 2, npk = full + ntail;  // npk: 1..5
  uint8_t tail[320];
  memset(tail, 0, sizeof tail);
  if (n) memcpy(tail, m, n);
  tail[n] = 0x80;
  for (int b = 0; b < 8; b++) tail[64 * npk - 8 + b] = (uint8_t)(len >> (8 * b));
  R P[4];
  const size_t first = npk < 4 ? npk : 4;
  for (size_t k = 0; k < first; k++) P[k] = Load(tail + 64 * k);
  AbsorbAny(r, t, P, (int)first, cs);
  t += 3;
  if (npk == 5) {
    P[0] = Load(tail + 256);
    AbsorbAny(r, t, P, 1, cs);
    t += 3;
  }
  // t: the first final step, already run. The other five:
#if UNROLL_FINALS
  switch ((t + 1) & 3) {  // with compile-time phases
    case 0: Finals(r, 0, cs); break;
    case 1: Finals(r, 1, cs); break;
    case 2: Finals(r, 2, cs); break;
    default: Finals(r, 3, cs); break;
  }
#else
  for (int f = 1; f < PIKASEKO_FINAL_STEPS; f++) StepAny(r, ++t, cs);  // smaller code
#endif
  R a = r[0];
  for (int i = 1; i < 16; i++) a = Add(a, r[i]);
  R s = Aes(Aes(a, Add(r[1], r[2])), ROT1(Add(r[3], r[4])));
  s = Add(s, ROT2(s));
  uint64_t w[8];
  Store(w, s);
  memcpy(out, w, 32);
}

INL void Init(R r[16], const uint64_t key[8]) {
  // r_i[l] = (phi + i pi) * (phi + l e), 32 x 32 -> 64 bits: one multiply per vector.
  static const uint64_t kLane[8] = {
      (uint32_t)(PIKASEKO_PHI + 0u * PIKASEKO_E), (uint32_t)(PIKASEKO_PHI + 1u * PIKASEKO_E),
      (uint32_t)(PIKASEKO_PHI + 2u * PIKASEKO_E), (uint32_t)(PIKASEKO_PHI + 3u * PIKASEKO_E),
      (uint32_t)(PIKASEKO_PHI + 4u * PIKASEKO_E), (uint32_t)(PIKASEKO_PHI + 5u * PIKASEKO_E),
      (uint32_t)(PIKASEKO_PHI + 6u * PIKASEKO_E), (uint32_t)(PIKASEKO_PHI + 7u * PIKASEKO_E)};
  const R lane = Load(kLane);
  for (int i = 0; i < 16; i++) {
    const uint64_t a = (uint32_t)(PIKASEKO_PHI + (uint32_t)i * PIKASEKO_PI);
    const uint64_t av[8] = {a, a, a, a, a, a, a, a};
    r[i] = Mul(Load(av), lane);
  }
  if (key) {  // depends on the pointer only, never on the key value
    uint64_t k2[16];  // the key twice: lane l of k2 + (i mod 8) is key[(l + i) mod 8]
    memcpy(k2, key, 64);
    memcpy(k2 + 8, key, 64);
    for (int i = 0; i < 16; i++) r[i] = Xor(r[i], Rotl(Load(k2 + (i & 7)), 4 * i));  // 4i < 64
  }
}

// The state after step 0: computed for a key, loaded for the unkeyed hash.
static void Start(R r[16], const uint64_t key[8], const Consts* cs) {
  if (key) {  // depends on the pointer only, never on the key value
    Init(r, key);
    Step(r, 0, cs);
  } else {
    for (int i = 0; i < 16; i++) r[i] = Load(kPikasekoUnkeyedStep0 + 8 * i);
  }
}

void NAME(pikaseko256)(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]) {
  const uint8_t* p = (const uint8_t*)data;
  Consts cs;
  LoadConsts(&cs);
  R r[16];
  Start(r, key, &cs);
  const unsigned t = AbsorbGroups(r, 0, p, len / 256, &cs);
  Finish(r, t, p + (len - len % 256), len % 256, len, &cs, out);
}

static void LoadState(R r[16], const pikaseko256_ctx* ctx) {
  for (int i = 0; i < 16; i++) r[i] = Load(ctx->r + 8 * i);
}
static void StoreState(pikaseko256_ctx* ctx, const R r[16]) {
  for (int i = 0; i < 16; i++) Store(ctx->r + 8 * i, r[i]);
}

void NAME(pikaseko256_init)(pikaseko256_ctx* ctx, const uint64_t key[8]) {
  Consts cs;
  LoadConsts(&cs);
  R r[16];
  Start(r, key, &cs);
  StoreState(ctx, r);
  ctx->len = 0; ctx->t = 0; ctx->nbuf = 0;
}

void NAME(pikaseko256_update)(pikaseko256_ctx* ctx, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  ctx->len += len;
  if (len < 256 - (size_t)ctx->nbuf) {
    if (len) memcpy(ctx->buf + ctx->nbuf, p, len);
    ctx->nbuf += (unsigned)len;
    return;
  }
  Consts cs;
  LoadConsts(&cs);
  R r[16];
  LoadState(r, ctx);
  unsigned t = ctx->t;
  if (ctx->nbuf) {
    const size_t take = 256 - ctx->nbuf;
    memcpy(ctx->buf + ctx->nbuf, p, take);
    t = AbsorbGroups(r, t, ctx->buf, 1, &cs);
    p += take; len -= take; ctx->nbuf = 0;
  }
  t = AbsorbGroups(r, t, p, len / 256, &cs);
  p += len - len % 256;
  ctx->nbuf = (unsigned)(len % 256);
  if (ctx->nbuf) memcpy(ctx->buf, p, ctx->nbuf);
  ctx->t = t;
  StoreState(ctx, r);
}

void NAME(pikaseko256_final)(const pikaseko256_ctx* ctx, uint64_t out[4]) {
  Consts cs;
  LoadConsts(&cs);
  R r[16];
  LoadState(r, ctx);
  Finish(r, ctx->t, ctx->buf, ctx->nbuf, ctx->len, &cs, out);
}

void NAME(pikaseko256_steps)(uint64_t state[128], unsigned t0, unsigned nsteps) {
  Consts cs;
  LoadConsts(&cs);
  R r[16];
  for (int i = 0; i < 16; i++) r[i] = Load(state + 8 * i);
  for (unsigned e = 0; e < nsteps; e++) StepAny(r, t0 + e, &cs);
  for (int i = 0; i < 16; i++) Store(state + 8 * i, r[i]);
}
