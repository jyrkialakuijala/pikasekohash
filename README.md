# PikasekoHash-256

PikasekoHash-256 is an experimental 256-bit hash function built from AES
rounds over a wide state of sixteen 512-bit registers (8192 bits). It is
designed for CPUs with AVX-512 and VAES (AMD Zen 4 and Zen 5, Intel Ice Lake
and later); on one Zen 5 core it hashes about 24 GB/s and a short message
in about 36 ns. It can be used keyed (512-bit key) or unkeyed.
A portable C implementation gives bit-identical results on any platform.
This repository holds the specification, both implementations, test
vectors, and the security analysis made so far.

Author: Jyrki Alakuijala (co-author of HighwayHash).

[![build](https://github.com/jyrkialakuijala/pikasekohash/actions/workflows/ci.yml/badge.svg)](https://github.com/jyrkialakuijala/pikasekohash/actions)

## Status: experimental, not for production cryptographic use

This is version 0 of a research design. Do not use it where a vetted
cryptographic hash or MAC is required (use SHA-256, SHA-3, BLAKE3 or
similar). Known facts:

* **Free-start collisions are practical.** The step function is not a
  permutation, and collisions of a single step can be constructed in
  constant time from a chosen state. So free-start collisions of the
  compression function and of the finalization are practical (the two
  colliding chaining values are not reachable from the fixed initial value).
  Five explicit one-step collision pairs are in
  [analysis/data/](analysis/data/) and are checked by `make test`.
* No collision, second preimage or preimage of the hash itself is known, and
  no distinguisher of the full hash. Whether a semi-free-start collision
  (same chaining value, different packets) is practical is open; no security
  claim should be made before that is settled.
* Proven bounds exist only in byte-level truncated models, and they count
  probability only for the steps an attacker cannot steer.
* There is no domain separation: the all-zero key gives the public unkeyed
  hash.
* The design may still change (section 13 of [ANALYSIS.md](ANALYSIS.md)).
  Test vectors will then change too.

See [ANALYSIS.md](ANALYSIS.md) for the full security and quality analysis and
[analysis/reviews/](analysis/reviews/) for the review reports. Cryptanalysis
is welcome; see [SECURITY.md](SECURITY.md).

## Requirements

* A C11 compiler (gcc or clang) and `make`.
* For the fast implementation: a compiler that supports
  `-mavx512f -mavx512bw -mvaes`, and a CPU with AVX-512F, AVX-512BW and
  VAES to run it. There is no run-time CPU dispatch; the caller checks the
  CPU.
* The portable implementation is plain C and runs on any CPU and byte order.

## Build and test

    make            # library, tests, benchmark and tools in build/
    make test       # test vectors; AVX-512 against portable for lengths 0..2048;
                    # the one-step collision pairs
    make bench      # speed, unkeyed and keyed (AVX-512 builds)
    make bench-portable
    make battery    # 256-bit statistical battery (single-threaded, a few minutes)
    make clean

The library is `build/libpikaseko256.a`; the header is
`include/pikaseko256.h`.

Build options:

* `make PORTABLE=1` builds only the portable code (the default when the
  compiler does not target x86-64). That library has no `pikaseko256`,
  `pikaseko256_unkeyed`, `pikaseko64` or `pikaseko256_steps`; use the
  `_portable` names. Programs are built with `-DPIKASEKO256_NO_AVX512`.
* `make test` checks the CPU at run time. Without AVX-512F, AVX-512BW, VAES
  and OS support for the zmm state it runs only the portable tests, says
  that the AVX-512 tests were skipped, and still succeeds.
  `PIKASEKO256_FORCE_PORTABLE=1 make test` forces that path.
* Cross builds take `CC`, `AR`, `LDFLAGS` and `RUN` (an emulator prefix), for
  example a big-endian test:
  `make PORTABLE=1 CC=s390x-linux-gnu-gcc AR=s390x-linux-gnu-ar LDFLAGS=-static RUN=qemu-s390x test`.
* CMake: `cmake -B build/cmake && cmake --build build/cmake && ctest --test-dir build/cmake`
  (CMake 3.20 or newer for `--test-dir`), with options
  `-DPIKASEKO256_AVX512=ON|OFF` and `-DPIKASEKO256_BUILD_TESTS=OFF`.
* Library users check the CPU before calling the AVX-512 functions;
  `test/cpu.h` shows how.

Other tools:

* `tools/diffusion.c`: bytes of the state reached by a one-bit difference
  after each step (output in `analysis/results/diffusion.txt`).
* `tools/battery.c`: the statistical battery (output in
  `analysis/results/battery.txt`).
* `tools/verify_step_collisions.c`: checks the explicit one-step collision
  pairs in `analysis/data/step_collision_t{1,3,5,7,9}.txt` (see ANALYSIS.md,
  section 4).

## API

`include/pikaseko256.h`:

```c
// 256-bit hash. key: eight 64-bit words, or NULL for the zero key.
void pikaseko256(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
// Same as pikaseko256 with the zero key.
void pikaseko256_unkeyed(const void* data, size_t len, uint64_t out[4]);
// 64-bit hash: out[0] of pikaseko256.
uint64_t pikaseko64(const void* data, size_t len, const uint64_t key[8]);

// Portable C versions, bit-identical to the above on every platform.
void pikaseko256_portable(const void* data, size_t len, const uint64_t key[8], uint64_t out[4]);
void pikaseko256_unkeyed_portable(const void* data, size_t len, uint64_t out[4]);
uint64_t pikaseko64_portable(const void* data, size_t len, const uint64_t key[8]);

// Runs nsteps steps of the core from step index t0 on a 128-word state
// (word 8i + l is lane l of register i). For analysis.
void pikaseko256_steps(uint64_t state[128], unsigned t0, unsigned nsteps);
void pikaseko256_steps_portable(uint64_t state[128], unsigned t0, unsigned nsteps);
```

Example:

```c
#include "pikaseko256.h"

uint64_t key[8];      // 512-bit secret key, filled by the caller
uint64_t digest[4];
pikaseko256(msg, msg_len, key, digest);         // AVX-512 + VAES
pikaseko256_portable(msg, msg_len, key, digest);  // any CPU, same result
```

Byte order: key word i is bytes 8i .. 8i + 7 of a 64-byte key,
little-endian. The canonical 32-byte digest is out[0], out[1], out[2], out[3],
each written little-endian.

The functions without suffix need AVX-512F, AVX-512BW and VAES; there is no
run-time CPU dispatch. The portable functions work everywhere and are
independent of byte order.

Notes for use:

* The zero key and the NULL key give the unkeyed hash. There is no domain
  separation between keyed and unkeyed use, or between the 64-bit and the
  256-bit output. Never use an all-zero key as a secret.
* The interface is one-shot; there is no streaming interface yet.
* The message length is not hidden: running time depends on it.

## Speed

AMD Ryzen 9 9950X3D (Zen 5), one core at about 5.5 GHz, data in cache,
AVX-512 implementation built with gcc 15 (`make bench`). A key adds about 2 ns.

| input | unkeyed | keyed |
|---|---:|---:|
| 0 to 128 bytes | 36 ns | 38 ns |
| 256 bytes | 45 ns | 47 ns |
| 1 KiB | 76 ns | 78 ns |
| 1 MiB | 24.3 GB/s | 24.6 GB/s |

In bulk the loop takes about 14.5 core cycles per 64 bytes; it is bound by the
two AES pipes (96 AES rounds per 256 bytes at two per cycle is 48 of its about
58 cycles). Timings vary by a few percent between runs.
With clang 21 the AVX-512 code is about 10% slower on short inputs (40 ns at
8 bytes) and 23.7 GB/s in bulk; gcc is recommended for it.

Every message costs at least 9 steps (288 AES rounds on 512-bit vectors),
which sets the latency floor for short inputs.

The portable implementation uses a bitsliced AES (constant time, no tables,
no intrinsics). On the same machine (gcc -O2, `make bench-portable`) it takes
about 6 µs per short message, 15 µs for 1 KiB and runs at about 118 MB/s in
bulk, roughly 200 times slower than the AVX-512 code. With clang 21 it is about 10% faster
(5.8 µs per short message, 129 MB/s).

## Test vectors

`test/test_vectors.txt`. Byte i of the message is i mod 256; each line gives
out[0..3] in hex. The unkeyed hash of the empty message is

    4ce9ce973810692d 933bdf92669b1d6e 277e33f96dacc156 9b8ea902b68c1524

More vectors, including keyed ones, are in section 11 of [SPEC.md](SPEC.md).

## Construction

The state is sixteen 512-bit registers, each four 128-bit AES blocks. The
initial value is computed from 32-bit words of pi, e and the golden ratio,
and the key is xored into it. A step splits the registers into four groups
of four (with a stride that cycles 1, 2, 4, 8). Each register gets two AES
rounds whose round keys are xors of other registers of its group; the
second key is rotated by one or two blocks, which carries data between
blocks. One group also takes a constant derived from pi. A one-bit
difference reaches every register after three steps and practically every
byte of the state after four. The message is cut
into 64-byte packets with Merkle-Damgard strengthening. Four packets are
added into four registers every three steps. One step later, each packet
register is multiplied (32 x 32 -> 64 bits per lane) with a neighbouring
register, and the products are xored into four other registers after the
third step. After the last packets come six more steps. The output is the
sum of all registers, passed through two AES rounds keyed by sums of other
registers and folded to 256 bits.

The full definition is in [SPEC.md](SPEC.md).

## Documents

* [SPEC.md](SPEC.md): the complete definition and test vectors.
* [ANALYSIS.md](ANALYSIS.md): security and quality analysis, open problems
  and planned changes.
* [paper/pikasekohash.md](paper/pikasekohash.md): a paper describing the
  design, its rationale, the security analysis and measurements.
* [analysis/reviews/](analysis/reviews/): five review reports on the design.
* [SECURITY.md](SECURITY.md): reporting attacks and implementation issues.
* [CONTRIBUTING.md](CONTRIBUTING.md): building, testing and changing the
  function.
* [CHANGELOG.md](CHANGELOG.md): release history.

## License

MIT; see [LICENSE](LICENSE). Copyright (c) 2026 Jyrki Alakuijala.
