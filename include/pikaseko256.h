// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: a 256-bit hash for AVX-512 + VAES, with a portable C reference.
// Experimental. See README.md, SPEC.md and ANALYSIS.md.
#ifndef PIKASEKO256_H_
#define PIKASEKO256_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Digest: four 64-bit lanes. As bytes, lanes 0..3 in order, each little-endian (32 bytes).
// Key: eight 64-bit lanes (512 bits), or NULL for the all-zero key. The unkeyed hash is
// the keyed hash with the zero key; there is no domain separation between the two yet.

// AVX-512 implementation (needs AVX512F, AVX512BW and VAES; the caller checks the CPU).
void pikaseko256(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_unkeyed(const void* data, size_t len, uint64_t out[4]);
uint64_t pikaseko64(const void* data, size_t len, const uint64_t key[8]);  // lane 0 of the digest

// Portable C implementation: same results on any CPU and byte order, no tables,
// no data- or key-dependent branches or memory accesses (much slower).
void pikaseko256_portable(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_unkeyed_portable(const void* data, size_t len, uint64_t out[4]);
uint64_t pikaseko64_portable(const void* data, size_t len, const uint64_t key[8]);

// The step function alone, for analysis, is declared in pikaseko256_analysis.h.

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_H_
