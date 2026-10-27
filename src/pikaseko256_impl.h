// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: implementations that are reached through the dispatching functions of
// pikaseko256.h and are declared here for the tests. Each needs the named CPU features;
// none exists in a portable-only build (PIKASEKO256_NO_AVX512).
#ifndef PIKASEKO256_IMPL_H_
#define PIKASEKO256_IMPL_H_

#include "pikaseko256.h"

#ifdef __cplusplus
extern "C" {
#endif

// The level the dispatching functions use: 3 AVX-512 + VAES, 2 AVX2 + VAES, 1 AES-NI,
// 0 portable. Every level below the returned one is supported too.
int pikaseko256_cpu_level(void);

// AVX2 + VAES (256-bit AES instructions).
void pikaseko256_avx2(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_init_avx2(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_avx2(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_avx2(const pikaseko256_ctx* ctx, uint64_t out[4]);
void pikaseko256_steps_avx2(uint64_t state[128], unsigned t0, unsigned nsteps);

// SSE2 + AES-NI.
void pikaseko256_aesni(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_init_aesni(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_aesni(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_aesni(const pikaseko256_ctx* ctx, uint64_t out[4]);
void pikaseko256_steps_aesni(uint64_t state[128], unsigned t0, unsigned nsteps);

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_IMPL_H_
