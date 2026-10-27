// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256, AVX-512 implementation (AVX512F, AVX512BW, VAES).
// The sixteen state registers live in zmm registers. Steps are written with a
// compile-time phase so that strides, rotations and the step constant are immediates;
// the bulk loop absorbs 16 packets (four groups, twelve steps) per iteration.
//
// Constant time: no branch or memory access depends on the data or the key; the only
// branches depend on the length and on whether a key pointer is given.
#include <immintrin.h>
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"
#include "pikaseko256_consts.h"

typedef __m512i V;
#define INL static inline __attribute__((always_inline))

INL V Xor(V a, V b) { return _mm512_xor_si512(a, b); }
INL V Add(V a, V b) { return _mm512_add_epi64(a, b); }
INL V Aes(V x, V k) { return _mm512_aesenc_epi128(x, k); }
INL V Rot1(V x) { return _mm512_shuffle_i64x2(x, x, 0x39); }  // block b <- block b + 1
INL V Rot2(V x) { return _mm512_shuffle_i64x2(x, x, 0x4E); }  // block b <- block b + 2

typedef struct { V c[4]; } Consts;  // step constants

// One step with phase p = t mod 4 (a compile-time constant at every call site).
INL void Step(V r[16], const int p, const Consts* cs) {
  const int s = 1 << p, c = (s == 8) ? 1 : 2 * s;
#pragma GCC unroll 16
  for (int i = 0; i < 16; i++) {
    if (i & (s | c)) continue;
    const int j = i ^ s, i2 = i ^ c, j2 = i ^ c ^ s;
    const V k = Xor(r[i], r[j]);
    const V k2 = (i == 0) ? _mm512_ternarylogic_epi64(r[i2], r[j2], cs->c[p], 0x96) : Xor(r[i2], r[j2]);
    const V kk = Xor(k, k2);
    const V rk = (p & 1) ? Rot2(k) : Rot1(k), rk2 = (p & 1) ? Rot2(k2) : Rot1(k2);
    const V ni = Aes(Aes(r[i], k2), rk), ni2 = Aes(Aes(r[i2], k), rk2);
    const V nj = Aes(Aes(r[j], kk), rk2), nj2 = Aes(Aes(r[j2], kk), rk);
    r[i] = ni; r[i2] = ni2; r[j] = nj; r[j2] = nj2;
  }
}

INL void StepAny(V r[16], unsigned t, const Consts* cs) {
  switch (t & 3) {
    case 0: Step(r, 0, cs); break;
    case 1: Step(r, 1, cs); break;
    case 2: Step(r, 2, cs); break;
    default: Step(r, 3, cs); break;
  }
}

// Absorption of np packets (np compile-time 1..4) starting at phase p.
INL void Absorb(V r[16], const int p, const V* P, const int np, const Consts* cs) {
  Step(r, p, cs);
  if (np > 0) r[0] = Add(r[0], P[0]);
  if (np > 1) r[5] = Add(r[5], P[1]);
  if (np > 2) r[10] = Add(r[10], P[2]);
  if (np > 3) r[15] = Add(r[15], P[3]);
  Step(r, (p + 1) & 3, cs);
  V q0 = r[0], q1 = r[0], q2 = r[0], q3 = r[0];
  if (np > 0) q0 = _mm512_mul_epu32(r[1], r[0]);
  if (np > 1) q1 = _mm512_mul_epu32(r[4], r[5]);
  if (np > 2) q2 = _mm512_mul_epu32(r[11], r[10]);
  if (np > 3) q3 = _mm512_mul_epu32(r[14], r[15]);
  Step(r, (p + 2) & 3, cs);
  if (np > 0) r[3] = Xor(r[3], q0);
  if (np > 1) r[6] = Xor(r[6], q1);
  if (np > 2) r[9] = Xor(r[9], q2);
  if (np > 3) r[12] = Xor(r[12], q3);
}

