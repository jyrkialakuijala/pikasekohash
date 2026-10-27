// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256 version 1: a 256-bit hash (and a 64-bit one) built from AES rounds, keyed
// or unkeyed. Experimental. See README.md, SPEC.md and ANALYSIS.md.
//
// The functions use the fastest implementation the CPU and OS support (checked once, at the
// first call): AVX-512 + VAES, AVX2 + VAES or AES-NI on x86-64, the ARMv8 AES instructions on
// AArch64, else portable C. The results are the same on every CPU and byte order.
//
// Unkeyed and keyed hashing are separate functions and separate functions of the data: the
// keyed hash with any key, including an empty or all-zero one, differs from the unkeyed
// hash, and the 64-bit hash is not a part of the 256-bit one. A key is any number of bytes;
// keys longer than 64 bytes are hashed first (SPEC.md, section 4). Use a uniformly random
// secret key of 16 to 64 bytes for a MAC or a PRF.
#ifndef PIKASEKO256_H_
#define PIKASEKO256_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIKASEKO256_DIGEST_BYTES 32

// One-shot. The 256-bit digest is 32 bytes; the 64-bit hash is returned as a number (its
// bytes are the 8-byte digest of SPEC.md, little-endian). key may be NULL when key_len is 0.
void pikaseko256_unkeyed(const void* data, size_t len, uint8_t digest[32]);
void pikaseko256_keyed(const void* data, size_t len, const void* key, size_t key_len, uint8_t digest[32]);
uint64_t pikaseko64_unkeyed(const void* data, size_t len);
uint64_t pikaseko64_keyed(const void* data, size_t len, const void* key, size_t key_len);

// Streaming: the same functions for a message given in pieces. Start with one of the four
// init functions, add data with update, and finish with the final function of the same
// width (pikaseko256_final after a pikaseko256_init_*, pikaseko64_final after a
// pikaseko64_init_*). final does not change the context: more data can still be added.
// The context holds no pointers and can be copied.
typedef struct {
  uint64_t r[128];   // state after step t: word 8i + l is lane l of register r_i
  uint64_t len;      // message bytes so far
  unsigned t;        // step index at which the next group of four packets is absorbed
  unsigned nbuf;     // bytes in buf (0..255): the incomplete group of four packets
  unsigned out_len;  // digest bytes: 32 or 8
  uint8_t buf[256];
} pikaseko256_ctx;
void pikaseko256_init_unkeyed(pikaseko256_ctx* ctx);
void pikaseko256_init_keyed(pikaseko256_ctx* ctx, const void* key, size_t key_len);
void pikaseko64_init_unkeyed(pikaseko256_ctx* ctx);
void pikaseko64_init_keyed(pikaseko256_ctx* ctx, const void* key, size_t key_len);
void pikaseko256_update(pikaseko256_ctx* ctx, const void* data, size_t len);
void pikaseko256_final(const pikaseko256_ctx* ctx, uint8_t digest[32]);
uint64_t pikaseko64_final(const pikaseko256_ctx* ctx);

// The implementation in use: "AVX-512 + VAES", "AVX2 + VAES", "AES-NI", "ARMv8 AES" or
// "portable", and whether it is the AVX-512 one. Setting the environment variable
// PIKASEKO256_FORCE_PORTABLE (to anything but "0") before the first call selects the
// portable implementation (constant time without relying on the AES instructions).
const char* pikaseko256_implementation(void);
int pikaseko256_uses_avx512(void);

// The step function alone, for analysis, is declared in pikaseko256_analysis.h.

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_H_
