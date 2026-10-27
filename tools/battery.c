// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// Classic hash test battery for pikaseko256 (SMHasher-style, reduced sizes).
// Each test prints one line; the last line is "VERDICT PASS" or "VERDICT FAIL <tests>".
// Collision tests compare the number of duplicate values among n hashes, for
// each of the four 64-bit output words (expect 0) and for the low, middle and
// high 32 bits of each word (expect n - 2^32 (1 - exp(-n / 2^32)),
// z = (seen - expect) / sqrt(expect)). Avalanche, BIC and the differential
// test cover all 256 output bits. Usage: battery [--quick]
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pikaseko256.h"

#define OB 256  // output bits
typedef struct { uint64_t w[4]; } H256;
static inline H256 H(const void* p, size_t n, const uint64_t* key) { H256 h; pikaseko256(p, n, key, h.w); return h; }
static inline H256 Xor(H256 a, H256 b) { H256 r; for (int i = 0; i < 4; i++) r.w[i] = a.w[i] ^ b.w[i]; return r; }
static inline int Bit(H256 h, int o) { return (h.w[o >> 6] >> (o & 63)) & 1; }
static inline int IsZero(H256 h) { return (h.w[0] | h.w[1] | h.w[2] | h.w[3]) == 0; }

static inline uint64_t SplitMix64(uint64_t* s) { uint64_t z = (*s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
static uint64_t g_rng = 0x5eed5eed;
static const uint64_t kKey[8] = {0x243f6a8885a308d3ull, 0x13198a2e03707344ull, 0xa4093822299f31d0ull, 0x082efa98ec4e6c89ull, 0x452821e638d01377ull, 0xbe5466cf34e90c6cull, 0xc0ac29b7c97c50ddull, 0x3f84d5b5b5470917ull};
static int g_fail = 0; static char g_failed[512];
static void Fail(const char* name) { g_fail++; if (strlen(g_failed) + strlen(name) + 2 < sizeof(g_failed)) { strcat(g_failed, " "); strcat(g_failed, name); } }
static double worst_z = 0;
static void NoteZ(double z) { if (fabs(z) > fabs(worst_z)) worst_z = z; }

static int Cmp64(const void* a, const void* b) { uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b; return x < y ? -1 : x > y; }
static int Cmp32(const void* a, const void* b) { uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b; return x < y ? -1 : x > y; }
static size_t Dups64(uint64_t* h, size_t n) { qsort(h, n, 8, Cmp64); size_t d = 0; for (size_t i = 1; i < n; i++) d += h[i] == h[i - 1]; return d; }
static size_t Dups32(const uint64_t* h, size_t n, int shift, uint32_t* tmp) {
  for (size_t i = 0; i < n; i++) tmp[i] = (uint32_t)(h[i] >> shift);
  qsort(tmp, n, 4, Cmp32); size_t d = 0; for (size_t i = 1; i < n; i++) d += tmp[i] == tmp[i - 1]; return d;
}
static double Expect32(size_t n) { double m = 4294967296.0; return (double)n - m * (1 - exp(-(double)n / m)); }
// Poisson upper tail P(X >= k) for small expectations.
static double PoissonTail(double ex, size_t k) {
  double p = exp(-ex), cdf = 0; for (size_t i = 0; i < k; i++) { cdf += p; p *= ex / (i + 1); } return cdf > 1 ? 0 : 1 - cdf;
}
// Collision test over n 256-bit hashes: every word's 64-bit dups must be 0; every 32-bit slice
// within 4 sd when the expectation is large, or with a Poisson tail above 1e-5 when it is small.
static void Collisions(const char* name, const H256* h, size_t n) {
  uint64_t* w64 = malloc(n * 8); uint32_t* tmp = malloc(n * 4);
  const double ex = Expect32(n), sd = sqrt(ex);
  size_t d64 = 0; double worst = 0; int ww = 0, ws = 0, bad = 0; double minp = 1;
  for (int w = 0; w < 4; w++) {
    for (size_t i = 0; i < n; i++) w64[i] = h[i].w[w];
    for (int s = 0; s < 3; s++) {
      const size_t seen = Dups32(w64, n, 16 * s, tmp); const double z = (seen - ex) / sd;
      if (ex >= 30) { NoteZ(z); if (fabs(z) > fabs(worst)) { worst = z; ww = w; ws = 16 * s; } if (fabs(z) > 4) bad = 1; }
      else { const double p = PoissonTail(ex, seen); if (p < minp) { minp = p; ww = w; ws = 16 * s; worst = (double)seen; } if (p < 1e-5) bad = 1; }
    }
    d64 += Dups64(w64, n);
  }
  if (ex >= 30) printf("%-11s n=%-9zu dups64(all words)=%-3zu worst 32-bit z %+5.1f (word %d bits %d..) expect %.0f +- %.0f%s\n", name, n, d64, worst, ww, ws, ex, sd, (d64 || bad) ? " <-- FAIL" : "");
  else printf("%-11s n=%-9zu dups64(all words)=%-3zu worst 32-bit slice: %.0f dups (word %d bits %d..) expect %.3f, tail p %.2g%s\n", name, n, d64, worst, ww, ws, ex, minp, (d64 || bad) ? " <-- FAIL" : "");
  if (d64 || bad) Fail(name);
  free(w64); free(tmp);
}

// 1. Avalanche over 256 output bits: worst |p - 0.5| vs the ideal max.
static int Avalanche(size_t len, int trials, int key_bits, double* worst_ratio) {
  const size_t nin = key_bits ? 512 : len * 8;
  uint32_t* counts = calloc(nin * OB, 4);
  uint8_t* buf = malloc(len ? len : 1); uint64_t key[8];
  for (int t = 0; t < trials; ++t) {
    for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)SplitMix64(&g_rng);
    for (int i = 0; i < 8; ++i) key[i] = SplitMix64(&g_rng);
    const H256 h = H(buf, len, key);
    for (size_t b = 0; b < nin; ++b) {
      H256 h2;
      if (key_bits) { key[b / 64] ^= 1ull << (b % 64); h2 = H(buf, len, key); key[b / 64] ^= 1ull << (b % 64); }
      else { buf[b / 8] ^= (uint8_t)(1u << (b % 8)); h2 = H(buf, len, key); buf[b / 8] ^= (uint8_t)(1u << (b % 8)); }
      const H256 d = Xor(h, h2); uint32_t* row = counts + b * OB;
      for (int o = 0; o < OB; ++o) row[o] += Bit(d, o);
    }
  }
  double worst = 0;
  for (size_t i = 0; i < nin * OB; ++i) { double bias = fabs((double)counts[i] / trials - 0.5); if (bias > worst) worst = bias; }
  const double expect = 0.5 / sqrt((double)trials) * sqrt(2.0 * log((double)nin * OB));
  free(counts); free(buf);
  const double ratio = worst / expect; if (ratio > *worst_ratio) *worst_ratio = ratio;
  return ratio > 1.5;
}

// 2. Bit independence over 256 output bits: for each input bit, max |corr| between pairs of output-bit flips.
static void BIC(size_t len, int trials) {
  const size_t nin = len * 8; const int words = (trials + 63) / 64;
  uint64_t* cols = calloc((size_t)OB * words, 8);
  uint8_t* buf = malloc(len); uint64_t key[8];
  double worst = 0; size_t wi = 0; int wj = 0, wk = 0;
  for (size_t b = 0; b < nin; ++b) {
    memset(cols, 0, (size_t)OB * words * 8);
    for (int t = 0; t < trials; ++t) {
      for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)SplitMix64(&g_rng);
      for (int i = 0; i < 8; ++i) key[i] = SplitMix64(&g_rng);
      const H256 h = H(buf, len, key);
      buf[b / 8] ^= (uint8_t)(1u << (b % 8)); const H256 d = Xor(h, H(buf, len, key)); buf[b / 8] ^= (uint8_t)(1u << (b % 8));
      for (int o = 0; o < OB; ++o) if (Bit(d, o)) cols[(size_t)o * words + t / 64] |= 1ull << (t % 64);
    }
    for (int j = 0; j < OB; ++j) for (int k = j + 1; k < OB; ++k) {
      int c = 0; for (int w = 0; w < words; ++w) c += __builtin_popcountll(cols[(size_t)j * words + w] ^ cols[(size_t)k * words + w]);
      const double corr = fabs(2.0 * c / trials - 1.0);
      if (corr > worst) { worst = corr; wi = b; wj = j; wk = k; }
    }
  }
  const double expect = sqrt(2.0 * log((double)nin * (OB * (OB - 1) / 2))) / sqrt((double)trials);
  printf("bic        len=%-4zu trials=%d max|corr|=%.4f (in %zu, out %d,%d) ideal max ~%.4f%s\n", len, trials, worst, wi, wj, wk, expect, worst > 1.5 * expect ? " <-- FAIL" : "");
  if (worst > 1.5 * expect) Fail("bic");
  free(cols); free(buf);
}

