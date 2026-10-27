// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Tests: test vectors (an embedded copy of part of test/test_vectors.txt) on both
// implementations, the AVX-512 code against the portable code for every length 0..2048 at
// three alignments and three keys, streaming against one-shot, the step function, and
// NULL key = zero key.
//
// The AVX-512 tests run only when the CPU and OS support AVX512F, AVX512BW and VAES (and
// the library was not built with PIKASEKO256_NO_AVX512); otherwise, or when the
// environment variable PIKASEKO256_FORCE_PORTABLE=1 is set, only the portable tests run:
// the vectors, the checksum, a reduced alignment and key sweep, and step consistency.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"
#include "pikaseko256_impl.h"

#ifndef PIKASEKO256_NO_AVX512
static int ForcePortable(void) {
  const char* e = getenv("PIKASEKO256_FORCE_PORTABLE");
  return e && *e && !(e[0] == '0' && e[1] == 0);
}
#endif

typedef struct { int keyed; size_t len; uint64_t h[4]; } Vector;
static const Vector kVectors[] = {
  {0, 0, {0x4ce9ce973810692dull, 0x933bdf92669b1d6eull, 0x277e33f96dacc156ull, 0x9b8ea902b68c1524ull}},
  {1, 0, {0xb51bcf8389ec302dull, 0xf308920a30644943ull, 0x8bc96cf0c183894bull, 0xb8b03fa938843fa0ull}},
  {0, 1, {0xe7c9b70e06ab6227ull, 0x7f12a8928cc8e7f0ull, 0xa9c7f2d3f32cc7baull, 0x17f4a037b67cd148ull}},
  {1, 1, {0x9207d99b7e4cc590ull, 0x7e25960605692074ull, 0xf9f411794eeb900dull, 0xeb9fe6abe5f7ac71ull}},
  {0, 55, {0x9e81b0fae8ed6dc0ull, 0x03dd7d2aeae33099ull, 0xe230a765098761a7ull, 0x51a2ca0db29f1ba0ull}},
  {1, 55, {0x8f50d80647a4b127ull, 0xcfde76a52061df93ull, 0x234c5c9f9f603c67ull, 0xb389aa7cfbc7d26full}},
  {0, 56, {0xe14d664c8d8d2211ull, 0x2984b77b33f8b7e3ull, 0x523c99d419775ad5ull, 0xa89c748918c88069ull}},
  {1, 56, {0x5404f02ed70670fcull, 0x8f0f62cceaf42e13ull, 0x1ca41f0cc11a41caull, 0xc3d485b4d1d46e02ull}},
  {0, 63, {0x06d79fdb8d5efa37ull, 0xa7f598bab89a74efull, 0x15ce0cce0b3bbdd2ull, 0xdbbf7da4a0b10076ull}},
  {1, 63, {0x6231e67375879c93ull, 0xf2275701abf944e6ull, 0x401835715f46aeb9ull, 0x2050ac031f97bba5ull}},
  {0, 64, {0x4e6e370a080d35d6ull, 0x80be6c20bbc346cfull, 0x276bf04fe3acc9c1ull, 0x4eccdedbbddd4e4cull}},
  {1, 64, {0x4f1c2beb2f35bd19ull, 0x78c22dc619d5d41aull, 0x800349c7c7546c05ull, 0x520b9835c6ed42a5ull}},
  {0, 255, {0x6d4fcbaded549b5dull, 0xba25e1471008db1bull, 0x65dc0c8f3718eabeull, 0x0ad5540d6838b4d4ull}},
  {1, 255, {0x9b85fbe15c55c8c7ull, 0xfe8888a68d9eb5b9ull, 0xef0b445d6e574df3ull, 0x69655489450e67d8ull}},
  {0, 256, {0x6e17dc2b63550adeull, 0x608f47cbab1a18a4ull, 0xdaf2b7fe20d80741ull, 0x1b0ad2ad8ac203d7ull}},
  {1, 256, {0xc87b98a350a7b7e7ull, 0x31ef7be86646613dull, 0x54735ec7b52d565eull, 0x9335a58d3496964aull}},
  {0, 1023, {0xd282ad5fc0f5067bull, 0x21bccdaf72d557baull, 0x883b0dcae51c823aull, 0x11e98476f49113c7ull}},
  {1, 1023, {0x4e2c917a15cb83e1ull, 0x8f89d5d8e943f863ull, 0x83f66167deecc649ull, 0x9cdc21512f160c37ull}},
  {0, 1024, {0x120281c2a84d8704ull, 0x226e80627a35a804ull, 0x1477e51cc28c5284ull, 0x9bc97fa1c3362055ull}},
  {1, 1024, {0xb4a517bad3f4c3e4ull, 0x63c84f11205359fdull, 0x45ca71df66d9d261ull, 0xa3d45a979991d530ull}},
  {0, 1025, {0x5bd4fa613c59e496ull, 0x38ed1566d6c669b6ull, 0xfba24824de3a99e6ull, 0x72d9b0a8e7d87443ull}},
  {1, 1025, {0xcc0de3a5279d242eull, 0xf09615961c98f404ull, 0x14bc3851b65e1f4cull, 0x30c282135586f4e0ull}},
  {0, 4096, {0x8476133fc8c17e89ull, 0x42914c496f9d0b7aull, 0xa7de9a62d774b445ull, 0x1da09180a041e1d0ull}},
  {1, 4096, {0x51f0a1157fcc596eull, 0x918ed449a21e8b43ull, 0xa1d5cb21ce984780ull, 0xd68df9766491a158ull}}
};
static const uint64_t kFoldAll = 0xc0fb62d98e9f3498ull;  // unkeyed, lengths 0..2048, folded as in vectors.c

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t Next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

