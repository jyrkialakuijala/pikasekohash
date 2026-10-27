// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256 for ARMv8 with the cryptographic extension (AESE, AESMC), little-endian:
// a 512-bit register is four 128-bit vectors, one per AES block. The x86 round
// aesenc(x, k) = MixColumns(ShiftRows(SubBytes(x))) ^ k is AESMC(AESE(x, 0)) ^ k; in two
// rounds in a row the compiler folds the xor into the key operand of the next AESE.
#include <arm_neon.h>

typedef uint64x2_t S;
#define W 4
#define SXor veorq_u64
#define SAdd vaddq_u64
#include "pikaseko256_compiler.h"
INL uint64x2_t SAes(uint64x2_t x, uint64x2_t k) {
  const uint8x16_t y = vaesmcq_u8(vaeseq_u8(vreinterpretq_u8_u64(x), vdupq_n_u8(0)));
  return veorq_u64(vreinterpretq_u64_u8(y), k);
}
INL uint64x2_t SMul(uint64x2_t a, uint64x2_t b) { return vmull_u32(vmovn_u64(a), vmovn_u64(b)); }
#define SLoad(p) vreinterpretq_u64_u8(vld1q_u8((const uint8_t*)(p)))
#define SStore(p, x) vst1q_u8((uint8_t*)(p), vreinterpretq_u8_u64(x))
#define ROT1(x) ((R){{(x).v[1], (x).v[2], (x).v[3], (x).v[0]}})
#define ROT2(x) ((R){{(x).v[2], (x).v[3], (x).v[0], (x).v[1]}})
#define SUFFIX _neon
#define UNROLL_FINALS 0
#include "pikaseko256_simd.h"
