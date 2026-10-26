// Constants of PikasekoHash-256 (SPEC.md, "Constants").
#ifndef PIKASEKO256_CONSTS_H_
#define PIKASEKO256_CONSTS_H_
#include <stdint.h>

// First 32 fractional bits of pi, e and the golden ratio: the IV factors.
#define PIKASEKO_PI  0x243F6A88u
#define PIKASEKO_E   0xB7E15162u
#define PIKASEKO_PHI 0x9E3779B9u

// Step constants C[n], xored into the cross key of the group that holds r0 at steps
// with t mod 4 == n: hex digits 1537..2048 of pi (words 96..127 of its expansion).
static const uint64_t kPikasekoStepConst[4][8] = {
  {0x62fb1341cee4c6e8ull, 0xef20cada36774c01ull, 0xd07e9efe2bf11fb4ull, 0x95dbda4dae909198ull,
   0xeaad8e716b93d5a0ull, 0xd08ed1d0afc725e0ull, 0x8e3c5b2f8e7594b7ull, 0x8ff6e2fbf2122b64ull},
  {0x8888b812900df01cull, 0x4fad5ea0688fc31cull, 0xd1cff191b3a8c1adull, 0x2f2f2218be0e1777ull,
   0xea752dfe8b021fa1ull, 0xe5a0cc0fb56f74e8ull, 0x18acf3d6ce89e299ull, 0xb4a84fe0fd13e0b7ull},
  {0x7cc43b81d2ada8d9ull, 0x165fa26680957705ull, 0x93cc7314211a1477ull, 0xe6ad206577b5fa86ull,
   0xc75442f5fb9d35cfull, 0xebcdaf0c7b3e89a0ull, 0xd6411bd3ae1e7e49ull, 0x00250e2d2071b35eull},
  {0x226800bb57b8e0afull, 0x2464369bf009b91eull, 0x5563911d59dfa6aaull, 0x78c14389d95a537full,
   0x207d5ba202e5b9c5ull, 0x832603766295cfa9ull, 0x11c819684e734a41ull, 0xb3472dca7b14a94aull},
};

#define PIKASEKO_FINAL_STEPS 6

#endif  // PIKASEKO256_CONSTS_H_
