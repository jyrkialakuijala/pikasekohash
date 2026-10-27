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

// Byte forms, independent of the host byte order: a 64-byte key to the eight key words
// (word i = bytes 8i .. 8i + 7, little-endian), and a digest to its canonical 32 bytes.
static inline void pikaseko256_key_from_bytes(uint64_t key[8], const uint8_t bytes[64]) {
  for (int i = 0; i < 8; i++) {
    uint64_t v = 0;
    for (int j = 7; j >= 0; j--) v = (v << 8) | bytes[8 * i + j];
    key[i] = v;
  }
}
static inline void pikaseko256_digest_to_bytes(uint8_t bytes[32], const uint64_t out[4]) {
  for (int i = 0; i < 32; i++) bytes[i] = (uint8_t)(out[i / 8] >> (8 * (i % 8)));
}

// Hash functions. They use the AVX-512 implementation when the library has it and the CPU
// and OS support AVX512F, AVX512BW and VAES (checked once, at the first call), and the
// portable implementation otherwise. The results are the same either way.
void pikaseko256(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_unkeyed(const void* data, size_t len, uint64_t out[4]);
uint64_t pikaseko64(const void* data, size_t len, const uint64_t key[8]);  // lane 0 of the digest

// Streaming: the same function for a message given in pieces. final does not change the
// context (more data can still be added). The context holds the state in normal form and
// is the same for every implementation, so a hash can be continued with any of them.
typedef struct {
  uint64_t r[128];  // state: word 8i + l is lane l of register r_i
  uint64_t len;     // message bytes so far
  unsigned t;       // next step index
  unsigned nbuf;    // bytes in buf (0..255): the incomplete group of four packets
  uint8_t buf[256];
} pikaseko256_ctx;
void pikaseko256_init(pikaseko256_ctx* ctx, const uint64_t key[8]);  // key NULL: zero key
void pikaseko256_update(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final(const pikaseko256_ctx* ctx, uint64_t out[4]);

// 1 when the functions above use the AVX-512 implementation. Setting the environment
// variable PIKASEKO256_FORCE_PORTABLE (to anything but "0") before the first call selects
// the portable implementation.
int pikaseko256_uses_avx512(void);

// The implementations by name. The _avx512 functions need AVX512F, AVX512BW and VAES (the
// caller checks the CPU, e.g. with pikaseko256_uses_avx512) and are absent from a library
// built without them (PIKASEKO256_NO_AVX512). The _portable functions work everywhere:
// same results on any CPU and byte order, no tables, no data- or key-dependent branches
// or memory accesses (much slower).
void pikaseko256_avx512(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_unkeyed_avx512(const void* data, size_t len, uint64_t out[4]);
uint64_t pikaseko64_avx512(const void* data, size_t len, const uint64_t key[8]);
void pikaseko256_init_avx512(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_avx512(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_avx512(const pikaseko256_ctx* ctx, uint64_t out[4]);

void pikaseko256_portable(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_unkeyed_portable(const void* data, size_t len, uint64_t out[4]);
uint64_t pikaseko64_portable(const void* data, size_t len, const uint64_t key[8]);
void pikaseko256_init_portable(pikaseko256_ctx* ctx, const uint64_t key[8]);
void pikaseko256_update_portable(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final_portable(const pikaseko256_ctx* ctx, uint64_t out[4]);

// The step function alone, for analysis, is declared in pikaseko256_analysis.h.

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_H_
