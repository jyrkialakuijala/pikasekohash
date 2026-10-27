// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// libFuzzer target: every implementation the build and the CPU have, one-shot and streaming
// with fuzzer-chosen piece lengths, must agree with the portable one-shot hash, and the public
// functions with both. Input: byte 0 flags (bit 0 keyed, bit 1 64-bit digest), byte 1 the key
// length (keyed: 0..255, the key bytes follow), then 1 byte that seeds the piece lengths, then
// the message. Build: make fuzz (clang); run: ./build/fuzz -max_total_time=60
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pikaseko256.h"
#include "pikaseko256_consts.h"
#include "pikaseko256_impl.h"

typedef void HashFn(const void*, size_t, const uint64_t*, uint64_t, uint64_t*);
typedef void InitFn(pikaseko256_ctx*, const uint64_t*, uint64_t);
typedef void UpdateFn(pikaseko256_ctx*, const void*, size_t);
typedef void FinalFn(const pikaseko256_ctx*, uint64_t*);
typedef struct { int level; HashFn* hash; InitFn* init; UpdateFn* update; FinalFn* final; } Impl;
#define IMPL(level, sfx) {level, pikaseko_hash_##sfx, pikaseko_init_##sfx, pikaseko_update_##sfx, pikaseko_final_##sfx}
static const Impl kImpls[] = {
    IMPL(0, portable),
#if defined(PIKASEKO256_HAVE_X86)
    IMPL(3, avx512), IMPL(2, avx2), IMPL(1, aesni),
#elif defined(PIKASEKO256_HAVE_NEON)
    IMPL(1, neon),
#endif
};

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 3) return 0;
  const int keyed = data[0] & 1, width = data[0] & 2 ? 8 : 32;
  const size_t klen = data[1];
  data += 2; size -= 2;
  if (keyed && size < klen + 1) return 0;
  // The key and the message are copied to allocations of their own so that reads past
  // their ends are caught.
  uint8_t* key = (uint8_t*)malloc(klen ? klen : 1);
  if (!key) return 0;
  if (keyed && klen) { memcpy(key, data, klen); data += klen; size -= klen; }
  uint32_t seed = data[0] * 2654435761u + 1;
  data++; size--;
  uint8_t* msg = (uint8_t*)malloc(size ? size : 1);
  if (!msg) { free(key); return 0; }
  if (size) memcpy(msg, data, size);
  uint64_t K[8];
  const uint64_t dom = keyed ? pikaseko_key_setup(key, klen, (unsigned)width, K)
                             : width == 32 ? PIKASEKO_DOMAIN_UNKEYED256 : PIKASEKO_DOMAIN_UNKEYED64;
  const uint64_t* kp = keyed ? K : NULL;
  uint64_t want[4];
  pikaseko_hash_portable(msg, size, kp, dom, want);
  // The public functions.
  if (width == 32) {
    uint8_t d[32];
    if (keyed) pikaseko256_keyed(msg, size, key, klen, d);
    else pikaseko256_unkeyed(msg, size, d);
    for (int i = 0; i < 32; i++) if (d[i] != (uint8_t)(want[i / 8] >> (8 * (i % 8)))) abort();
  } else {
    const uint64_t h = keyed ? pikaseko64_keyed(msg, size, key, klen) : pikaseko64_unkeyed(msg, size);
    if (h != want[0]) abort();
  }
  const int level = pikaseko256_cpu_level();
  for (size_t m = 0; m < sizeof kImpls / sizeof kImpls[0]; m++) {
    const Impl* im = &kImpls[m];
    if (im->level > level) continue;
    uint64_t got[4];
    im->hash(msg, size, kp, dom, got);
    if (memcmp(want, got, 32)) abort();
    pikaseko256_ctx ctx;
    im->init(&ctx, kp, dom);
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
  free(key);
  return 0;
}
