// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: the dispatching entry points. They call the fastest implementation the
// CPU and OS support: on x86-64 AVX-512 + VAES, AVX2 + VAES or AES-NI, on AArch64 the ARMv8
// AES instructions, else the portable one. The check runs once and its result is cached.
#include <stdlib.h>
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_analysis.h"
#include "pikaseko256_impl.h"
#include "pikaseko256_consts.h"

#if defined(PIKASEKO256_HAVE_X86) || defined(PIKASEKO256_HAVE_NEON)
static int ForcePortable(void) {
  const char* e = getenv("PIKASEKO256_FORCE_PORTABLE");
  return e && *e && !(e[0] == '0' && e[1] == 0);
}
#endif

#if defined(PIKASEKO256_HAVE_X86)
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

// 3 AVX-512 + VAES, 2 AVX2 + VAES, 1 AES-NI, 0 portable.
static int Detect(void) {
  if (ForcePortable()) return 0;
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
static const char* const kNames[4] = {"portable", "AES-NI", "AVX2 + VAES", "AVX-512 + VAES"};
#define PICK(name) (Level() == 3 ? name##_avx512 : Level() == 2 ? name##_avx2 : Level() == 1 ? name##_aesni : name##_portable)

#elif defined(PIKASEKO256_HAVE_NEON)
#if defined(__APPLE__)
static int Detect(void) { return ForcePortable() ? 0 : 1; }  // every Apple AArch64 CPU has AES
#elif defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP_AES
#define HWCAP_AES (1 << 3)
#endif
static int Detect(void) { return !ForcePortable() && (getauxval(AT_HWCAP) & HWCAP_AES) != 0; }
#elif defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)
static int Detect(void) { return ForcePortable() ? 0 : 1; }  // the compiler targets AES already
#else
static int Detect(void) { return 0; }  // no known way to ask the OS
#endif
static const char* const kNames[2] = {"portable", "ARMv8 AES"};
#define PICK(name) (Level() == 1 ? name##_neon : name##_portable)

#else
static int Detect(void) { return 0; }
static const char* const kNames[1] = {"portable"};
#define PICK(name) (name##_portable)
#endif

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

int pikaseko256_cpu_level(void) { return Level(); }
const char* pikaseko256_implementation(void) { return kNames[Level()]; }
int pikaseko256_uses_avx512(void) {
#if defined(PIKASEKO256_HAVE_X86)
  return Level() == 3;
#else
  return 0;
#endif
}

// ---- The public functions: keys, domain words and digests (SPEC.md, sections 4 and 9) ----

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_M_X64) || defined(_M_ARM64)
#define LITTLE_ENDIAN_HOST 1
#endif

// Clears secret material so that the compiler cannot drop the stores as dead.
static void Wipe(void* p, size_t n) {
#if defined(__GNUC__) || defined(__clang__)
  memset(p, 0, n);
  __asm__ volatile("" : : "r"(p) : "memory");
#else
  volatile uint8_t* q = (volatile uint8_t*)p;
  while (n--) *q++ = 0;
#endif
}

// A key of any length to the eight key words and the domain word. Keys longer than 64
// bytes are replaced by their unkeyed 256-bit digest (flag bit 1 of the domain word).
uint64_t pikaseko_key_setup(const void* key, size_t key_len, unsigned out_len, uint64_t K[8]) {
  uint8_t kb[64] = {0};
  int hashed = 0;
  if (key_len > 64) {
    pikaseko256_unkeyed(key, key_len, kb);
    key_len = 32;
    hashed = 1;
  } else if (key_len) {
    memcpy(kb, key, key_len);
  }
#ifdef LITTLE_ENDIAN_HOST
  memcpy(K, kb, 64);
#else
  for (int i = 0; i < 8; i++) {
    uint64_t v = 0;
    for (int j = 7; j >= 0; j--) v = (v << 8) | kb[8 * i + j];
    K[i] = v;
  }
#endif
  Wipe(kb, sizeof kb);
  return PIKASEKO_DOMAIN(1, hashed, key_len, out_len);
}

static void DigestBytes(const uint64_t w[4], uint8_t* digest, unsigned n) {
#ifdef LITTLE_ENDIAN_HOST
  memcpy(digest, w, n);
#else
  for (unsigned i = 0; i < n; i++) digest[i] = (uint8_t)(w[i / 8] >> (8 * (i % 8)));
#endif
}

void pikaseko256_unkeyed(const void* data, size_t len, uint8_t digest[32]) {
  uint64_t w[4];
  PICK(pikaseko_hash)(data, len, NULL, PIKASEKO_DOMAIN_UNKEYED256, w);
  DigestBytes(w, digest, 32);
}
void pikaseko256_keyed(const void* data, size_t len, const void* key, size_t key_len, uint8_t digest[32]) {
  uint64_t K[8], w[4];
  const uint64_t dom = pikaseko_key_setup(key, key_len, 32, K);
  PICK(pikaseko_hash)(data, len, K, dom, w);
  Wipe(K, sizeof K);
  DigestBytes(w, digest, 32);
}
uint64_t pikaseko64_unkeyed(const void* data, size_t len) {
  uint64_t w[4];
  PICK(pikaseko_hash)(data, len, NULL, PIKASEKO_DOMAIN_UNKEYED64, w);
  return w[0];
}
uint64_t pikaseko64_keyed(const void* data, size_t len, const void* key, size_t key_len) {
  uint64_t K[8], w[4];
  const uint64_t dom = pikaseko_key_setup(key, key_len, 8, K);
  PICK(pikaseko_hash)(data, len, K, dom, w);
  Wipe(K, sizeof K);
  return w[0];
}

void pikaseko256_init_unkeyed(pikaseko256_ctx* ctx) {
  PICK(pikaseko_init)(ctx, NULL, PIKASEKO_DOMAIN_UNKEYED256);
  ctx->out_len = 32;
}
void pikaseko256_init_keyed(pikaseko256_ctx* ctx, const void* key, size_t key_len) {
  uint64_t K[8];
  const uint64_t dom = pikaseko_key_setup(key, key_len, 32, K);
  PICK(pikaseko_init)(ctx, K, dom);
  Wipe(K, sizeof K);
  ctx->out_len = 32;
}
void pikaseko64_init_unkeyed(pikaseko256_ctx* ctx) {
  PICK(pikaseko_init)(ctx, NULL, PIKASEKO_DOMAIN_UNKEYED64);
  ctx->out_len = 8;
}
void pikaseko64_init_keyed(pikaseko256_ctx* ctx, const void* key, size_t key_len) {
  uint64_t K[8];
  const uint64_t dom = pikaseko_key_setup(key, key_len, 8, K);
  PICK(pikaseko_init)(ctx, K, dom);
  Wipe(K, sizeof K);
  ctx->out_len = 8;
}
void pikaseko256_update(pikaseko256_ctx* ctx, const void* data, size_t len) {
  PICK(pikaseko_update)(ctx, data, len);
}
void pikaseko256_final(const pikaseko256_ctx* ctx, uint8_t digest[32]) {
  uint64_t w[4];
  PICK(pikaseko_final)(ctx, w);
  DigestBytes(w, digest, 32);
}
uint64_t pikaseko64_final(const pikaseko256_ctx* ctx) {
  uint64_t w[4];
  PICK(pikaseko_final)(ctx, w);
  return w[0];
}
void pikaseko256_steps(uint64_t state[128], unsigned t0, unsigned nsteps) {
  PICK(pikaseko_steps)(state, t0, nsteps);
}