// 3. Differential: low-weight input differences; output collisions and the bias of the output difference.
static void Differential(size_t len, int ndiff, int trials) {
  const size_t nin = len * 8;
  uint8_t* x = malloc(len); uint8_t* y = malloc(len); uint64_t key[8];
  size_t coll = 0; double worst = 0; int ncfg = 0;
  uint32_t cnt[OB];
  for (int d = 0; d < ndiff; ++d) {
    int w = d < (int)nin ? 1 : 2 + (d & 1); size_t bits[3];
    if (w == 1) bits[0] = d; else for (int i = 0; i < w; ++i) {  // distinct bits
      int again; do { bits[i] = SplitMix64(&g_rng) % nin; again = 0; for (int j = 0; j < i; ++j) again |= bits[j] == bits[i]; } while (again); }
    memset(cnt, 0, sizeof(cnt));
    for (int t = 0; t < trials; ++t) {
      for (size_t i = 0; i < len; ++i) x[i] = (uint8_t)SplitMix64(&g_rng);
      for (int i = 0; i < 8; ++i) key[i] = SplitMix64(&g_rng);
      memcpy(y, x, len); for (int i = 0; i < w; ++i) y[bits[i] / 8] ^= (uint8_t)(1u << (bits[i] % 8));
      const H256 dh = Xor(H(x, len, key), H(y, len, key));
      if (IsZero(dh)) coll++;
      for (int o = 0; o < OB; ++o) cnt[o] += Bit(dh, o);
    }
    for (int o = 0; o < OB; ++o) { double b = fabs((double)cnt[o] / trials - 0.5); if (b > worst) worst = b; }
    ncfg++;
  }
  const double expect = 0.5 / sqrt((double)trials) * sqrt(2.0 * log((double)ncfg * OB));
  printf("diff       len=%-4zu diffs=%d trials=%d collisions=%zu max|bias|=%.4f ideal max ~%.4f%s\n", len, ndiff, trials, coll, worst, expect, (coll || worst > 1.5 * expect) ? " <-- FAIL" : "");
  if (coll || worst > 1.5 * expect) Fail("diff");
  free(x); free(y);
}

