// Diffusion of the step: flip one bit of a random state and count, after 1..N steps,
// how many of the 1024 state bytes differ (mean and worst over the 8192 bit positions,
// a few random states each, all four step phases). Random ceiling: 1024 * 255/256 = 1020.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pikaseko256.h"

static uint64_t rng = 0x243F6A8885A308D3ull;
static uint64_t Next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(int argc, char** argv) {
  const int maxs = argc > 1 ? atoi(argv[1]) : 6;
  printf("steps | bytes reached of 1024: mean   worst\n");
  for (int s = 1; s <= maxs; s++) {
    double sum = 0; int worst = 1024; long cnt = 0;
    for (int bit = 0; bit < 8192; bit++) {
      uint64_t a[128], b[128];
      for (int i = 0; i < 128; i++) a[i] = b[i] = Next();
      b[bit >> 6] ^= 1ull << (bit & 63);
      const unsigned t0 = (unsigned)(bit & 3);
      pikaseko256_steps(a, t0, (unsigned)s);
      pikaseko256_steps(b, t0, (unsigned)s);
      const uint8_t* x = (const uint8_t*)a; const uint8_t* y = (const uint8_t*)b;
      int d = 0;
      for (int i = 0; i < 1024; i++) d += x[i] != y[i];
      sum += d; cnt++;
      if (d < worst) worst = d;
    }
    printf("%5d | %22.1f %7d\n", s, sum / cnt, worst);
  }
  return 0;
}