typedef void HashFn(const void*, size_t, const uint64_t*, uint64_t*);
typedef void UnkeyedFn(const void*, size_t, uint64_t*);

typedef void StepsFn(uint64_t*, unsigned, unsigned);

#ifndef PIKASEKO256_NO_AVX512
// An implementation against portable: random data, three keys, three alignments, every
// length 0..2048. Returns mismatches.
static long ImplVsPortable(const uint8_t* buf, const uint64_t* key18, HashFn* hash, const char* name) {
  long n = 0, bad = 0;
  for (int kc = 0; kc < 3; kc++) {
    uint64_t key[8];
    for (int i = 0; i < 8; i++) key[i] = Next();
    const uint64_t* kp = kc == 0 ? NULL : kc == 1 ? key18 : key;
    for (size_t len = 0; len <= 2048; len++)
      for (int al = 0; al < 3; al++) {
        const uint8_t* d = buf + (al == 0 ? 0 : al == 1 ? 1 : 37);
        uint64_t a[4], p[4];
        hash(d, len, kp, a);
        pikaseko256_portable(d, len, kp, p);
        n++;
        if (memcmp(a, p, 32)) { if (bad < 5) printf("FAIL %s != portable: len %zu key %d align %d\n", name, len, kc, al); bad++; }
      }
  }
  printf("%s vs portable: %ld inputs, %ld mismatches\n", name, n, bad);
  return bad;
}

// Step function: an implementation against portable.
static int StepsVsPortable(StepsFn* steps, const char* name) {
  int sb = 0;
  for (int trial = 0; trial < 64; trial++) {
    uint64_t s1[128], s2[128];
    for (int i = 0; i < 128; i++) s1[i] = s2[i] = Next();
    steps(s1, (unsigned)trial % 11, 1 + trial % 6);
    pikaseko256_steps_portable(s2, (unsigned)trial % 11, 1 + trial % 6);
    sb += memcmp(s1, s2, sizeof s1) != 0;
  }
  printf("step function %s vs portable: %s\n", name, sb ? "FAIL" : "ok");
  return sb != 0;
}
#endif

// Portable only (reduced, the portable code is slow): the same input at other alignments
// gives the same digest, and pikaseko64_portable is lane 0. Returns mismatches.
static long PortableSweep(uint8_t* buf, const uint64_t* key18) {
  long n = 0, bad = 0;
  uint64_t key[8];
  for (int i = 0; i < 8; i++) key[i] = Next();
  for (int kc = 0; kc < 3; kc++) {
    const uint64_t* kp = kc == 0 ? NULL : kc == 1 ? key18 : key;
    for (size_t len = 0; len <= 1100; len += len < 140 ? 1 : len < 1000 ? 61 : 23) {
      uint64_t p0[4];
      pikaseko256_portable(buf, len, kp, p0);
      for (int al = 1; al < 3; al++) {
        uint64_t p[4];
        uint8_t* d = buf + 2048 + (al == 1 ? 1 : 37);
        memcpy(d, buf, len);
        pikaseko256_portable(d, len, kp, p);
        n++;
        if (memcmp(p0, p, 32)) { if (bad < 5) printf("FAIL portable alignment: len %zu key %d align %d\n", len, kc, al); bad++; }
      }
      if (len % 8 == 0 && pikaseko64_portable(buf, len, kp) != p0[0]) bad++;
    }
  }
  printf("portable alignment/key sweep: %ld inputs, %ld mismatches\n", n, bad);
  return bad;
}

