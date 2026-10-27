// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Tests of PikasekoHash-256 version 1. Usage: test [path of test/test_vectors.txt]
//
// For every implementation the build has and the CPU supports (portable, AVX-512, AVX2,
// AES-NI, ARMv8): the test vectors, every length 0..2048 at three alignments against the
// portable code (keyed and unkeyed, both digest widths), streaming against one-shot, the
// step function against the portable one, and messages next to inaccessible pages. Then the
// public functions: domain separation (unkeyed, empty key, zero key, long keys, the two
// widths), streaming, and the precomputed unkeyed states. When the environment variable
// PIKASEKO256_FORCE_PORTABLE=1 is set, only the portable implementation runs.
#if defined(__unix__) || defined(__APPLE__)
#define _DEFAULT_SOURCE 1  // MAP_ANONYMOUS
#include <sys/mman.h>
#include <unistd.h>
#define HAVE_MMAP 1
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"
#include "pikaseko256_consts.h"
#include "pikaseko256_impl.h"
#include "pikaseko256_step0.h"

typedef void HashFn(const void*, size_t, const uint64_t*, uint64_t, uint64_t*);
typedef void InitFn(pikaseko256_ctx*, const uint64_t*, uint64_t);
typedef void UpdateFn(pikaseko256_ctx*, const void*, size_t);
typedef void FinalFn(const pikaseko256_ctx*, uint64_t*);
typedef void StepsFn(uint64_t*, unsigned, unsigned);
typedef struct { const char* name; int level; HashFn* hash; InitFn* init; UpdateFn* update; FinalFn* final; StepsFn* steps; } Impl;
#define IMPL(name, level, sfx) {name, level, pikaseko_hash_##sfx, pikaseko_init_##sfx, pikaseko_update_##sfx, pikaseko_final_##sfx, pikaseko_steps_##sfx}
static const Impl kImpls[] = {
    IMPL("portable", 0, portable),
#if defined(PIKASEKO256_HAVE_X86)
    IMPL("AVX-512", 3, avx512), IMPL("AVX2", 2, avx2), IMPL("AES-NI", 1, aesni),
#elif defined(PIKASEKO256_HAVE_NEON)
    IMPL("ARMv8 AES", 1, neon),
#endif
};

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t Next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static int fails = 0;
static void Check(int ok, const char* what) {
  printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
  fails += !ok;
}

static void ToBytes(const uint64_t w[4], uint8_t* d, int n) {
  for (int i = 0; i < n; i++) d[i] = (uint8_t)(w[i / 8] >> (8 * (i % 8)));
}

// Hashes with one implementation as the public functions do: key bytes (NULL: unkeyed),
// digest width 32 or 8.
static void HashBytes(const Impl* im, const void* data, size_t len, const uint8_t* key, size_t key_len, int width,
                      uint8_t* digest) {
  uint64_t K[8], w[4];
  if (key) {
    const uint64_t dom = pikaseko_key_setup(key, key_len, (unsigned)width, K);
    im->hash(data, len, K, dom, w);
  } else {
    im->hash(data, len, NULL, width == 32 ? PIKASEKO_DOMAIN_UNKEYED256 : PIKASEKO_DOMAIN_UNKEYED64, w);
  }
  ToBytes(w, digest, width);
}

static int Unhex(const char* s, uint8_t* d, int n) {
  for (int i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
    d[i] = (uint8_t)v;
  }
  return 1;
}

// The test vectors file, for one implementation. Returns the number of lines checked, or -1.
static long Vectors(const Impl* im, const char* path, int checksum) {
  FILE* f = fopen(path, "r");
  if (!f) return -1;
  static uint8_t buf[4200], key[100];
  for (int i = 0; i < 4200; i++) buf[i] = (uint8_t)i;
  for (int i = 0; i < 100; i++) key[i] = (uint8_t)i;
  char line[512];
  long n = 0, bad = 0;
  while (fgets(line, sizeof line, f)) {
    size_t len = 0, klen = 0;
    char hex[80];
    uint8_t want[32], got[32];
    int width = 0, keyed = 0;
    if (sscanf(line, "unkeyed256 len %zu %79s", &len, hex) == 2) width = 32;
    else if (sscanf(line, "unkeyed64 len %zu %79s", &len, hex) == 2) width = 8;
    else if (sscanf(line, "keyed256 klen %zu len %zu %79s", &klen, &len, hex) == 3) width = 32, keyed = 1;
    else if (sscanf(line, "keyed64 klen %zu len %zu %79s", &klen, &len, hex) == 3) width = 8, keyed = 1;
    else if (checksum && !strncmp(line, "unkeyed256, all lengths 0..2048 folded:", 39)) {
      uint64_t acc = 0;
      for (size_t m = 0; m <= 2048; m++) {
        uint64_t w[4];
        im->hash(buf, m, NULL, PIKASEKO_DOMAIN_UNKEYED256, w);
        for (int i = 0; i < 4; i++) acc = acc * 0x9E3779B97F4A7C15ull ^ w[i];
      }
      n++;
      bad += acc != strtoull(line + 40, NULL, 16);
      continue;
    } else {
      continue;
    }
    if (!Unhex(hex, want, width)) { bad++; continue; }
    HashBytes(im, buf, len, keyed ? key : NULL, klen, width, got);
    n++;
    if (memcmp(want, got, (size_t)width)) {
      if (bad < 3) printf("  %s: mismatch on: %s", im->name, line);
      bad++;
    }
  }
  fclose(f);
  return bad ? -1 : n;
}

