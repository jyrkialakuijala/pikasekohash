// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// libFuzzer target: every implementation the build and the CPU have, one-shot and
// streaming with fuzzer-chosen piece lengths, must agree with the portable one-shot hash.
// Input: byte 0 flags (bit 0: keyed), then 64 key bytes if keyed, then 1 byte that seeds
// the piece lengths, then the message. Build: make fuzz (clang), run: ./build/fuzz -max_total_time=60
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_impl.h"

typedef void HashFn(const void*, size_t, const uint64_t*, uint64_t*);
typedef void InitFn(pikaseko256_ctx*, const uint64_t*);
typedef void UpdateFn(pikaseko256_ctx*, const void*, size_t);
typedef void FinalFn(const pikaseko256_ctx*, uint64_t*);
typedef struct { int level; HashFn* hash; InitFn* init; UpdateFn* update; FinalFn* final; } Impl;

static const Impl kImpls[] = {
    {0, pikaseko256_portable, pikaseko256_init_portable, pikaseko256_update_portable, pikaseko256_final_portable},
    {0, pikaseko256, pikaseko256_init, pikaseko256_update, pikaseko256_final},
#if defined(PIKASEKO256_HAVE_X86)
    {3, pikaseko256_avx512, pikaseko256_init_avx512, pikaseko256_update_avx512, pikaseko256_final_avx512},
    {2, pikaseko256_avx2, pikaseko256_init_avx2, pikaseko256_update_avx2, pikaseko256_final_avx2},
    {1, pikaseko256_aesni, pikaseko256_init_aesni, pikaseko256_update_aesni, pikaseko256_final_aesni},
#elif defined(PIKASEKO256_HAVE_NEON)
    {1, pikaseko256_neon, pikaseko256_init_neon, pikaseko256_update_neon, pikaseko256_final_neon},
#endif
};

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) return 0;
  const int keyed = data[0] & 1;
  data++; size--;
  uint64_t key[8];
  if (keyed) {
    if (size < 65) return 0;
    pikaseko256_key_from_bytes(key, data);
    data += 64; size -= 64;
  }
  uint32_t seed = data[0] * 2654435761u + 1;
  data++; size--;
  // The message is copied to its own allocation so that reads past its end are caught.
  uint8_t* msg = (uint8_t*)malloc(size ? size : 1);
  if (!msg) return 0;
  if (size) memcpy(msg, data, size);
  const uint64_t* kp = keyed ? key : NULL;
  uint64_t want[4];
  pikaseko256_portable(msg, size, kp, want);
  const int level = pikaseko256_cpu_level();
  for (size_t m = 0; m < sizeof kImpls / sizeof kImpls[0]; m++) {
    const Impl* im = &kImpls[m];
    if (im->level > level) continue;
    uint64_t got[4];
    im->hash(msg, size, kp, got);
    if (memcmp(want, got, 32)) abort();
    pikaseko256_ctx ctx;
    im->init(&ctx, kp);
    for (size_t pos = 0; pos < size;) {
      seed = seed * 1103515245u + 12345u;
      size_t piece = (seed >> 16) % 600;
      if (piece > size - pos) piece = size - pos;
      im->update(&ctx, msg + pos, piece);
      pos += piece;
    }
    im->final(&ctx, got);
    if (memcmp(want, got, 32)) abort();
  }
  free(msg);
  return 0;
}