// 4. Sparse keys: all inputs of `len` bytes with at most `maxw` bits set.
static void Sparse(size_t len, int maxw) {
  const size_t nb = len * 8;
  size_t n = 1 + nb; if (maxw >= 2) n += nb * (nb - 1) / 2; if (maxw >= 3) n += nb * (nb - 1) * (nb - 2) / 6;
  H256* h = malloc(n * sizeof(H256)); uint8_t* buf = calloc(len, 1); size_t k = 0;
  h[k++] = H(buf, len, kKey);
  for (size_t i = 0; i < nb; ++i) {
    buf[i / 8] ^= 1u << (i % 8); h[k++] = H(buf, len, kKey);
    if (maxw >= 2) for (size_t j = i + 1; j < nb; ++j) {
      buf[j / 8] ^= 1u << (j % 8); h[k++] = H(buf, len, kKey);
      if (maxw >= 3) for (size_t l = j + 1; l < nb; ++l) { buf[l / 8] ^= 1u << (l % 8); h[k++] = H(buf, len, kKey); buf[l / 8] ^= 1u << (l % 8); }
      buf[j / 8] ^= 1u << (j % 8);
    }
    buf[i / 8] ^= 1u << (i % 8);
  }
  char name[32]; snprintf(name, sizeof(name), "sparse%zu/%d", len, maxw);
  Collisions(name, h, k); free(h); free(buf);
}

// 5. Cyclic: a block repeated (distinct blocks: a counter for small blocks, random for large).
static void Cyclic(size_t block, int reps, size_t n) {
  H256* h = malloc(n * sizeof(H256)); uint8_t* buf = malloc(block * reps);
  for (size_t i = 0; i < n; ++i) {
    uint64_t v = block <= 4 ? (i * 0x9E3779B1ull) & ((1ull << (8 * block)) - 1) : SplitMix64(&g_rng);
    for (size_t b = 0; b < block; ++b) buf[b] = (uint8_t)(v >> (8 * b));
    for (int r = 1; r < reps; ++r) memcpy(buf + r * block, buf, block);
    h[i] = H(buf, block * reps, kKey);
  }
  char name[32]; snprintf(name, sizeof(name), "cyclic%zux%d", block, reps);
  Collisions(name, h, n); free(h); free(buf);
}

