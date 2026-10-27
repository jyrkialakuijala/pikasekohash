// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: implementations that are reached through the dispatching functions of
// pikaseko256.h and are declared here for the tests and the benchmark. Each needs the named
// CPU features; none exists in a portable-only build.
#ifndef PIKASEKO256_IMPL_H_
#define PIKASEKO256_IMPL_H_

#include "pikaseko256.h"

// Which implementations this build has. PIKASEKO256_PORTABLE_ONLY (or the older name
// PIKASEKO256_NO_AVX512) leaves only the portable one.
#if defined(PIKASEKO256_NO_AVX512) && !defined(PIKASEKO256_PORTABLE_ONLY)
#define PIKASEKO256_PORTABLE_ONLY 1
#endif
#if !defined(PIKASEKO256_PORTABLE_ONLY) && (defined(__x86_64__) || defined(_M_X64))
#define PIKASEKO256_HAVE_X86 1
#endif
#if !defined(PIKASEKO256_PORTABLE_ONLY) && defined(__aarch64__) && !defined(__ARM_BIG_ENDIAN)
#define PIKASEKO256_HAVE_NEON 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// The level the dispatching functions use. x86-64: 3 AVX-512 + VAES, 2 AVX2 + VAES,
// 1 AES-NI; AArch64: 1 ARMv8 AES; 0 portable. Every level below the returned one is
// supported too.
int pikaseko256_cpu_level(void);

// x86-64: AVX2 + VAES (256-bit AES instructions).
void pikaseko256_avx2(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_init_avx2(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_avx2(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_avx2(const pikaseko256_ctx* ctx, uint64_t out[4]);
void pikaseko256_steps_avx2(uint64_t state[128], unsigned t0, unsigned nsteps);

// x86-64: SSE2 + AES-NI.
void pikaseko256_aesni(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_init_aesni(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_aesni(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_aesni(const pikaseko256_ctx* ctx, uint64_t out[4]);
void pikaseko256_steps_aesni(uint64_t state[128], unsigned t0, unsigned nsteps);

// AArch64 (little-endian) with the ARMv8 cryptographic extension (AESE, AESMC).
void pikaseko256_neon(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_init_neon(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_neon(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_neon(const pikaseko256_ctx* ctx, uint64_t out[4]);
void pikaseko256_steps_neon(uint64_t state[128], unsigned t0, unsigned nsteps);

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_IMPL_H_
