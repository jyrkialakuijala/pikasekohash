// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: the dispatching entry points. They call the AVX-512 implementation when
// the library contains it and the CPU and OS support AVX512F, AVX512BW and VAES, and the
// portable implementation otherwise. The check runs once and its result is cached.
#include <stdlib.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"

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

static int Detect(void) {
  const char* e = getenv("PIKASEKO256_FORCE_PORTABLE");
  if (e && *e && !(e[0] == '0' && e[1] == 0)) return 0;
  unsigned r[4];
  Cpuid(0, 0, r);
  if (r[0] < 7) return 0;
  Cpuid(1, 0, r);
  if (!(r[2] & (1u << 27))) return 0;  // OSXSAVE
  // XCR0: the OS saves SSE, AVX, opmask, ZMM_Hi256 and Hi16_ZMM state.
  if ((Xcr0() & 0xe6) != 0xe6) return 0;
  Cpuid(7, 0, r);
  const int avx512f = (r[1] >> 16) & 1, avx512bw = (r[1] >> 30) & 1, vaes = (r[2] >> 9) & 1;
  return avx512f && avx512bw && vaes;
}

// -1: not checked yet. Every thread computes the same value, so a race only repeats the check.
#if defined(__GNUC__) || defined(__clang__)
static int g_avx = -1;
#define LOAD_AVX() __atomic_load_n(&g_avx, __ATOMIC_RELAXED)
#define STORE_AVX(v) __atomic_store_n(&g_avx, (v), __ATOMIC_RELAXED)
#else
static volatile int g_avx = -1;
#define LOAD_AVX() (g_avx)
#define STORE_AVX(v) (g_avx = (v))
#endif

static int UseAvx(void) {
  int s = LOAD_AVX();
  if (s < 0) {
    s = Detect();
    STORE_AVX(s);
  }
  return s;
}
#define PICK(avx, portable) (UseAvx() ? (avx) : (portable))
#else
static int UseAvx(void) { return 0; }
#define PICK(avx, portable) (portable)
#endif

int pikaseko256_uses_avx512(void) { return UseAvx(); }

void pikaseko256(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]) {
  PICK(pikaseko256_avx512, pikaseko256_portable)(data, len, key, out);
}
void pikaseko256_unkeyed(const void* data, size_t len, uint64_t out[4]) {
  PICK(pikaseko256_avx512, pikaseko256_portable)(data, len, NULL, out);
}
uint64_t pikaseko64(const void* data, size_t len, const uint64_t key[8]) {
  uint64_t out[4];
  pikaseko256(data, len, key, out);
  return out[0];
}
void pikaseko256_init(pikaseko256_ctx* ctx, const uint64_t key[8]) {
  PICK(pikaseko256_init_avx512, pikaseko256_init_portable)(ctx, key);
}
void pikaseko256_update(pikaseko256_ctx* ctx, const void* data, size_t len) {
  PICK(pikaseko256_update_avx512, pikaseko256_update_portable)(ctx, data, len);
}
void pikaseko256_final(const pikaseko256_ctx* ctx, uint64_t out[4]) {
  PICK(pikaseko256_final_avx512, pikaseko256_final_portable)(ctx, out);
}
void pikaseko256_steps(uint64_t state[128], unsigned t0, unsigned nsteps) {
  PICK(pikaseko256_steps_avx512, pikaseko256_steps_portable)(state, t0, nsteps);
}