// Portable steps: n steps equal a steps then n - a steps, and zero steps change nothing.
static int StepsPortableConsistency(void) {
  int sb = 0;
  for (int trial = 0; trial < 16; trial++) {
    uint64_t s1[128], s2[128], s0[128];
    for (int i = 0; i < 128; i++) s0[i] = s1[i] = s2[i] = Next();
    const unsigned t0 = (unsigned)trial % 11, n = 2 + (unsigned)trial % 5, a = 1 + (unsigned)trial % (n - 1);
    pikaseko256_steps_portable(s1, t0, n);
    pikaseko256_steps_portable(s2, t0, a);
    pikaseko256_steps_portable(s2, t0 + a, n - a);
    sb += memcmp(s1, s2, sizeof s1) != 0 || !memcmp(s1, s0, sizeof s1);
    pikaseko256_steps_portable(s2, t0, 0);
    sb += memcmp(s1, s2, sizeof s1) != 0;
  }
  printf("step function portable consistency: %s\n", sb ? "FAIL" : "ok");
  return sb != 0;
}

typedef void InitFn(pikaseko256_ctx*, const uint64_t*);
typedef void UpdateFn(pikaseko256_ctx*, const void*, size_t);
typedef void FinalFn(const pikaseko256_ctx*, uint64_t*);

// Streaming against one-shot: lengths 0..maxlen (step lstep), unkeyed and keyed, the whole
// message in one update, random pieces (with a digest of every prefix taken on the way,
// which must not disturb the context), and single bytes. Returns mismatches.
static long Streaming(const uint8_t* buf, const uint64_t* key18, HashFn* hash, InitFn* init, UpdateFn* update,
                      FinalFn* final, size_t maxlen, size_t lstep, const char* name) {
  long n = 0, bad = 0;
  for (size_t len = 0; len <= maxlen; len += len < 70 ? 1 : lstep)
    for (int kc = 0; kc < 2; kc++) {
      const uint64_t* kp = kc ? key18 : NULL;
      uint64_t want[4], got[4], pre[4], part[4];
      hash(buf, len, kp, want);
      pikaseko256_ctx ctx;
      init(&ctx, kp); update(&ctx, buf, len); final(&ctx, got);
      n++; bad += memcmp(want, got, 32) != 0;
      init(&ctx, kp);
      for (size_t pos = 0; pos < len;) {
        size_t piece = (size_t)(Next() % 300);
        if (piece > len - pos) piece = len - pos;
        update(&ctx, buf + pos, piece);
        pos += piece;
        if (Next() % 4 == 0) {
          final(&ctx, part); hash(buf, pos, kp, pre);
          n++; bad += memcmp(part, pre, 32) != 0;
        }
      }
      final(&ctx, got);
      n++; bad += memcmp(want, got, 32) != 0;
      if (len <= 600 && len % 3 == 0) {
        init(&ctx, kp);
        for (size_t i = 0; i < len; i++) update(&ctx, buf + i, 1);
        final(&ctx, got);
        n++; bad += memcmp(want, got, 32) != 0;
      }
    }
  printf("streaming (%s): %ld digests, %ld mismatches\n", name, n, bad);
  return bad;
}

