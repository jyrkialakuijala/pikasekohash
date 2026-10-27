// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: the implementations behind the public functions of pikaseko256.h, for
// the dispatch, the tests and the benchmark. Each implementation has the same five core
// functions; the public layer (src/pikaseko256_dispatch.c) turns keys of any length into the
// eight key words and the domain word, and digests into bytes.
//
//   hash(data, len, key, dom, out)  key: eight 64-bit words, or NULL for the unkeyed hash;
//                                   dom: the domain word (SPEC.md, section 4); out: the four
//                                   64-bit words of the fold (the digest is their bytes)
//   init(ctx, key, dom)  update(ctx, data, len)  final(ctx, out)   streaming
//   steps(state, t0, nsteps)        the step function alone (pikaseko256_analysis.h)
//
// For the unkeyed hash, dom must be PIKASEKO_DOMAIN_UNKEYED256 or PIKASEKO_DOMAIN_UNKEYED64
// (the implementations that start from a precomputed state rely on it).
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

// The level the public functions use. x86-64: 3 AVX-512 + VAES, 2 AVX2 + VAES, 1 AES-NI;
// AArch64: 1 ARMv8 AES; 0 portable. Every level below the returned one is supported too.
int pikaseko256_cpu_level(void);

// The public key handling (SPEC.md, section 4): a key of any length (NULL when key_len is
// 0) to the eight key words K and the keyed domain word for a digest of out_len bytes.
uint64_t pikaseko_key_setup(const void* key, size_t key_len, unsigned out_len, uint64_t K[8]);

#define PIKASEKO_DECLARE_IMPL(sfx)                                                                       \
  void pikaseko_hash_##sfx(const void* data, size_t len, const uint64_t key[8], uint64_t dom, uint64_t out[4]); \
  void pikaseko_init_##sfx(pikaseko256_ctx* ctx, const uint64_t key[8], uint64_t dom);                  \
  void pikaseko_update_##sfx(pikaseko256_ctx* ctx, const void* data, size_t len);                       \
  void pikaseko_final_##sfx(const pikaseko256_ctx* ctx, uint64_t out[4]);                               \
  void pikaseko_steps_##sfx(uint64_t state[128], unsigned t0, unsigned nsteps);

PIKASEKO_DECLARE_IMPL(portable)  // any CPU and byte order, constant time, slow
PIKASEKO_DECLARE_IMPL(avx512)    // x86-64: AVX-512F, AVX-512BW, VAES
PIKASEKO_DECLARE_IMPL(avx2)      // x86-64: AVX2, VAES, AES-NI
PIKASEKO_DECLARE_IMPL(aesni)     // x86-64: AES-NI
PIKASEKO_DECLARE_IMPL(neon)      // AArch64, little-endian, with the AES extension

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_IMPL_H_
