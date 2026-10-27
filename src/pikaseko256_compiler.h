// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Compiler portability for the SIMD implementations: forced inlining and loop unrolling
// hints for gcc and clang, and their MSVC equivalents (unrolling hints are dropped).
#ifndef PIKASEKO256_COMPILER_H_
#define PIKASEKO256_COMPILER_H_

#if defined(_MSC_VER) && !defined(__clang__)
#define INL static __forceinline
#define PIKASEKO_UNROLL(n)
#else
#define INL static inline __attribute__((always_inline))
#define PIKASEKO_PRAGMA(x) _Pragma(#x)
#define PIKASEKO_UNROLL(n) PIKASEKO_PRAGMA(GCC unroll n)
#endif

#endif  // PIKASEKO256_COMPILER_H_
