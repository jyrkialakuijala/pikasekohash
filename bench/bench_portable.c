// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Speed of the portable implementation, pikaseko256_portable (unkeyed and keyed).
// Plain C (clock_gettime, or C11 timespec_get on Windows), so it runs on any platform. Results are only
// meaningful on an idle machine with a fixed clock.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pikaseko256.h"

static double Now(void) {
#if defined(_WIN32)
  struct timespec ts; timespec_get(&ts, TIME_UTC);
#else
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

int main(int argc, char** argv) {
  const int keyed = argc > 1 && !strcmp(argv[1], "--keyed");
  const size_t sizes[] = {8, 64, 256, 1024, 4096, 65536};
  const size_t maxn = 65536;
  uint8_t* buf = (uint8_t*)malloc(maxn);
  if (!buf) return 1;
  for (size_t i = 0; i < maxn; i++) buf[i] = (uint8_t)(i * 131 + 7);
  const uint64_t key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint64_t sink = 0, out[4];
  printf("portable, %s\n%10s %12s %14s\n", keyed ? "keyed" : "unkeyed", "bytes", "MB/s", "ns/hash");
  for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
    const size_t n = sizes[si];
    long iters = 1;
    double best = 1e30;
    // Grow the iteration count until one repetition takes at least 50 ms; best of 5.
    for (;;) {
      const double t0 = Now();
      for (long it = 0; it < iters; it++) {
        buf[0] = (uint8_t)sink;  // a dependency between calls: latency for short inputs
        pikaseko256_portable(buf, n, keyed ? key : NULL, out);
        sink += out[0];
      }
      if (Now() - t0 >= 0.05) break;
      iters *= 2;
    }
    for (int rep = 0; rep < 5; rep++) {
      const double t0 = Now();
      for (long it = 0; it < iters; it++) {
        buf[0] = (uint8_t)sink;
        pikaseko256_portable(buf, n, keyed ? key : NULL, out);
        sink += out[0];
      }
      const double dt = Now() - t0;
      if (dt < best) best = dt;
    }
    const double ns = best * 1e9 / iters;
    printf("%10zu %12.2f %14.1f\n", n, n * 1e3 / ns, ns);
  }
  printf("(sink %016llx)\n", (unsigned long long)sink);
  free(buf);
  return 0;
}
