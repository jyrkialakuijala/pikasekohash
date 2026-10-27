// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Speed of PikasekoHash-256 on one core: nanoseconds per hash and throughput, for any
// implementation the build and the CPU have.
//
//   bench [--keyed] [--impl NAME] [--reps N] [--max BYTES]
//     NAME: auto (default: what pikaseko256() uses), avx512, avx2, aesni, neon, portable
//
// Each call depends on the previous result (its first message byte is taken from the last
// digest), so short inputs measure latency, as a hash table lookup sees it. The iteration
// count of a repetition is chosen so that one repetition takes about 20 ms; the table gives
// the median and the minimum over the repetitions and the spread (max - min) / median.
// On x86-64 the TSC column is reference cycles per byte, which equal core cycles only at
// the nominal clock. Results are only meaningful on an idle machine with a fixed clock:
// the governor line shows the Linux cpufreq setting where it can be read.
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pikaseko256.h"
#include "pikaseko256_impl.h"

#if defined(PIKASEKO256_HAVE_X86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#define HAVE_TSC 1
static uint64_t Tsc(void) { return __rdtsc(); }
#endif

static double Now(void) {
#if defined(_WIN32)
  struct timespec ts; timespec_get(&ts, TIME_UTC);
#else
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

typedef void HashFn(const void*, size_t, const uint64_t*, uint64_t*);
typedef struct { const char* name; int level; HashFn* hash; } Impl;
static const Impl kImpls[] = {
    {"auto", 0, pikaseko256},
    {"portable", 0, pikaseko256_portable},
#if defined(PIKASEKO256_HAVE_X86)
    {"avx512", 3, pikaseko256_avx512},
    {"avx2", 2, pikaseko256_avx2},
    {"aesni", 1, pikaseko256_aesni},
#elif defined(PIKASEKO256_HAVE_NEON)
    {"neon", 1, pikaseko256_neon},
#endif
};

static int CmpDouble(const void* a, const void* b) {
  const double x = *(const double*)a, y = *(const double*)b;
  return x < y ? -1 : x > y;
}

static void PrintGovernor(void) {
  FILE* f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
  char g[64] = "unknown";
  if (f) {
    if (!fgets(g, sizeof g, f)) strcpy(g, "unknown");
    fclose(f);
    g[strcspn(g, "\n")] = 0;
  }
  printf("cpufreq governor (cpu0): %s%s\n", g,
         strcmp(g, "performance") && strcmp(g, "unknown") ? "  (results vary with the clock; use 'performance')" : "");
}

int main(int argc, char** argv) {
  int keyed = 0, reps = 11;
  size_t maxn = (size_t)1 << 20;
  const Impl* im = &kImpls[0];
  for (int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--keyed")) { keyed = 1; continue; }
    if (!strcmp(argv[a], "--reps") && a + 1 < argc) { reps = atoi(argv[++a]); if (reps < 1) reps = 1; continue; }
    if (!strcmp(argv[a], "--max") && a + 1 < argc) { maxn = (size_t)strtoull(argv[++a], NULL, 10); continue; }
    if (!strcmp(argv[a], "--impl") && a + 1 < argc) {
      const char* name = argv[++a];
      im = NULL;
      for (size_t i = 0; i < sizeof kImpls / sizeof kImpls[0]; i++)
        if (!strcmp(kImpls[i].name, name)) im = &kImpls[i];
      if (!im) { fprintf(stderr, "implementation %s is not in this build\n", name); return 1; }
      continue;
    }
    fprintf(stderr, "usage: %s [--keyed] [--impl auto|avx512|avx2|aesni|neon|portable] [--reps N] [--max BYTES]\n", argv[0]);
    return 1;
  }
  if (im->level > pikaseko256_cpu_level()) { fprintf(stderr, "this CPU does not support %s\n", im->name); return 1; }

  static const size_t kSizes[] = {8, 32, 64, 128, 256, 1024, 4096, 16384, 65536, 1 << 20};
  uint8_t* raw = (uint8_t*)malloc(maxn + 64);
  if (!raw) return 1;
  uint8_t* buf = raw + (64 - (uintptr_t)raw % 64);  // 64-byte aligned
  for (size_t i = 0; i < maxn; i++) buf[i] = (uint8_t)(i * 131 + 7);
  const uint64_t key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint64_t sink = 0, out[4];
  double* t = (double*)malloc(sizeof(double) * (size_t)reps);
  if (!t) return 1;

  printf("PikasekoHash-256, %s implementation, %s\n", im == &kImpls[0] ? pikaseko256_implementation() : im->name,
         keyed ? "keyed" : "unkeyed");
  PrintGovernor();
  printf("%10s %12s %12s %10s %10s", "bytes", "ns median", "ns min", "spread", "MB/s");
#ifdef HAVE_TSC
  printf(" %10s", "TSC cyc/B");
#endif
  printf("\n");
  for (size_t si = 0; si < sizeof kSizes / sizeof kSizes[0] && kSizes[si] <= maxn; si++) {
    const size_t n = kSizes[si];
    long iters = 1;
    for (;;) {  // calibrate: about 20 ms per repetition
      const double t0 = Now();
      for (long it = 0; it < iters; it++) {
        buf[0] = (uint8_t)sink;
        im->hash(buf, n, keyed ? key : NULL, out);
        sink += out[0];
      }
      if (Now() - t0 >= 0.02 || iters > (1L << 40)) break;
      iters *= 2;
    }
#ifdef HAVE_TSC
    uint64_t cyc_best = ~0ull;
#endif
    for (int rep = 0; rep < reps; rep++) {
#ifdef HAVE_TSC
      const uint64_t c0 = Tsc();
#endif
      const double t0 = Now();
      for (long it = 0; it < iters; it++) {
        buf[0] = (uint8_t)sink;  // a dependency between calls: latency for short inputs
        im->hash(buf, n, keyed ? key : NULL, out);
        sink += out[0];
      }
      t[rep] = (Now() - t0) * 1e9 / (double)iters;
#ifdef HAVE_TSC
      const uint64_t c = Tsc() - c0;
      if (c < cyc_best) cyc_best = c;
#endif
    }
    qsort(t, (size_t)reps, sizeof(double), CmpDouble);
    const double med = t[reps / 2];
    printf("%10zu %12.1f %12.1f %9.1f%% %10.1f", n, med, t[0], 100.0 * (t[reps - 1] - t[0]) / med, 1e3 * (double)n / med);
#ifdef HAVE_TSC
    printf(" %10.3f", (double)cyc_best / (double)iters / (double)n);
#endif
    printf("\n");
  }
  printf("(sink %016llx)\n", (unsigned long long)sink);
  free(t);
  free(raw);
  return 0;
}