int main(void) {
  static uint8_t buf[4200 + 64], buf0[4200];
  int fails = 0;
  const int avx = pikaseko256_uses_avx512();
#ifdef PIKASEKO256_NO_AVX512
  const char* why = "portable-only build";
#else
  const char* why = ForcePortable() ? "PIKASEKO256_FORCE_PORTABLE is set" : "CPU or OS lacks AVX512F/AVX512BW/VAES";
#endif
  if (!avx) printf("AVX-512 tests skipped (%s); running the portable tests only\n", why);
  HashFn* hash = pikaseko256_portable;
  UnkeyedFn* unkeyed = pikaseko256_unkeyed_portable;
#ifndef PIKASEKO256_NO_AVX512
  if (avx) { hash = pikaseko256_avx512; unkeyed = pikaseko256_unkeyed_avx512; }
#endif
  for (int i = 0; i < 4200; i++) buf[i] = buf0[i] = (uint8_t)i;
  const uint64_t key18[8] = {1, 2, 3, 4, 5, 6, 7, 8}, zero[8] = {0};
  for (size_t i = 0; i < sizeof kVectors / sizeof kVectors[0]; i++) {
    const Vector* v = &kVectors[i];
    uint64_t a[4], p[4];
    if (avx) {
      hash(buf, v->len, v->keyed ? key18 : NULL, a);
      if (memcmp(a, v->h, 32)) { printf("FAIL vector %s len %zu (AVX-512)\n", v->keyed ? "keyed" : "unkeyed", v->len); fails++; }
    }
    pikaseko256_portable(buf, v->len, v->keyed ? key18 : NULL, p);
    if (memcmp(p, v->h, 32)) { printf("FAIL vector %s len %zu (portable)\n", v->keyed ? "keyed" : "unkeyed", v->len); fails++; }
  }
  uint64_t acc = 0, o[4];
  for (size_t len = 0; len <= 2048; len++) {
    unkeyed(buf, len, o);
    for (int i = 0; i < 4; i++) acc = acc * 0x9E3779B97F4A7C15ull ^ o[i];
  }
  if (acc != kFoldAll) { printf("FAIL checksum over lengths 0..2048 (%s)\n", avx ? "AVX-512" : "portable"); fails++; }
  printf("test vectors (%s): %s\n", avx ? "AVX-512 and portable" : "portable", fails ? "FAIL" : "ok");

  for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)Next();
#ifndef PIKASEKO256_NO_AVX512
  if (avx) {
    fails += ImplVsPortable(buf, key18, pikaseko256_avx512, "AVX-512") != 0;
    int ok = 1;
    for (size_t len = 0; len <= 300; len += 7) ok &= pikaseko64_avx512(buf, len, key18) == pikaseko64_portable(buf, len, key18);
    if (!ok) { printf("FAIL pikaseko64_avx512\n"); fails++; }
  }
#endif
  if (!avx) fails += PortableSweep(buf, key18) != 0;

  // NULL key equals the zero key; unkeyed equals both.
  {
    uint64_t a[4], b[4], c[4];
    hash(buf, 100, NULL, a); hash(buf, 100, zero, b); unkeyed(buf, 100, c);
    int ok = !memcmp(a, b, 32) && !memcmp(a, c, 32);
    if (avx) {
      pikaseko256_portable(buf, 100, NULL, a); pikaseko256_portable(buf, 100, zero, b); pikaseko256_unkeyed_portable(buf, 100, c);
      ok &= !memcmp(a, b, 32) && !memcmp(a, c, 32);
    }
    printf("NULL key = zero key = unkeyed: %s\n", ok ? "ok" : "FAIL");
    fails += !ok;
  }

  fails += Streaming(buf, key18, pikaseko256_portable, pikaseko256_init_portable, pikaseko256_update_portable,
                     pikaseko256_final_portable, avx ? 1100 : 600, avx ? 31 : 97, "portable") != 0;
#ifndef PIKASEKO256_NO_AVX512
  if (avx) {
    fails += Streaming(buf, key18, pikaseko256_avx512, pikaseko256_init_avx512, pikaseko256_update_avx512, pikaseko256_final_avx512, 4100, 3,
                       "AVX-512") != 0;
    // The context is the same for both implementations: start with one, continue with the other.
    uint64_t want[4], got[4];
    pikaseko256_avx512(buf, 3000, key18, want);
    pikaseko256_ctx ctx;
    pikaseko256_init_avx512(&ctx, key18);
    pikaseko256_update_avx512(&ctx, buf, 777);
    pikaseko256_update_portable(&ctx, buf + 777, 1000);
    pikaseko256_update_avx512(&ctx, buf + 1777, 1223);
    pikaseko256_final_portable(&ctx, got);
    const int ok = !memcmp(want, got, 32);
    printf("streaming, implementations mixed: %s\n", ok ? "ok" : "FAIL");
    fails += !ok;
  }
