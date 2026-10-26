// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Run-time check for the AVX-512 implementation, shared by the tests and tools.
// PikasekoHaveAvx512() is 1 when the library contains the AVX-512 code (it was not built
// with PIKASEKO256_NO_AVX512), the CPU has AVX512F, AVX512BW and VAES, the OS saves the
// zmm state, and the environment variable PIKASEKO256_FORCE_PORTABLE is unset or "0".
#ifndef PIKASEKO256_TEST_CPU_H_
#define PIKASEKO256_TEST_CPU_H_

#include <stdlib.h>

#if !defined(PIKASEKO256_NO_AVX512) && (defined(__x86_64__) || defined(_M_X64)) && \
    (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#define PIKASEKO256_TEST_X86 1
#endif

static inline int PikasekoForcePortable(void) {
  const char* e = getenv("PIKASEKO256_FORCE_PORTABLE");
  return e && *e && !(e[0] == '0' && e[1] == 0);
}

static inline int PikasekoHaveAvx512(void) {
#ifdef PIKASEKO256_TEST_X86
  if (PikasekoForcePortable()) return 0;
  unsigned a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d) || !(c & (1u << 27))) return 0;  // OSXSAVE
  unsigned lo, hi;
  __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
  (void)hi;
  if ((lo & 0xe6) != 0xe6) return 0;  // XCR0: SSE, AVX, opmask, ZMM_Hi256, Hi16_ZMM state
  if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
  if (!(c & (1u << 9))) return 0;  // VAES
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
#else
  return 0;
#endif
}

#endif  // PIKASEKO256_TEST_CPU_H_