// 6. Two bytes: all keys of `len` bytes with at most two nonzero bytes.
static void TwoBytes(size_t len) {
  const size_t n = 1 + len * 255 + len * (len - 1) / 2 * 255 * 255;
  H256* h = malloc(n * sizeof(H256)); uint8_t* buf = calloc(len, 1); size_t k = 0;
  h[k++] = H(buf, len, kKey);
  for (size_t i = 0; i < len; ++i) for (int a = 1; a < 256; ++a) {
    buf[i] = (uint8_t)a; h[k++] = H(buf, len, kKey);
    for (size_t j = i + 1; j < len; ++j) for (int b = 1; b < 256; ++b) { buf[j] = (uint8_t)b; h[k++] = H(buf, len, kKey); buf[j] = 0; }
    buf[i] = 0;
  }
  char name[32]; snprintf(name, sizeof(name), "twobytes%zu", len);
  Collisions(name, h, k); free(h); free(buf);
}

// 7. Window: a 20-bit counter at every 8th bit position of a zero input; 32-bit collisions per window, words 0 and 3.
static void Window(size_t len, int bits) {
  const size_t n = (size_t)1 << bits; uint64_t* h = malloc(n * 8); uint64_t* h3 = malloc(n * 8); uint32_t* tmp = malloc(n * 4);
  uint8_t* buf = calloc(len + 8, 1); double worst = 0; int wpos = 0, wshift = 0; const double ex = Expect32(n), sd = sqrt(ex);
  for (int pos = 0; pos + bits <= (int)len * 8; pos += 8) {
    for (size_t v = 0; v < n; ++v) {
      memset(buf, 0, len); uint64_t w; memcpy(&w, buf + pos / 8, 8); w |= v << (pos % 8); memcpy(buf + pos / 8, &w, 8);
      const H256 r = H(buf, len, kKey); h[v] = r.w[0]; h3[v] = r.w[3];
    }
    for (int s = 0; s < 3; s++) {
      double z = (Dups32(h, n, 16 * s, tmp) - ex) / sd; if (fabs(z) > fabs(worst)) { worst = z; wpos = pos; wshift = 16 * s; }
      z = (Dups32(h3, n, 16 * s, tmp) - ex) / sd; if (fabs(z) > fabs(worst)) { worst = z; wpos = pos; wshift = 192 + 16 * s; }
    }
  }
  printf("window     len=%-4zu bits=%d worst z %+5.1f (pos %d, out bits %d..) expect %.0f +- %.0f%s\n", len, bits, worst, wpos, wshift, ex, sd, fabs(worst) > 4.5 ? " <-- FAIL" : "");
  NoteZ(worst); if (fabs(worst) > 4.5) Fail("window");
  free(h); free(h3); free(tmp); free(buf);
}

// 8. Permutation: 8 words from a small set of structured values, all combinations.
static void Permutation(void) {
  static const uint32_t vals[7] = {0, 1, 2, 3, 0x80000000u, 0xFFFFFFFFu, 0x7FFFFFFFu};
  const size_t n = 7 * 7 * 7 * 7 * 7 * 7 * 7 * 7; H256* h = malloc(n * sizeof(H256)); uint32_t buf[8];
  for (size_t i = 0; i < n; ++i) { size_t x = i; for (int w = 0; w < 8; ++w) { buf[w] = vals[x % 7]; x /= 7; } h[i] = H(buf, 32, kKey); }
  Collisions("perm", h, n); free(h);
}

