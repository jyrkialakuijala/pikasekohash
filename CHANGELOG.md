# Changelog

All notable changes to this project are listed here. Any version that
changes the output of the function changes the test vectors.

## 0.0.2 (version 0) - 2026-10-27

Same function and test vectors as 0.0.1.

* Paper (paper/pikasekohash.md) describing the design and its analysis.
* Statistical results of the final design in analysis/results/statistics.txt.
* Security targets per mode (ANALYSIS.md); figure of the step (docs/step.svg).
* The analysis entry points `pikaseko256_steps` and `pikaseko256_steps_portable`
  moved to `include/pikaseko256_analysis.h`.
* Streaming interface: `pikaseko256_init`, `pikaseko256_update`,
  `pikaseko256_final` and their `_portable` versions, sharing one context type.
  Output unchanged.
* Run-time CPU dispatch: `pikaseko256`, `pikaseko256_unkeyed`, `pikaseko64`,
  the streaming functions and `pikaseko256_steps` now pick the AVX-512 or the
  portable code and work on every CPU and in every build. The AVX-512 code is
  available by name with the suffix `_avx512` (API change: these were the
  unsuffixed names). `pikaseko256_uses_avx512()` reports the choice.
* AVX2 + VAES and AES-NI implementations (`src/pikaseko256_simd.h`), chosen
  by the dispatch on x86-64 CPUs without AVX-512; `pikaseko256_implementation()`
  names the choice; `bench --impl` measures any of them.
* ARMv8 (AArch64, little-endian) implementation with AESE/AESMC, chosen by the
  dispatch when the CPU has the AES extension. The build flag for portable-only
  builds is now `PIKASEKO256_PORTABLE_ONLY` (the old `PIKASEKO256_NO_AVX512` is
  still accepted); the CMake option is `PIKASEKO256_SIMD`.
* MSVC: the sources, benchmarks and tools build with MSVC (CMake), with the
  x86 implementations; Windows CI job.
* `make fuzz`: libFuzzer target comparing every implementation (one-shot and
  streaming) with the portable one under ASan and UBSan; run in CI.
* Python reference implementation from the specification (`ref/pikaseko256.py`,
  `make test-python`): reproduces all test vectors and the checksum; run in CI.
* Cryptanalysis challenges on reduced versions (analysis/challenges.md); the
  Python reference computes them (`--final`, `absorb`).
* Faster short messages without AVX-512: the AVX2, AES-NI, ARMv8 and portable
  code start the unkeyed hash from the precomputed state after step 0 and set up
  the key with vector operations (AVX2 unkeyed 93 to 85 ns, keyed 160 to 101 ns;
  portable 6.1 to 5.4 µs). Output unchanged.
* One benchmark for every implementation and platform (`bench --impl`): the
  iteration count is set by time, it reports the median, the minimum and the
  spread, and prints the cpufreq governor; `bench_portable` is gone
  (`make bench-portable` runs `bench --impl portable`).
* Guard-page test: messages that end at, or start right after, an inaccessible
  page, for every implementation (POSIX systems).
* `pikaseko256_key_from_bytes` and `pikaseko256_digest_to_bytes`: byte forms
  of the key and the digest, independent of the host byte order.

## 0.0.1 (version 0) - 2026-10-26

Initial public release.

* Specification of PikasekoHash-256 version 0 (SPEC.md) with test vectors.
* AVX-512 + VAES implementation and a portable C implementation without
  tables, bit-identical on every platform.
* Tests, benchmark, statistical battery, diffusion tool and a checker for
  the published one-step collision pairs.
* Security and quality analysis (ANALYSIS.md) and five review reports
  (analysis/reviews/). Known: free-start collisions are practical; the
  semi-free-start question is open. Experimental; not for production
  cryptographic use.
