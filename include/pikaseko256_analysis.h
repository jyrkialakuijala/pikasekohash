// Copyright (c) 2026 Jyrki Alakuijala
// SPDX-License-Identifier: MIT
// PikasekoHash-256: entry points for analysis and for checking an implementation of the
// step (SPEC.md, section 5) in isolation. Not needed for hashing; see pikaseko256.h.
#ifndef PIKASEKO256_ANALYSIS_H_
#define PIKASEKO256_ANALYSIS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Run nsteps steps, starting at step index t0, on a state of 16 registers x 8 lanes
// (state[8 * i + l] = lane l of register r_i).
void pikaseko256_steps(uint64_t state[128], unsigned t0, unsigned nsteps);  // AVX-512 + VAES
void pikaseko256_steps_portable(uint64_t state[128], unsigned t0, unsigned nsteps);

#ifdef __cplusplus
}
#endif

#endif  // PIKASEKO256_ANALYSIS_H_