// One implementation against portable: every length 0..2048 (or a sample), three alignments,
// unkeyed 256 and 64, and a random key. Returns mismatches.
static long VsPortable(const Impl* im, const uint8_t* buf, int sample) {
  long bad = 0;
  uint64_t key[8];
  for (int i = 0; i < 8; i++) key[i] = Next();
  const uint64_t keyed_dom = PIKASEKO_DOMAIN(1, 0, 64, 32);
  for (size_t len = 0; len <= 2048; len += sample && len >= 140 ? 61 : 1)
    for (int al = 0; al < 3; al++) {
      const uint8_t* d = buf + (al == 0 ? 0 : al == 1 ? 1 : 37);
      for (int mode = 0; mode < 3; mode++) {
        const uint64_t* k = mode == 2 ? key : NULL;
        const uint64_t dom = mode == 0 ? PIKASEKO_DOMAIN_UNKEYED256 : mode == 1 ? PIKASEKO_DOMAIN_UNKEYED64 : keyed_dom;
        uint64_t a[4], p[4];
        im->hash(d, len, k, dom, a);
        pikaseko_hash_portable(d, len, k, dom, p);
        if (memcmp(a, p, 32)) { if (bad < 3) printf("  %s != portable: len %zu align %d mode %d\n", im->name, len, al, mode); bad++; }
      }
    }
  return bad;
}

// Streaming against one-shot for one implementation: the whole message in one update,
// random pieces with a digest of every prefix taken on the way (which must not disturb the
// context), and single bytes. Returns mismatches.
static long Streaming(const Impl* im, const uint8_t* buf, size_t maxlen, size_t lstep) {
  long bad = 0;
  uint64_t key[8];
  for (int i = 0; i < 8; i++) key[i] = Next();
  for (size_t len = 0; len <= maxlen; len += len < 70 ? 1 : lstep)
    for (int keyed = 0; keyed < 2; keyed++) {
      const uint64_t* kp = keyed ? key : NULL;
      const uint64_t dom = keyed ? PIKASEKO_DOMAIN(1, 0, 64, 32) : PIKASEKO_DOMAIN_UNKEYED256;
      uint64_t want[4], got[4], pre[4], part[4];
      im->hash(buf, len, kp, dom, want);
      pikaseko256_ctx ctx;
      im->init(&ctx, kp, dom); im->update(&ctx, buf, len); im->final(&ctx, got);
      bad += memcmp(want, got, 32) != 0;
      im->init(&ctx, kp, dom);
      for (size_t pos = 0; pos < len;) {
        size_t piece = (size_t)(Next() % 300);
        if (piece > len - pos) piece = len - pos;
        im->update(&ctx, buf + pos, piece);
        pos += piece;
        if (Next() % 4 == 0) {
          im->final(&ctx, part); im->hash(buf, pos, kp, dom, pre);
          bad += memcmp(part, pre, 32) != 0;
        }
      }
      im->final(&ctx, got);
      bad += memcmp(want, got, 32) != 0;
      if (len <= 600 && len % 3 == 0) {
        im->init(&ctx, kp, dom);
        for (size_t i = 0; i < len; i++) im->update(&ctx, buf + i, 1);
        im->final(&ctx, got);
        bad += memcmp(want, got, 32) != 0;
      }
    }
  return bad;
}

static int StepsVsPortable(const Impl* im) {
  int bad = 0;
  for (int trial = 0; trial < 64; trial++) {
    uint64_t s1[128], s2[128];
    for (int i = 0; i < 128; i++) s1[i] = s2[i] = Next();
    im->steps(s1, (unsigned)trial % 11, 1 + trial % 6);
    pikaseko_steps_portable(s2, (unsigned)trial % 11, 1 + trial % 6);
    bad += memcmp(s1, s2, sizeof s1) != 0;
  }
  return bad;
}