// Absorption of the last group: np (1..4) packets known only at run time. Missing
// packets are zero and their products are masked off, which equals absorbing np packets.
INL void AbsorbMasked(V r[16], const int p, const V* P, const V* m, const Consts* cs) {
  Step(r, p, cs);
  r[0] = Add(r[0], P[0]); r[5] = Add(r[5], P[1]); r[10] = Add(r[10], P[2]); r[15] = Add(r[15], P[3]);
  Step(r, (p + 1) & 3, cs);
  const V q0 = _mm512_mul_epu32(r[1], r[0]), q1 = _mm512_and_si512(_mm512_mul_epu32(r[4], r[5]), m[1]);
  const V q2 = _mm512_and_si512(_mm512_mul_epu32(r[11], r[10]), m[2]), q3 = _mm512_and_si512(_mm512_mul_epu32(r[14], r[15]), m[3]);
  Step(r, (p + 2) & 3, cs);
  r[3] = Xor(r[3], q0); r[6] = Xor(r[6], q1); r[9] = Xor(r[9], q2); r[12] = Xor(r[12], q3);
}

INL void Finals(V r[16], const int p, const Consts* cs) {
  Step(r, p, cs); Step(r, (p + 1) & 3, cs); Step(r, (p + 2) & 3, cs);
  Step(r, (p + 3) & 3, cs); Step(r, p, cs); Step(r, (p + 1) & 3, cs);
}

// The last one or two groups (np = 1..5 packets in P[0..7], unused ones zero) and the
// six final steps, starting at phase p.
INL void Tail(V r[16], const int p, const V* P, int np, const Consts* cs) {
  const V ones = _mm512_set1_epi64(-1), zero = _mm512_setzero_si512();
  V m[4] = {ones, np > 1 ? ones : zero, np > 2 ? ones : zero, np > 3 ? ones : zero};
  if (np <= 4) {
    AbsorbMasked(r, p, P, m, cs);
    Finals(r, (p + 3) & 3, cs);
  } else {
    AbsorbMasked(r, p, P, m, cs);
    m[1] = m[2] = m[3] = zero;
    AbsorbMasked(r, (p + 3) & 3, P + 4, m, cs);
    Finals(r, (p + 2) & 3, cs);
  }
}

INL V Load(const uint8_t* p) { return _mm512_loadu_si512((const void*)p); }

INL void Init(V r[16], const uint64_t key[8]) {
  const V lane = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
  // phi + l e (mod 2^32) in the low half of lane l
  const V bl = _mm512_and_si512(_mm512_add_epi64(_mm512_set1_epi64(PIKASEKO_PHI),
                   _mm512_mul_epu32(lane, _mm512_set1_epi64(PIKASEKO_E))), _mm512_set1_epi64(0xffffffff));
#pragma GCC unroll 16
  for (int i = 0; i < 16; i++) r[i] = _mm512_mul_epu32(_mm512_set1_epi64(PIKASEKO_PHI + (uint32_t)i * PIKASEKO_PI), bl);
  if (key) {  // depends on the pointer only, never on the key value
    const V K = _mm512_loadu_si512((const void*)key);
#pragma GCC unroll 16
    for (int i = 0; i < 16; i++) {
      // lane l: rotl64(K[(l + i) mod 8], 4i mod 64)
      const V Ki = _mm512_permutexvar_epi64(_mm512_and_si512(_mm512_add_epi64(lane, _mm512_set1_epi64(i)), _mm512_set1_epi64(7)), K);
      r[i] = Xor(r[i], _mm512_rolv_epi64(Ki, _mm512_set1_epi64((4 * i) & 63)));
    }
  }
}

INL V Fold(V r[16]) {
  const V a0 = Add(Add(Add(r[0], r[1]), Add(r[2], r[3])), Add(Add(r[4], r[5]), Add(r[6], r[7])));
  const V a1 = Add(Add(Add(r[8], r[9]), Add(r[10], r[11])), Add(Add(r[12], r[13]), Add(r[14], r[15])));
  V s = Aes(Add(a0, a1), Add(r[1], r[2]));
  s = Aes(s, Rot1(Add(r[3], r[4])));
  return Add(s, Rot2(s));
}

static void LoadConsts(Consts* cs) {
  for (int n = 0; n < 4; n++) cs->c[n] = _mm512_loadu_si512((const void*)kPikasekoStepConst[n]);
}

