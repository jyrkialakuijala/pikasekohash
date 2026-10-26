// Tests: the built-in test vectors (test/test_vectors.txt) on both implementations, the
// AVX-512 code against the portable code over many lengths, alignments and keys, the step
// function, and NULL key = zero key.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pikaseko256.h"

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

int main(void) {
  static uint8_t buf[4200 + 64];
  int fails = 0;
  for (int i = 0; i < 4200; i++) buf[i] = (uint8_t)i;
  const uint64_t key18[8] = {1, 2, 3, 4, 5, 6, 7, 8}, zero[8] = {0};
  for (size_t i = 0; i < sizeof kVectors / sizeof kVectors[0]; i++) {
    const Vector* v = &kVectors[i];
    uint64_t a[4], p[4];
    pikaseko256(buf, v->len, v->keyed ? key18 : NULL, a);
    pikaseko256_portable(buf, v->len, v->keyed ? key18 : NULL, p);
    if (memcmp(a, v->h, 32)) { printf("FAIL vector %s len %zu (AVX-512)\n", v->keyed ? "keyed" : "unkeyed", v->len); fails++; }
    if (memcmp(p, v->h, 32)) { printf("FAIL vector %s len %zu (portable)\n", v->keyed ? "keyed" : "unkeyed", v->len); fails++; }
  }
  uint64_t acc = 0, o[4];
  for (size_t len = 0; len <= 2048; len++) {
    pikaseko256_unkeyed(buf, len, o);
    for (int i = 0; i < 4; i++) acc = acc * 0x9E3779B97F4A7C15ull ^ o[i];
  }
  if (acc != kFoldAll) { printf("FAIL checksum over lengths 0..2048\n"); fails++; }
  printf("test vectors: %s\n", fails ? "FAIL" : "ok");

  // AVX-512 against portable: random data, three keys, three alignments.
  for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)Next();
  long n = 0, bad = 0;
  for (int kc = 0; kc < 3; kc++) {
    uint64_t key[8];
    for (int i = 0; i < 8; i++) key[i] = Next();
    const uint64_t* kp = kc == 0 ? NULL : kc == 1 ? key18 : key;
    for (size_t len = 0; len <= 2048; len += len < 300 ? 1 : 7)
      for (int al = 0; al < 3; al++) {
        const uint8_t* d = buf + (al == 0 ? 0 : al == 1 ? 1 : 37);
        uint64_t a[4], p[4];
        pikaseko256(d, len, kp, a);
        pikaseko256_portable(d, len, kp, p);
        n++;
        if (memcmp(a, p, 32)) { if (bad < 5) printf("FAIL AVX-512 != portable: len %zu key %d align %d\n", len, kc, al); bad++; }
        if (pikaseko64(d, len, kp) != a[0] || pikaseko64_portable(d, len, kp) != p[0]) bad++;
      }
  }
  printf("AVX-512 vs portable: %ld inputs, %ld mismatches\n", n, bad);
  fails += bad != 0;

  // NULL key equals the zero key; unkeyed equals both.
  {
    uint64_t a[4], b[4], c[4];
    pikaseko256(buf, 100, NULL, a); pikaseko256(buf, 100, zero, b); pikaseko256_unkeyed(buf, 100, c);
    const int ok = !memcmp(a, b, 32) && !memcmp(a, c, 32);
    printf("NULL key = zero key = unkeyed: %s\n", ok ? "ok" : "FAIL");
    fails += !ok;
  }

  // Step function: AVX-512 against portable.
  {
    int sb = 0;
    for (int trial = 0; trial < 64; trial++) {
      uint64_t s1[128], s2[128];
      for (int i = 0; i < 128; i++) s1[i] = s2[i] = Next();
      pikaseko256_steps(s1, (unsigned)trial % 11, 1 + trial % 6);
      pikaseko256_steps_portable(s2, (unsigned)trial % 11, 1 + trial % 6);
      sb += memcmp(s1, s2, sizeof s1) != 0;
    }
    printf("step function AVX-512 vs portable: %s\n", sb ? "FAIL" : "ok");
    fails += sb != 0;
  }
  printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
  return fails != 0;
}
