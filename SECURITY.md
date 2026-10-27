# Security policy

## Status

PikasekoHash-256 is an experimental research design (version 0). It is not
intended for production cryptographic use. Free-start collisions of its
compression function and finalization are already practical (ANALYSIS.md,
section 4), and the semi-free-start question is open (section 5). Treat
every security property as unproven. The levels the design aims for, per
mode, are listed under "Security targets" in ANALYSIS.md; an attack below
one of them is a result worth reporting.

There are no supported releases in the sense of security fixes: a weakness
in the function itself is addressed by changing the design, which changes
the test vectors and the version.

## What counts as a security issue

In the function (the definition in SPEC.md):

* collisions, second preimages or preimages of the full hash, keyed or
  unkeyed;
* semi-free-start collisions of one absorption (same chaining value,
  different packets);
* distinguishers on the full hash, including key-recovery or forgery
  attacks on the keyed hash;
* results on reduced versions that beat the margins stated in ANALYSIS.md,
  or that show a stated bound or claim to be wrong.

In the implementations (`src/`):

* side channels: timing, cache or branch behaviour that depends on the key
  or the message contents (the message length is not hidden, by design);
* out-of-bounds reads or writes, undefined behaviour, or any input on which
  the AVX-512 and portable implementations disagree or differ from SPEC.md.

## Reporting cryptanalysis

This is a research design, and cryptanalysis is what it needs. Please report
attacks, distinguishers, improved bounds and corrections publicly by
opening an issue (there is a "Cryptanalysis result" template), or link to
your paper or preprint. Public discussion helps everyone who studies the
design. Results will be credited in ANALYSIS.md.

## Reporting implementation vulnerabilities

For an implementation problem that could hurt someone who already uses the
code (for example a memory-safety bug or a key-dependent timing leak), you
may prefer a private report: use GitHub's private vulnerability reporting
("Report a vulnerability" on the Security tab of the repository). If in
doubt, a public issue is fine too, given the experimental status.

There is no bug bounty.
