// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: the dispatching entry points. On x86-64 they call the fastest
// implementation the CPU and OS support: AVX-512 + VAES, AVX2 + VAES, AES-NI, else the
// portable one. The check runs once and its result is cached.
#include <stdlib.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"
#include "pikaseko256_impl.h"

#if !defined(PIKASEKO256_NO_AVX512) && (defined(__x86_64__) || defined(_M_X64))
#define PIKASEKO_DISPATCH 1
#else
#define PIKASEKO_DISPATCH 0
#endif

#if PIKASEKO_DISPATCH
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static void Cpuid(unsigned leaf, unsigned sub, unsigned r[4]) {
  int x[4];
  __cpuidex(x, (int)leaf, (int)sub);
  for (int i = 0; i < 4; i++) r[i] = (unsigned)x[i];
}
static unsigned long long Xcr0(void) { return _xgetbv(0); }
#else
#include <cpuid.h>
static void Cpuid(unsigned leaf, unsigned sub, unsigned r[4]) { __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]); }
static unsigned long long Xcr0(void) {
  unsigned lo, hi;
  __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
  return ((unsigned long long)hi << 32) | lo;
}
#endif

// Implementation levels: 3 AVX-512 + VAES, 2 AVX2 + VAES, 1 AES-NI, 0 portable.
static int Detect(void) {
  const char* e = getenv("PIKASEKO256_FORCE_PORTABLE");
  if (e && *e && !(e[0] == '0' && e[1] == 0)) return 0;
  unsigned r[4];
  Cpuid(0, 0, r);
  const unsigned maxleaf = r[0];
  Cpuid(1, 0, r);
  const int aesni = (r[2] >> 25) & 1, osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
  if (!osxsave || !avx || maxleaf < 7) return aesni;
  const unsigned long long xcr0 = Xcr0();
  Cpuid(7, 0, r);
  const int avx2 = (r[1] >> 5) & 1, avx512f = (r[1] >> 16) & 1, avx512bw = (r[1] >> 30) & 1, vaes = (r[2] >> 9) & 1;
  // XCR0: the OS saves SSE and AVX state (0x6), and opmask, ZMM_Hi256, Hi16_ZMM (0xe0).
  if (vaes && avx512f && avx512bw && (xcr0 & 0xe6) == 0xe6) return 3;
  if (vaes && avx2 && aesni && (xcr0 & 0x6) == 0x6) return 2;
  return aesni;
}

// -1: not checked yet. Every thread computes the same value, so a race only repeats the check.
#if defined(__GNUC__) || defined(__clang__)
static int g_level = -1;
#define LOAD_LEVEL() __atomic_load_n(&g_level, __ATOMIC_RELAXED)
#define STORE_LEVEL(v) __atomic_store_n(&g_level, (v), __ATOMIC_RELAXED)
#else
static volatile int g_level = -1;
#define LOAD_LEVEL() (g_level)
#define STORE_LEVEL(v) (g_level = (v))
#endif

static int Level(void) {
  int s = LOAD_LEVEL();
  if (s < 0) {
    s = Detect();
    STORE_LEVEL(s);
  }
  return s;
}
#define PICK(name) (Level() == 3 ? name##_avx512 : Level() == 2 ? name##_avx2 : Level() == 1 ? name##_aesni : name##_portable)
#else
static int Level(void) { return 0; }
#define PICK(name) (name##_portable)
#endif

int pikaseko256_uses_avx512(void) { return Level() == 3; }
int pikaseko256_cpu_level(void) { return Level(); }

const char* pikaseko256_implementation(void) {
  static const char* const kNames[4] = {"portable", "AES-NI", "AVX2 + VAES", "AVX-512 + VAES"};
  return kNames[Level()];
}

void pikaseko256(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]) {
  PICK(pikaseko256)(data, len, key, out);
}
void pikaseko256_unkeyed(const void* data, size_t len, uint64_t out[4]) {
  PICK(pikaseko256)(data, len, NULL, out);
}
uint64_t pikaseko64(const void* data, size_t len, const uint64_t key[8]) {
  uint64_t out[4];
  pikaseko256(data, len, key, out);
  return out[0];
}
void pikaseko256_init(pikaseko256_ctx* ctx, const uint64_t key[8]) {
  PICK(pikaseko256_init)(ctx, key);
}
void pikaseko256_update(pikaseko256_ctx* ctx, const void* data, size_t len) {
  PICK(pikaseko256_update)(ctx, data, len);
}
void pikaseko256_final(const pikaseko256_ctx* ctx, uint64_t out[4]) {
  PICK(pikaseko256_final)(ctx, out);
}
void pikaseko256_steps(uint64_t state[128], unsigned t0, unsigned nsteps) {
  PICK(pikaseko256_steps)(state, t0, nsteps);
}
