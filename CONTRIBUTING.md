# Contributing

Thank you for your interest. PikasekoHash-256 is a small, single-author
research project. The most useful contributions are cryptanalysis (see
[SECURITY.md](SECURITY.md)), independent implementations checked against the
test vectors, measurements on other CPUs, and bug reports.

## Build and test

    make            # everything, into build/
    make test       # must pass before any pull request
    make bench      # speed (please give CPU, clock and compiler with numbers)
    make battery    # statistical battery, a few minutes

`make test` checks the test vectors, compares the AVX-512 and portable
implementations for all lengths 0..2048, and verifies the one-step collision
pairs in `analysis/data/`. The AVX-512 tests need a CPU with AVX-512F,
AVX-512BW and VAES.

## Changes to the function

Any change that alters the output of the function (the step, the constants,
the schedule, padding, the fold, the key handling) must update, in the same
pull request:

* [SPEC.md](SPEC.md): the definition and the test vectors in section 11;
* `test/test_vectors.txt` (regenerate with `./build/vectors > test/test_vectors.txt`);
* [ANALYSIS.md](ANALYSIS.md): every section whose measurements or bounds
  the change affects, with the result marked proven, measured or argued, and
  the planned-changes list in section 13;
* [CHANGELOG.md](CHANGELOG.md), as a new version.

Both implementations must stay bit-identical. A design change without a new
analysis of the affected properties (in particular the one-step collision
construction of ANALYSIS.md, section 4) will not be merged.

Changes that keep the output unchanged (speed, portability, tooling,
documentation) need only the tests to pass and, for speed claims, numbers.

## Coding style

Match the existing code: C11, two-space indentation, `//` comments, short
functions, names in the style of the surrounding file. The portable code
must stay free of tables and of branches or memory accesses that depend on
the key or the message contents. Keep documents plain and precise: state
what was measured, proven or argued.

## License

By contributing you agree that your contribution is licensed under the MIT
license of this repository ([LICENSE](LICENSE)), the same terms under which
the project is distributed (inbound = outbound). There is no contributor
license agreement.
