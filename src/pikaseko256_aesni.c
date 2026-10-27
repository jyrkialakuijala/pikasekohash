// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256 for SSE2 + AES-NI (every x86-64 CPU with AES-NI): a 512-bit register is
// four xmm vectors, one per AES block, so the block rotations are renamings.
#include <immintrin.h>

typedef __m128i S;
#define W 4
#define SXor _mm_xor_si128
#define SAdd _mm_add_epi64
#define SAes _mm_aesenc_si128
#define SMul _mm_mul_epu32
#define SLoad(p) _mm_loadu_si128((const __m128i*)(p))
#define SStore(p, x) _mm_storeu_si128((__m128i*)(p), (x))
#define ROT1(x) ((R){{(x).v[1], (x).v[2], (x).v[3], (x).v[0]}})
#define ROT2(x) ((R){{(x).v[2], (x).v[3], (x).v[0], (x).v[1]}})
#define SUFFIX _aesni
#define UNROLL_FINALS 0
#include "pikaseko256_simd.h"