#endif
  // The dispatching functions equal the portable ones.
  {
    int ok = 1;
    for (size_t len = 0; len <= 600; len += 37) {
      uint64_t a[4], b[4], c[4], s[128], u[128];
      pikaseko256(buf, len, key18, a); pikaseko256_portable(buf, len, key18, b);
      ok &= !memcmp(a, b, 32) && pikaseko64(buf, len, NULL) == pikaseko64_portable(buf, len, NULL);
      pikaseko256_unkeyed(buf, len, a); pikaseko256_unkeyed_portable(buf, len, b);
      pikaseko256_ctx ctx;
      pikaseko256_init(&ctx, NULL); pikaseko256_update(&ctx, buf, len / 2); pikaseko256_update(&ctx, buf + len / 2, len - len / 2);
      pikaseko256_final(&ctx, c);
      ok &= !memcmp(a, b, 32) && !memcmp(a, c, 32);
      for (int i = 0; i < 128; i++) s[i] = u[i] = Next();
      pikaseko256_steps(s, (unsigned)len, 2); pikaseko256_steps_portable(u, (unsigned)len, 2);
      ok &= !memcmp(s, u, sizeof s);
    }
    printf("dispatching functions (%s): %s\n", pikaseko256_implementation(), ok ? "ok" : "FAIL");
    fails += !ok;
  }

  // Byte forms: the canonical digest of the empty message (SPEC.md, section 11) and a key.
  {
    static const uint8_t want[32] = {0x2d, 0x69, 0x10, 0x38, 0x97, 0xce, 0xe9, 0x4c, 0x6e, 0x1d, 0x9b, 0x66, 0x92, 0xdf, 0x3b, 0x93,
                                     0x56, 0xc1, 0xac, 0x6d, 0xf9, 0x33, 0x7e, 0x27, 0x24, 0x15, 0x8c, 0xb6, 0x02, 0xa9, 0x8e, 0x9b};
    uint8_t kb[64], d[32];
    uint64_t h[4], k[8];
    pikaseko256_unkeyed_portable("", 0, h);
    pikaseko256_digest_to_bytes(d, h);
    int ok = !memcmp(d, want, 32);
    for (int i = 0; i < 64; i++) kb[i] = (uint8_t)(i % 8 == 0 ? i / 8 + 1 : 0);  // the key 1..8
    pikaseko256_key_from_bytes(k, kb);
    ok &= !memcmp(k, key18, sizeof k);
    printf("byte forms of key and digest: %s\n", ok ? "ok" : "FAIL");
    fails += !ok;
  }

#ifndef PIKASEKO256_NO_AVX512
  if (avx) fails += StepsVsPortable(pikaseko256_steps_avx512, "AVX-512");
  // The AVX2 + VAES and AES-NI implementations, where the CPU has them.
  {
    const int level = pikaseko256_cpu_level();
    struct { int level; const char* name; HashFn* hash; InitFn* init; UpdateFn* update; FinalFn* final; StepsFn* steps; } impl[2] = {
      {2, "AVX2", pikaseko256_avx2, pikaseko256_init_avx2, pikaseko256_update_avx2, pikaseko256_final_avx2, pikaseko256_steps_avx2},
      {1, "AES-NI", pikaseko256_aesni, pikaseko256_init_aesni, pikaseko256_update_aesni, pikaseko256_final_aesni, pikaseko256_steps_aesni}};
    for (int m = 0; m < 2; m++) {
      if (level < impl[m].level) { printf("%s tests skipped (not supported or not selected)\n", impl[m].name); continue; }
      int vb = 0;
      for (size_t i = 0; i < sizeof kVectors / sizeof kVectors[0]; i++) {
        uint64_t a[4];
        impl[m].hash(buf0, kVectors[i].len, kVectors[i].keyed ? key18 : NULL, a);
        vb += memcmp(a, kVectors[i].h, 32) != 0;
      }
      printf("test vectors (%s): %s\n", impl[m].name, vb ? "FAIL" : "ok");
      fails += vb != 0;
      fails += ImplVsPortable(buf, key18, impl[m].hash, impl[m].name) != 0;
      fails += Streaming(buf, key18, impl[m].hash, impl[m].init, impl[m].update, impl[m].final, 2100, 7, impl[m].name) != 0;
      fails += StepsVsPortable(impl[m].steps, impl[m].name);
    }
  }
#endif
  fails += StepsPortableConsistency();
  if (!avx) printf("(AVX-512 tests were skipped)\n");
  printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
  return fails != 0;
}