// Absorbs one group of four full packets at p, at step index t (any phase).
INL void AbsorbAt(V r[16], unsigned t, const uint8_t* p, const Consts* cs) {
  V P[4] = {Load(p), Load(p + 64), Load(p + 128), Load(p + 192)};
  switch (t & 3) {
    case 0: Absorb(r, 0, P, 4, cs); break;
    case 1: Absorb(r, 1, P, 4, cs); break;
    case 2: Absorb(r, 2, P, 4, cs); break;
    default: Absorb(r, 3, P, 4, cs); break;
  }
}

// Absorbs ngroups groups of four full packets (256 bytes each) from p, starting at step
// index t; returns the next step index. Groups start at phases 0, 3, 2, 1 in turn; from
// phase 0 the bulk loop takes sixteen packets (twelve steps) per iteration. (The one-shot
// function starts at t = 0; only streaming needs the alignment loop.)
INL unsigned AbsorbGroups(V r[16], unsigned t, const uint8_t* p, size_t ngroups, const Consts* cs) {
  while (ngroups && (t & 3)) { AbsorbAt(r, t, p, cs); t += 3; p += 256; ngroups--; }
  for (; ngroups >= 4; ngroups -= 4, p += 1024, t += 12) {
    V P[4];
    P[0] = Load(p); P[1] = Load(p + 64); P[2] = Load(p + 128); P[3] = Load(p + 192);
    Absorb(r, 0, P, 4, cs);
    P[0] = Load(p + 256); P[1] = Load(p + 320); P[2] = Load(p + 384); P[3] = Load(p + 448);
    Absorb(r, 3, P, 4, cs);
    P[0] = Load(p + 512); P[1] = Load(p + 576); P[2] = Load(p + 640); P[3] = Load(p + 704);
    Absorb(r, 2, P, 4, cs);
    P[0] = Load(p + 768); P[1] = Load(p + 832); P[2] = Load(p + 896); P[3] = Load(p + 960);
    Absorb(r, 1, P, 4, cs);
  }
  // 0..3 more groups: phases 0, 3, 2.
  if (ngroups) {
    V P[4] = {Load(p), Load(p + 64), Load(p + 128), Load(p + 192)};
    Absorb(r, 0, P, 4, cs);
    if (ngroups > 1) {
      V Q[4] = {Load(p + 256), Load(p + 320), Load(p + 384), Load(p + 448)};
      Absorb(r, 3, Q, 4, cs);
      if (ngroups > 2) {
        V U[4] = {Load(p + 512), Load(p + 576), Load(p + 640), Load(p + 704)};
        Absorb(r, 2, U, 4, cs);
      }
    }
    t += 3 * (unsigned)ngroups;
  }
  return t;
}

// The last n (0..255) message bytes at p, the padding, the six final steps and the fold.
// t is the next step index, len the length of the whole message.
INL void Finish(V r[16], unsigned t, const uint8_t* p, size_t n, uint64_t len, const Consts* cs, uint64_t out[4]) {
  const size_t full = n / 64, rem = n % 64;
  // 0..3 full packets, then the padding packet(s): 0x80, zeros, length in lane 7.
  // Built with masked loads and blends (masked-off lanes are never read, so nothing
  // past the end of the input is touched).
  const uint8_t* q = p + 64 * full;
  V pad = _mm512_maskz_loadu_epi8(rem ? (((__mmask64)1 << rem) - 1) : 0, q);
  pad = _mm512_mask_mov_epi8(pad, (__mmask64)1 << rem, _mm512_set1_epi8((char)0x80));
  const V lenv = _mm512_set1_epi64((long long)len);
  const int two = rem >= 56;  // the length needs a packet of its own
  if (!two) pad = _mm512_mask_mov_epi64(pad, 0x80, lenv);
  const V pad2 = _mm512_maskz_mov_epi64(0x80, lenv);
  V P[8];
#pragma GCC unroll 8
  for (int k = 0; k < 8; k++) {
    V x = _mm512_maskz_loadu_epi64((size_t)k < full ? 0xff : 0, p + 64 * k);
    x = _mm512_mask_mov_epi64(x, (size_t)k == full ? 0xff : 0, pad);
    x = _mm512_mask_mov_epi64(x, (two && (size_t)k == full + 1) ? 0xff : 0, pad2);
    P[k] = x;
  }
  const int np = (int)full + 1 + two;
  switch (t & 3) {
    case 0: Tail(r, 0, P, np, cs); break;
    case 1: Tail(r, 1, P, np, cs); break;
    case 2: Tail(r, 2, P, np, cs); break;
    default: Tail(r, 3, P, np, cs); break;
  }
  _mm256_storeu_si256((__m256i*)out, _mm512_castsi512_si256(Fold(r)));
}

