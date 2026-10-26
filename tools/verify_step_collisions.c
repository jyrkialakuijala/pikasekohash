// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Checks the explicit one-step collisions in analysis/data/: two different states X, X'
// that give the same output after one step at step index t (see ANALYSIS.md).
// Usage: verify_step_collisions FILE...   (each file: "# t = N" line, then X, Xp and Y,
// 128 hex words each, register-major: word 8i + l is lane l of r_i).
// Uses pikaseko256_steps when the CPU has AVX-512 + VAES, else pikaseko256_steps_portable.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "pikaseko256.h"

static int ReadWords(char* line, uint64_t* w) {
  char* p = strchr(line, ' ');
  for (int i = 0; i < 128; i++) {
    if (!p) return 0;
    char* e; w[i] = strtoull(p, &e, 16);
    if (e == p) return 0;
    p = e;
  }
  return 1;
}

int main(int argc, char** argv) {
  int fails = 0;
  void (*steps)(uint64_t*, unsigned, unsigned) = pikaseko256_steps_portable;
#ifndef PIKASEKO256_NO_AVX512
  if (PikasekoHaveAvx512()) steps = pikaseko256_steps;
#endif
  printf("step function: %s\n", steps == pikaseko256_steps_portable ? "portable" : "AVX-512");
  for (int f = 1; f < argc; f++) {
    FILE* fp = fopen(argv[f], "r");
    if (!fp) { printf("%s: cannot open\n", argv[f]); fails++; continue; }
    static char line[16384];
    uint64_t X[128], Xp[128], Y[128];
    int t = -1, got = 0;
    while (fgets(line, sizeof line, fp)) {
      if (!strncmp(line, "# t = ", 6)) t = atoi(line + 6);
      else if (!strncmp(line, "Xp ", 3)) got |= ReadWords(line, Xp) << 1;
      else if (!strncmp(line, "X ", 2)) got |= ReadWords(line, X);
      else if (!strncmp(line, "Y ", 2)) got |= ReadWords(line, Y) << 2;
    }
    fclose(fp);
    if (t < 0 || got != 7) { printf("%s: bad format\n", argv[f]); fails++; continue; }
    int bytes = 0;
    for (int i = 0; i < 128; i++) for (int b = 0; b < 8; b++) bytes += ((X[i] ^ Xp[i]) >> (8 * b) & 0xff) != 0;
    steps(X, (unsigned)t, 1);
    steps(Xp, (unsigned)t, 1);
    const int coll = !memcmp(X, Xp, sizeof X), match = !memcmp(X, Y, sizeof X);
    printf("%s: t = %d, inputs differ in %d bytes, outputs equal: %s, output as recorded: %s\n",
           argv[f], t, bytes, coll ? "yes" : "NO", match ? "yes" : "NO");
    fails += !(coll && match && bytes > 0);
  }
  return fails != 0;
}
