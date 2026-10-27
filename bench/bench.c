// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Throughput and latency of pikaseko256 (unkeyed and keyed). Results are only
// meaningful on an idle machine with a fixed clock.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>

#include "pikaseko256.h"

static double Now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + 1e-9 * ts.tv_nsec; }

int main(int argc, char** argv) {
  const int keyed = argc > 1 && !strcmp(argv[1], "--keyed");
  const size_t sizes[] = {8, 32, 64, 128, 256, 1024, 4096, 16384, 65536, 1 << 20};
  uint8_t* buf = (uint8_t*)aligned_alloc(64, 1 << 20);
  for (size_t i = 0; i < (1 << 20); i++) buf[i] = (uint8_t)(i * 131 + 7);
  const uint64_t key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint64_t sink = 0, out[4];
  printf("pikaseko256 (%s), %s\n%10s %12s %12s %12s\n", pikaseko256_uses_avx512() ? "AVX-512" : "portable", keyed ? "keyed" : "unkeyed", "bytes", "GB/s", "ns/hash", "TSC cyc/B");
  for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
    const size_t n = sizes[si];
    const long iters = (long)(200000000 / (n + 64)) + 10;
    double best = 1e9; uint64_t bestc = ~0ull;
    for (int rep = 0; rep < 7; rep++) {
      const double t0 = Now(); const uint64_t c0 = __rdtsc();
      for (long it = 0; it < iters; it++) {
        buf[0] = (uint8_t)sink;   // a dependency between calls: latency for short inputs
        pikaseko256(buf, n, keyed ? key : NULL, out);
        sink += out[0];
      }
      const uint64_t c1 = __rdtsc(); const double t1 = Now();
      if (t1 - t0 < best) best = t1 - t0;
      if (c1 - c0 < bestc) bestc = c1 - c0;
    }
    const double ns = best * 1e9 / iters;
    printf("%10zu %12.2f %12.1f %12.3f\n", n, n / ns, ns, (double)bestc / iters / n);
  }
  printf("(sink %016llx)\n", (unsigned long long)sink);
  return 0;
}
