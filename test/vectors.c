// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Prints the test vectors (test/test_vectors.txt): data byte i = i mod 256, unkeyed and
// with key {1, ..., 8}, at the padding corner lengths, plus a checksum over lengths 0..2048.
// Uses the AVX-512 code when the CPU supports it, else the portable code (the name of the
// implementation goes to stderr, so the output is the same either way).
#include <stdint.h>
#include <stdio.h>

#include "cpu.h"
#include "pikaseko256.h"

int main(void) {
  static uint8_t buf[4200];
  for (int i = 0; i < 4200; i++) buf[i] = (uint8_t)i;
  const size_t lens[] = {0, 1, 55, 56, 63, 64, 255, 256, 1023, 1024, 1025, 4096};
  const uint64_t key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint64_t o[4], acc = 0;
  void (*hash)(const void*, size_t, const uint64_t*, uint64_t*) = pikaseko256_portable;
  void (*unkeyed)(const void*, size_t, uint64_t*) = pikaseko256_unkeyed_portable;
#ifndef PIKASEKO256_NO_AVX512
  if (PikasekoHaveAvx512()) { hash = pikaseko256; unkeyed = pikaseko256_unkeyed; }
#endif
  fprintf(stderr, "vectors: %s implementation\n", hash == pikaseko256_portable ? "portable" : "AVX-512");
  printf("PikasekoHash-256 test vectors (data byte i = i mod 256; lanes 0..3 as hex)\n");
  for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
    unkeyed(buf, lens[i], o);
    printf("unkeyed len %5zu  %016llx %016llx %016llx %016llx\n", lens[i], (unsigned long long)o[0], (unsigned long long)o[1], (unsigned long long)o[2], (unsigned long long)o[3]);
    hash(buf, lens[i], key, o);
    printf("key 1..8 len %5zu  %016llx %016llx %016llx %016llx\n", lens[i], (unsigned long long)o[0], (unsigned long long)o[1], (unsigned long long)o[2], (unsigned long long)o[3]);
  }
  for (size_t len = 0; len <= 2048; len++) {
    unkeyed(buf, len, o);
    for (int i = 0; i < 4; i++) acc = acc * 0x9E3779B97F4A7C15ull ^ o[i];
  }
  printf("unkeyed, all lengths 0..2048 folded: %016llx\n", (unsigned long long)acc);
  return 0;
}
