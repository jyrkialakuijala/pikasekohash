# Changelog

All notable changes to this project are listed here. Any version that
changes the output of the function changes the test vectors.

## Unreleased

* Paper (paper/pikasekohash.md) describing the design and its analysis.
* Statistical results of the final design in analysis/results/statistics.txt.

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
