// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Prints the test vectors (test/test_vectors.txt), the same lines as
// `python3 ref/pikaseko256.py --print-vectors`: data byte i = i mod 256, key byte i = i, at the
// padding corner lengths; unkeyed and keyed (key lengths 0, 32, 64 and 100, which is hashed
// first) 256-bit digests and 64-bit hashes as bytes; and a checksum over lengths 0..2048.
// The name of the implementation goes to stderr, so the output is the same on every CPU.
#include <stdint.h>
#include <stdio.h>

#include "pikaseko256.h"

static void Hex(const uint8_t* d, int n) {
  for (int i = 0; i < n; i++) printf("%02x", d[i]);
  printf("\n");
}
static void Hex64(uint64_t v) {
  uint8_t d[8];
  for (int i = 0; i < 8; i++) d[i] = (uint8_t)(v >> (8 * i));
  Hex(d, 8);
}

int main(void) {
  static uint8_t buf[4200], key[100];
  for (int i = 0; i < 4200; i++) buf[i] = (uint8_t)i;
  for (int i = 0; i < 100; i++) key[i] = (uint8_t)i;
  const size_t lens[] = {0, 1, 55, 56, 63, 64, 255, 256, 1023, 1024, 1025, 4096};
  const size_t klens[] = {0, 32, 64, 100};
  uint8_t d[32];
  fprintf(stderr, "vectors: %s implementation\n", pikaseko256_implementation());
  printf("PikasekoHash-256 version 1 test vectors (data byte i = i mod 256, key byte i = i; digests as bytes)\n");
  for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
    const size_t n = lens[i];
    pikaseko256_unkeyed(buf, n, d);
    printf("unkeyed256 len %5zu  ", n); Hex(d, 32);
    printf("unkeyed64  len %5zu  ", n); Hex64(pikaseko64_unkeyed(buf, n));
    for (size_t k = 0; k < sizeof klens / sizeof klens[0]; k++) {
      pikaseko256_keyed(buf, n, key, klens[k], d);
      printf("keyed256 klen %3zu len %5zu  ", klens[k], n); Hex(d, 32);
    }
    printf("keyed64  klen %3d len %5zu  ", 32, n); Hex64(pikaseko64_keyed(buf, n, key, 32));
  }
  uint64_t acc = 0;
  for (size_t n = 0; n <= 2048; n++) {
    pikaseko256_unkeyed(buf, n, d);
    for (int w = 0; w < 4; w++) {
      uint64_t v = 0;
      for (int j = 7; j >= 0; j--) v = (v << 8) | d[8 * w + j];
      acc = acc * 0x9E3779B97F4A7C15ull ^ v;
    }
  }
  printf("unkeyed256, all lengths 0..2048 folded: %016llx\n", (unsigned long long)acc);
  return 0;
}