// 9. Seeds: one input under many keys (counter in one lane, and all keys with <= 2 bits set).
static void Seeds(size_t n) {
  H256* h = malloc(n * sizeof(H256)); uint64_t key[8]; memcpy(key, kKey, 64);
  const char msg[] = "pikasekohash seed test input";
  for (size_t i = 0; i < n; ++i) { key[i % 8] = kKey[i % 8] ^ (i / 8 + 1); h[i] = H(msg, sizeof(msg) - 1, key); key[i % 8] = kKey[i % 8]; }
  Collisions("seedctr", h, n);
  memset(key, 0, 64); size_t k = 0;
  h[k++] = H(msg, sizeof(msg) - 1, key);
  for (int i = 0; i < 512; ++i) { key[i / 64] ^= 1ull << (i % 64); h[k++] = H(msg, sizeof(msg) - 1, key);
    for (int j = i + 1; j < 512; ++j) { key[j / 64] ^= 1ull << (j % 64); h[k++] = H(msg, sizeof(msg) - 1, key); key[j / 64] ^= 1ull << (j % 64); }
    key[i / 64] ^= 1ull << (i % 64); }
  Collisions("seedsparse", h, k);
  free(h);
}

// 10. Lengths and extension: zero inputs of every length, and x vs x||0, 0||x, x with the last byte dropped.
static void Lengths(void) {
  const size_t maxlen = 4096; H256* h = malloc((maxlen + 1) * sizeof(H256)); uint8_t* z = calloc(maxlen + 1, 1);
  for (size_t l = 0; l <= maxlen; ++l) h[l] = H(z, l, kKey);
  uint64_t* w0 = malloc((maxlen + 1) * 8); for (size_t l = 0; l <= maxlen; ++l) w0[l] = h[l].w[0];
  const size_t d = Dups64(w0, maxlen + 1);
  uint8_t x[520]; int bad = 0;
  for (int t = 0; t < 2000; ++t) {
    const size_t len = 1 + SplitMix64(&g_rng) % 300;
    for (size_t i = 0; i < len + 1; ++i) x[i] = (uint8_t)SplitMix64(&g_rng);
    const H256 a = H(x, len, kKey); H256 b;
    x[len] = 0; b = H(x, len + 1, kKey); if (!memcmp(&a, &b, 32)) bad++;
    if (len > 1) { b = H(x, len - 1, kKey); if (!memcmp(&a, &b, 32)) bad++; }
    memmove(x + 1, x, len); x[0] = 0; b = H(x, len + 1, kKey); if (!memcmp(&a, &b, 32)) bad++;
    memmove(x, x + 1, len);
  }
  printf("lengths    zero inputs 0..%zu: %zu dups (word 0); extension collisions %d%s\n", maxlen, d, bad, (d || bad) ? " <-- FAIL" : "");
  if (d || bad) Fail("lengths");
  free(h); free(z); free(w0);
}

int main(int argc, char** argv) {
  const int quick = argc > 1 && !strcmp(argv[1], "--quick");
  for (int i = 1; i < argc; i++) if (!strncmp(argv[i], "--seed=", 7)) { g_rng = (uint64_t)strtoull(argv[i] + 7, NULL, 10) * 0x9E3779B97F4A7C15ull + 1; printf("seed %s\n", argv[i] + 7); }
  const clock_t t0 = clock();
  g_failed[0] = 0;
  { double wr = 0; int susp = 0; const size_t lens[] = {3, 8, 16, 31, 64, 100, 128, 256};
    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); ++i) susp += Avalanche(lens[i], quick ? 500 : 1000, 0, &wr);
    susp += Avalanche(64, quick ? 500 : 1000, 1, &wr);
    printf("avalanche  9 configs x 256 bits: %d suspicious, worst max|bias| / ideal = %.2f%s\n", susp, wr, susp ? " <-- FAIL" : "");
    if (susp) Fail("avalanche"); }
  BIC(8, quick ? 2000 : 4000); BIC(64, quick ? 1000 : 2000);
  Differential(8, 64 + 400, 500); Differential(32, 256 + 800, 400); Differential(128, 1024 + 1000, 200);
  Sparse(32, 3); Sparse(128, 2); Sparse(1024, 1);
  Cyclic(8, 8, 1u << 21); Cyclic(16, 4, 1u << 21); Cyclic(3, 20, 1u << 21);
  TwoBytes(8); TwoBytes(16); TwoBytes(24);
  Window(32, 20); Window(72, 20);
  Permutation();
  Seeds(1u << 22);
  Lengths();
  printf("worst 32-bit collision z %+.1f; %.0f s\n", worst_z, (double)(clock() - t0) / CLOCKS_PER_SEC);
  printf("VERDICT %s%s\n", g_fail ? "FAIL" : "PASS", g_failed);
  return g_fail != 0;
}