void pikaseko256_avx512(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]) {
  const uint8_t* p = (const uint8_t*)data;
  Consts cs; LoadConsts(&cs);
  V r[16];
  Init(r, key);
  const unsigned t = AbsorbGroups(r, 0, p, len / 256, &cs);
  Finish(r, t, p + (len & ~(size_t)255), len % 256, len, &cs, out);
}

// Streaming. The context holds the state in normal form (word 8i + l = lane l of r_i), the
// next step index and the bytes of the current, incomplete group of four packets. A
// complete group is absorbed at once, as in the one-shot function.
INL void LoadState(V r[16], const pikaseko256_ctx* ctx) {
  for (int i = 0; i < 16; i++) r[i] = _mm512_loadu_si512((const void*)(ctx->r + 8 * i));
}
INL void StoreState(pikaseko256_ctx* ctx, const V r[16]) {
  for (int i = 0; i < 16; i++) _mm512_storeu_si512((void*)(ctx->r + 8 * i), r[i]);
}

void pikaseko256_init_avx512(pikaseko256_ctx* ctx, const uint64_t key[8]) {
  V r[16];
  Init(r, key);
  StoreState(ctx, r);
  ctx->len = 0; ctx->t = 0; ctx->nbuf = 0;
}

void pikaseko256_update_avx512(pikaseko256_ctx* ctx, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  ctx->len += len;
  if (len < 256 - (size_t)ctx->nbuf) {
    if (len) memcpy(ctx->buf + ctx->nbuf, p, len);
    ctx->nbuf += (unsigned)len;
    return;
  }
  Consts cs; LoadConsts(&cs);
  V r[16];
  LoadState(r, ctx);
  unsigned t = ctx->t;
  if (ctx->nbuf) {
    const size_t take = 256 - ctx->nbuf;
    memcpy(ctx->buf + ctx->nbuf, p, take);
    t = AbsorbGroups(r, t, ctx->buf, 1, &cs);
    p += take; len -= take; ctx->nbuf = 0;
  }
  t = AbsorbGroups(r, t, p, len / 256, &cs);
  p += len & ~(size_t)255;
  ctx->nbuf = (unsigned)(len % 256);
  if (ctx->nbuf) memcpy(ctx->buf, p, ctx->nbuf);
  ctx->t = t;
  StoreState(ctx, r);
}

void pikaseko256_final_avx512(const pikaseko256_ctx* ctx, uint64_t out[4]) {
  Consts cs; LoadConsts(&cs);
  V r[16];
  LoadState(r, ctx);
  Finish(r, ctx->t, ctx->buf, ctx->nbuf, ctx->len, &cs, out);
}

void pikaseko256_unkeyed_avx512(const void* data, size_t len, uint64_t out[4]) {
  pikaseko256_avx512(data, len, NULL, out);
}

uint64_t pikaseko64_avx512(const void* data, size_t len, const uint64_t key[8]) {
  uint64_t out[4];
  pikaseko256_avx512(data, len, key, out);
  return out[0];
}

void pikaseko256_steps_avx512(uint64_t state[128], unsigned t0, unsigned nsteps) {
  Consts cs; LoadConsts(&cs);
  V r[16];
  for (int i = 0; i < 16; i++) r[i] = _mm512_loadu_si512((const void*)(state + 8 * i));
  for (unsigned e = 0; e < nsteps; e++) StepAny(r, t0 + e, &cs);
  for (int i = 0; i < 16; i++) _mm512_storeu_si512((void*)(state + 8 * i), r[i]);
}
