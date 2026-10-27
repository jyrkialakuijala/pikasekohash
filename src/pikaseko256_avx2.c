// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256 for AVX2 + VAES (256-bit AES instructions, e.g. Zen 3, Alder Lake):
// a 512-bit register is two ymm vectors (blocks 0, 1 and blocks 2, 3).
#include <immintrin.h>

typedef __m256i S;
#define W 2
#define SXor _mm256_xor_si256
#define SOr _mm256_or_si256
#define SShl(x, n) _mm256_sll_epi64((x), _mm_cvtsi32_si128(n))
#define SShr(x, n) _mm256_srl_epi64((x), _mm_cvtsi32_si128(n))
#define SAdd _mm256_add_epi64
#define SAes _mm256_aesenc_epi128
#define SMul _mm256_mul_epu32
#define SLoad(p) _mm256_loadu_si256((const __m256i*)(p))
#define SStore(p, x) _mm256_storeu_si256((__m256i*)(p), (x))
// rot1: blocks (1, 2 | 3, 0); rot2: the two halves swapped.
#define ROT1(x) ((R){{_mm256_permute2x128_si256((x).v[0], (x).v[1], 0x21), _mm256_permute2x128_si256((x).v[1], (x).v[0], 0x21)}})
#define ROT2(x) ((R){{(x).v[1], (x).v[0]}})
#define SUFFIX _avx2
#define UNROLL_FINALS 1
#include "pikaseko256_simd.h"
