# PikasekoHash-256

PikasekoHash-256 is a 256-bit hash function built from AES rounds over a
wide state of sixteen 512-bit registers (8192 bits). It is designed for
CPUs with AVX-512 and VAES (AMD Zen 4 and Zen 5, Intel Ice Lake and later).
It can be used keyed (512-bit key) or unkeyed. A portable C implementation
gives the same results on any platform.

Author: Jyrki Alakuijala.

## Status

Version 0, experimental. Do not use it where a vetted cryptographic hash is
required.

* No collision, second preimage or preimage of the hash is known.
* The step function is not a permutation. Collisions of a single step can be
  constructed in constant time from a chosen state. Consequently free-start
  collisions of the compression function and of the finalization are
  practical (the two colliding chaining values are not reachable from the
  fixed initial value). Whether a semi-free-start collision (same chaining
  value, different packets) is practical is open.
* Proven bounds exist only in byte-level truncated models, and they count
  probability only for the steps an attacker cannot steer.
* The design may still change (section "Planned changes" in ANALYSIS.md).
  Test vectors will then change too.

See [ANALYSIS.md](ANALYSIS.md) for the full security and quality analysis and
[analysis/reviews/](analysis/reviews/) for the review reports.

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

Every message costs at least 9 steps (288 AES rounds on 512-bit vectors),
which sets the latency floor for short inputs.

## Build and test

    make            # library objects, tests, benchmark
    make test       # test vectors; AVX-512 against portable for lengths 0..2048
    make bench      # speed
    make battery    # 256-bit statistical battery (single-threaded, a few minutes)

The AVX-512 code needs a compiler that supports `-mavx512f -mavx512bw -mvaes`
(gcc or clang). The portable code is plain C99.

Other tools:

* `tools/diffusion.c`: bytes of the state reached by a one-bit difference
  after each step.
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

## Test vectors

`test/test_vectors.txt`. Byte i of the message is i mod 256; each line gives
out[0..3] in hex. The unkeyed hash of the empty message is

    4ce9ce973810692d 933bdf92669b1d6e 277e33f96dacc156 9b8ea902b68c1524

## Construction

The state is sixteen 512-bit registers, each four 128-bit AES blocks. The
initial value is computed from 32-bit words of pi, e and the golden ratio,
and the key is xored into it. A step splits the registers into four groups
of four (with a stride that cycles 1, 2, 4, 8). Each register gets two AES
rounds whose round keys are xors of other registers of its group; the
second key is rotated by one or two blocks, which carries data between
blocks. One group also takes a constant derived from pi. A one-bit
difference reaches the whole state after three steps. The message is cut
into 64-byte packets with Merkle-Damgard strengthening. Four packets are
added into four registers every three steps. One step later, each packet
register is multiplied (32 x 32 -> 64 bits per lane) with a neighbouring
register, and the products are xored into four other registers after the
third step. After the last packets come six more steps. The output is the
sum of all registers, passed through two AES rounds keyed by sums of other
registers and folded to 256 bits.

The full definition is in [SPEC.md](SPEC.md).

## Acknowledgements

The 32 x 32 -> 64-bit multiplications of state lanes come from HighwayHash,
whose zipper merge also shaped early versions of this design.

## License

To be decided.