#ifdef HAVE_MMAP
// Messages that end at the last byte before an inaccessible page, and messages that start
// at the first byte after one, one-shot and streaming. Returns mismatches, or -1 if skipped.
static long GuardPages(const Impl* im) {
  const size_t page = (size_t)sysconf(_SC_PAGESIZE), span = (2048 / page + 1) * page;
  uint8_t* base = (uint8_t*)mmap(NULL, span + 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED) return -1;
  uint8_t* body = base + page;
  if (mprotect(base, page, PROT_NONE) || mprotect(body + span, page, PROT_NONE)) {
    munmap(base, span + 2 * page);
    return -1;
  }
  for (size_t i = 0; i < span; i++) body[i] = (uint8_t)Next();
  long bad = 0;
  for (size_t len = 0; len <= 2048; len += len < 300 ? 1 : 13) {
    const uint8_t* ends = body + span - len;
    uint64_t want[4], got[4];
    pikaseko_hash_portable(ends, len, NULL, PIKASEKO_DOMAIN_UNKEYED256, want);
    im->hash(ends, len, NULL, PIKASEKO_DOMAIN_UNKEYED256, got); bad += memcmp(got, want, 32) != 0;
    pikaseko256_ctx ctx;
    im->init(&ctx, NULL, PIKASEKO_DOMAIN_UNKEYED256);
    im->update(&ctx, ends, len / 3); im->update(&ctx, ends + len / 3, len - len / 3);
    im->final(&ctx, got); bad += memcmp(got, want, 32) != 0;
    pikaseko_hash_portable(body, len, NULL, PIKASEKO_DOMAIN_UNKEYED256, want);
    im->hash(body, len, NULL, PIKASEKO_DOMAIN_UNKEYED256, got); bad += memcmp(got, want, 32) != 0;
  }
  munmap(base, span + 2 * page);
  return bad;
}
#endif

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "test/test_vectors.txt";
  static uint8_t buf[4200 + 64];
  for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)Next();
  const int level = pikaseko256_cpu_level();
  printf("implementation in use: %s\n", pikaseko256_implementation());
  char what[128];

  for (size_t m = 0; m < sizeof kImpls / sizeof kImpls[0]; m++) {
    const Impl* im = &kImpls[m];
    if (im->level > level) { printf("%s: skipped (not supported by this CPU, or not selected)\n", im->name); continue; }
    const int portable = m == 0;
    const long nv = Vectors(im, path, 1);
    snprintf(what, sizeof what, "%s: test vectors (%ld lines)", im->name, nv);
    Check(nv > 0, what);
    if (!portable) {
      snprintf(what, sizeof what, "%s: against portable, lengths 0..2048", im->name);
      Check(VsPortable(im, buf, 0) == 0, what);
    }
    snprintf(what, sizeof what, "%s: streaming against one-shot", im->name);
    Check(Streaming(im, buf, portable ? 700 : 2100, portable ? 97 : 7) == 0, what);
    snprintf(what, sizeof what, "%s: step function against portable", im->name);
    Check(StepsVsPortable(im) == 0, what);
#ifdef HAVE_MMAP
    const long g = GuardPages(im);
    snprintf(what, sizeof what, "%s: messages next to guard pages%s", im->name, g < 0 ? " (skipped)" : "");
    Check(g <= 0, what);
#endif
  }
  if (level == 0) {  // no SIMD implementation ran: at least the portable code at other alignments
    long bad = 0;
    for (size_t len = 0; len <= 1100; len += len < 140 ? 1 : 61) {
      uint8_t d0[32], d1[32];
      pikaseko256_unkeyed(buf, len, d0);
      memcpy(buf + 2101, buf, len);
      pikaseko256_unkeyed(buf + 2101, len, d1);
      bad += memcmp(d0, d1, 32) != 0;
    }
    Check(bad == 0, "portable: alignment independence");
  }

  // Implementations can continue each other's streaming context.
  if (sizeof kImpls / sizeof kImpls[0] > 1 && kImpls[1].level <= level) {
    uint64_t want[4], got[4], key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint64_t dom = PIKASEKO_DOMAIN(1, 0, 64, 32);
    const Impl* a = &kImpls[1];
    a->hash(buf, 3000, key, dom, want);
    pikaseko256_ctx ctx;
    a->init(&ctx, key, dom);
    a->update(&ctx, buf, 777);
    pikaseko_update_portable(&ctx, buf + 777, 1000);
    a->update(&ctx, buf + 1777, 1223);
    pikaseko_final_portable(&ctx, got);
    Check(!memcmp(want, got, 32), "streaming context shared by the implementations");
  }

  // Domain separation (SPEC.md, section 4).
  {
    uint8_t u[32], k0[32], kz[32], kz32[32], klong[32], kdig[32], d64[8];
    const uint8_t zeros[64] = {0};
    uint8_t longkey[100];
    for (int i = 0; i < 100; i++) longkey[i] = (uint8_t)i;
    pikaseko256_unkeyed(buf, 100, u);
    pikaseko256_keyed(buf, 100, NULL, 0, k0);
    pikaseko256_keyed(buf, 100, zeros, 64, kz);
    pikaseko256_keyed(buf, 100, zeros, 32, kz32);
    pikaseko256_keyed(buf, 100, longkey, 100, klong);
    uint8_t kh[32];
    pikaseko256_unkeyed(longkey, 100, kh);
    pikaseko256_keyed(buf, 100, kh, 32, kdig);  // the hashed long key, given as a 32-byte key
    const uint64_t h64 = pikaseko64_unkeyed(buf, 100);
    for (int i = 0; i < 8; i++) d64[i] = (uint8_t)(h64 >> (8 * i));
    Check(memcmp(u, k0, 32) && memcmp(u, kz, 32) && memcmp(k0, kz, 32) && memcmp(kz, kz32, 32),
          "unkeyed, empty key, 32 and 64 zero bytes all differ");
    Check(memcmp(klong, kdig, 32) != 0, "a long key differs from its digest used as the key");
    Check(memcmp(u, d64, 8) != 0 && pikaseko64_keyed(buf, 100, NULL, 0) != h64, "64-bit hash is not a part of the 256-bit one");
  }

  // The public streaming functions equal the one-shot ones.
  {
    const uint8_t key[20] = {9, 8, 7};
    int ok = 1;
    for (size_t len = 0; len <= 1300; len += 37) {
      uint8_t a[32], b[32];
      pikaseko256_ctx ctx;
      pikaseko256_unkeyed(buf, len, a);
      pikaseko256_init_unkeyed(&ctx); pikaseko256_update(&ctx, buf, len / 2); pikaseko256_update(&ctx, buf + len / 2, len - len / 2);
      pikaseko256_final(&ctx, b); ok &= !memcmp(a, b, 32);
      pikaseko256_keyed(buf, len, key, sizeof key, a);
      pikaseko256_init_keyed(&ctx, key, sizeof key); pikaseko256_update(&ctx, buf, len); pikaseko256_final(&ctx, b);
      ok &= !memcmp(a, b, 32);
      pikaseko64_init_unkeyed(&ctx); pikaseko256_update(&ctx, buf, len); ok &= pikaseko64_final(&ctx) == pikaseko64_unkeyed(buf, len);
      pikaseko64_init_keyed(&ctx, key, sizeof key); pikaseko256_update(&ctx, buf, len);
      ok &= pikaseko64_final(&ctx) == pikaseko64_keyed(buf, len, key, sizeof key);
    }
    Check(ok, "public streaming functions equal the one-shot ones");
  }

  // The precomputed unkeyed states after step 0 are the unkeyed IVs after one step.
  {
    int ok = 1;
    for (int w = 0; w < 2; w++) {
      uint64_t st[128];
      for (int i = 0; i < 16; i++)
        for (int l = 0; l < 8; l++)
          st[8 * i + l] = (uint64_t)(uint32_t)(PIKASEKO_PHI + (uint32_t)i * PIKASEKO_PI) * (uint32_t)(PIKASEKO_PHI + (uint32_t)l * PIKASEKO_E);
      st[16] ^= w ? PIKASEKO_DOMAIN_UNKEYED64 : PIKASEKO_DOMAIN_UNKEYED256;
      pikaseko_steps_portable(st, 0, 1);
      ok &= !memcmp(st, kPikasekoUnkeyedStep0[w], sizeof st);
    }
    Check(ok, "precomputed unkeyed states after step 0");
  }

  // Portable steps: n steps equal a steps then n - a steps, and zero steps change nothing.
  {
    int bad = 0;
    for (int trial = 0; trial < 16; trial++) {
      uint64_t s1[128], s2[128], s0[128];
      for (int i = 0; i < 128; i++) s0[i] = s1[i] = s2[i] = Next();
      const unsigned t0 = (unsigned)trial % 11, n = 2 + (unsigned)trial % 5, a = 1 + (unsigned)trial % (n - 1);
      pikaseko_steps_portable(s1, t0, n);
      pikaseko_steps_portable(s2, t0, a);
      pikaseko_steps_portable(s2, t0 + a, n - a);
      bad += memcmp(s1, s2, sizeof s1) != 0 || !memcmp(s1, s0, sizeof s1);
      pikaseko_steps_portable(s2, t0, 0);
      bad += memcmp(s1, s2, sizeof s1) != 0;
    }
    Check(bad == 0, "portable step function consistency");
  }

  printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
  return fails != 0;
}
