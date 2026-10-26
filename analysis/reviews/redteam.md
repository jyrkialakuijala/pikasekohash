# PikasekoHash-256 (final design): red-team / implementation-security review

*Editorial note: this report was written in the development repository. Script and log names refer to
files there. Internal variant names are replaced by plain descriptions; FOLD 0 is the linear sum fold of the
previous finalization, FOLD 4 the nonlinear fold of the final design (SPEC.md, section 8), and FINAL n or
B16_FINAL = n means n final steps. Technical content and numbers are unchanged. Function names such as
`pikaseko64_portable*`, `InitSbox`, `InitRegs` and the header `pikaseko.h` are those of the development code. Findings 2,
3 and 4, the byte-order items and the `(NULL, 0)` item of Task 4 are addressed in this repository (ANALYSIS.md,
section 12); finding 1 and the streaming item are open.*

Viewpoint: hash-flooding and implementation-security practitioner (HashDoS against
Murmur/City, seed-independent multicollisions, SMHasher, Ahle-style adversarial inputs,
side channels and real-world misuse). Target: the final design (defaults of the development source: xor keys, step constant, 3 steps per absorption, products at delay 2,
**6 final steps (B16_FINAL=6)**, nonlinear fold FOLD 4). All work in a separate directory of the development repository;
no tracked source was modified; reduced builds are `-D` flag variants only.
Shared machine, ≤4 threads; no cycle/ns numbers are used in any conclusion.

Build of record used for attacks:
`gcc -O2 -mavx512f -mavx512bw -mavx512vl -mavx512vbmi -mvaes -maes` on the AVX-512 source of the development code.

## Summary of findings, most actionable first

| # | area | finding | severity |
|---|---|---|---|
| 1 | misuse / API | `key==NULL`, all-zero `key[8]`, and `pikaseko256_unkeyed` are the **same public function**. A MAC that derives or defaults to a zero key is silently unauthenticated and forgeable. No domain separation between keyed, unkeyed, 64-bit and 256-bit outputs. | high (deployment) |
| 2 | timing / key leak | The **portable reference** (`pikaseko64_portable*`) is a table-S-box AES: data-dependent `kSbox[secret]` loads leak the state (hence the key) by cache timing. It is exported in `pikaseko.h` as a usable function with **no runtime CPU dispatch**, so a non-AVX-512 deployment uses it as the real keyed MAC. | high (if used keyed) |
| 3 | timing / key leak | The AVX-512 keyed path contains a **data-dependent branch on the all-zero key** (`vptestnmd`+`kortestw`+`je`, confirmed in the disassembly). It leaks one bit — "is the key all-zero" — which combines with #1. Otherwise the AVX-512 keyed path is constant-time (vaesenc/vpmuludq/vpxord/vpshuf only). | low–medium |
| 4 | concurrency | `InitSbox()` in the portable reference lazily fills a `static` table with no synchronization (done-flag `kSbox[0]` written last). Concurrent first calls are a C11 data race (UB) and can read a half-built table → wrong digest. | medium (portable only) |
| 5 | keyed collisions | No seed-independent collision or seed-independent differential found. Every tested message difference is fully key-dependent (512/512 distinct output differences over 512 random keys, 0 collisions). Structural argument below: the key diffuses through step 0's AES before any packet, so no low-weight universal differential can exist. | holds (good) |
| 6 | unkeyed reduced near-collisions | With the attacker's strongest single-bit control in the last packet, the output Hamming distance is indistinguishable from random even at **B16_FINAL=0**: mean 128.0, best min over 2^16 ≈ 86–89 vs a random-pair control min of 91–97. No constant output bits. Product steering adds no near-collision leverage. | holds (good) |
| 7 | unkeyed reduced distinguisher | Corroborated the earlier review: a first-packet single-bit difference at an even last-group phase gives a weak per-bit bias at B16_FINAL=0/1 (worst |p−½| ≈ 0.003, ~3σ at 2^18), gone by the installed 6 finals. Distinguisher, not a collision. | informational |
| 8 | statistics | The 256-bit battery (`--quick`) PASS at the installed 6 finals **and** at FINAL=1 and FINAL=2 (worst 32-bit collision z within ±3.2). The battery does not see the differential distinguisher of #7 — the sum fold hides it, as the designer notes. | informational |

No attack on the full hash. Nothing below is a break of the installed design.

## Task 1 — keyed hash-table use: seed-independent collisions / multicollisions

Attempted (`kdiff.c`, `logs/kdiff.txt`): for each candidate message-difference pattern,
compute `d_K = H_K(M) ⊕ H_K(M')` over 512 random 512-bit keys and count distinct values
(full 256-bit and the 64-bit output lane). A seed-independent collision is `d_K=0` for all K;
a seed-independent differential is a single repeated nonzero `d_K`. Patterns tested at lengths
16–320: single-bit flips, lane-0 MSB flip (the fold's MSB-cancellation lane), additive +1 in a
lane, additive +d/−d in two packets' feed lanes (cancellation attempt across the transversal
feed set), and equal-xor in two packets.

Result: **every pattern gives 512/512 distinct output differences and 0 collisions**, for both
the 256-bit and the 64-bit output. No seed-independent structure at low weight.

Why this is structurally expected, and why the obvious levers fail:
* **key-only-xors-into-IV.** The key enters only the IV (register i, lane l:
  `C_i[l] ⊕ rotl64(K[(l+i) mod 8], 4i)`), but **step 0 runs before the first packet**, pushing
  all eight key lanes through the AES butterfly into all sixteen registers. By the first
  injection the state is a nonlinear function of K, so any message-difference cancellation
  condition is itself key-dependent. A universal (key-independent) collision would require a
  probability-1 differential that holds for every value of a secret 8192-bit state — i.e. a
  purely linear relation surviving to the output. The only linear pieces are the key layer
  (xor) and the fold; the two AES rounds per register per step destroy them within one step.
* **transversal feeds defeat intra-absorption cancellation.** Feeds {0,5,10,15} are a
  transversal of all four group partitions, so a `+d/−d` pair in two packets never lands in the
  same pair or group and cannot cancel in a single step (confirmed: fully key-dependent).
* **padding/length lane.** Framing is injective (verified in the earlier review, mode viewpoint, across all lengths);
  the length sits in lane 7 of the last packet and additionally perturbs r0, so equal-length,
  different-content messages never share the length lane as a collision lever.
* **unkeyed→keyed relation.** Because the key map `K→IV` is injective and immediately
  nonlinearised, an unkeyed collision does **not** transfer to any keyed instance.

Practical reading for a hash table keyed with a secret: an attacker who does not know the key
has no low-cost seed-independent multicollision. If the key leaks (see Task 3), flooding is
trivial (known 64-bit hash, 2^32 birthday) — so the key's secrecy is load-bearing, which makes
findings #1–#4 the real hash-flooding risk, not the core.

## Task 2 — unkeyed near-collisions on reduced finals (B16_FINAL = 0,1,2)

Builds: `-DB16_FINAL=0/1/2/3` (`obj/avx_f*.o`). Searcher `nc2.c`/`nearcol.c`
(`logs/nc2.txt`, `logs/nearcol_fixed.txt`). For message length L (which fixes the last-group
phase; L∈{200,456,712,968} span phases 0,3,2,1) and the attacker's strongest single-bit
difference in a **valid** last-packet byte, over N=2^16 random messages: minimum 256-bit
Hamming distance, number of never-flipping output bits, and a random-pair control.

| B16_FINAL | steps after last injection | mean Hamming | best min over 2^16 (1-bit) | const bits | random-pair control min |
|---|---:|---:|---:|---:|---:|
| 0 | 2 | 128.00 | 89 | 0 | 93 |
| 1 | 3 | 127.99 | 87 | 0 | 91 |
| 2 | 4 | 127.99 | 88 | 0 | 92 |
| 3 | 5 | 128.00 | 86 | 0 | 97 |

(min of 2^16 Binomial(256,½) draws is ≈ 90.) The best single-bit minimum tracks the random
control within the min-of-N spread; **no near-collision, no constant output bits**, even at
FINAL=0. Message-bit control (which subsumes the unkeyed product-steering freedom for the last
group, since the packet sets the feed register and the steerable products feed r3/r6/r9/r12 two
steps before the output) does not drive the Hamming distance below the random floor. Product
steering therefore buys no near-collision leverage on reduced finals.

For completeness, the bias of the earlier review is a *distinguisher* on the first-packet difference at
an even last-group phase (`distg.c`, `logs/distg.txt`): at FINAL=0/1 the worst per-bit flip bias
is ≈0.003 (~3σ at 2^18), concentrated in output word 2; at FINAL=2 it is already within noise.
The mean Hamming is 128 throughout, so it never becomes a near-collision. The installed 6 finals
leave ≥4 steps of margin over this.

## Task 3 — key recovery / key leakage from timing

Two concrete channels, both confirmed from source/disassembly:

1. **Portable reference is not constant-time (serious, if used as a keyed MAC).**
   the portable reference of the development code implements `aesenc` with a software table: `kSbox[b[...]]` and the
   `Xt`/MixColumns bytes, indexed by **secret-dependent** state bytes. Because step 0 maps
   `K ⊕ C` through AES, the first-round S-box inputs are `S-box(K_l ⊕ const_l)`-type values;
   the cache-line access pattern of `kSbox[]` leaks the high nibbles of these inputs, which is
   exactly the setup for a textbook first-round AES cache-timing key recovery (Osvik–Shamir–
   Tromer / Bernstein style) against a MAC that an attacker can query adaptively. There is **no
   runtime dispatch** in the library: on any target without AVX-512+VAES the application links
   and calls `pikaseko64_portable*`, so this is the real implementation there, and nothing in
   `pikaseko.h` warns against keyed use. **Fix:** do not ship the table S-box as a keyed path —
   use a bitsliced/constant-time software AES (or vperm-based) for the portable build, or refuse
   to expose `pikaseko64_portable*` with a non-NULL key, or provide runtime dispatch that falls
   back to a constant-time implementation.

2. **AVX-512 path branches on the all-zero key (minor).** In the IV setup (`InitRegs`) the flag
   `zero = (key[0]|…|key[7])==0` controls `if(!zero) v = VXOR(v, rotated-key)`. The compiler
   emits a data-dependent branch (`vptestnmd %zmm,%zmm,%k0; kortestw; je`, verified by
   `objdump` on `obj/avx.o`). This leaks a single bit — whether the secret key is all-zero —
   and taking the zero-key fast path is timing-distinguishable from a keyed hash. Across non-zero
   keys the path is identical, and the body (vaesenc/vpmuludq/vpxord/vpshufi) has
   operand-independent latency on Zen 5, so the AVX-512 keyed core is otherwise constant-time.
   Combined with finding #1 (Task 4): the all-zero key is both the timing-special case and the public
   unkeyed hash. **Fix:** compute the IV unconditionally (xor the rotated key in with no branch;
   for the genuine unkeyed entry, gate only on the public `key==NULL`, not on key *value*).

No timing numbers are reported (shared machine); both channels are argued from the code and the
emitted branch, which is sufficient to establish presence.

## Task 4 — misuse review (library-user footguns)

* **Keyed vs unkeyed collapse / no domain separation (highest-impact footgun).**
  `pikaseko256(m, NULL)`, `pikaseko256(m, {0,…,0})` and `pikaseko256_unkeyed(m)` are byte-for-byte
  identical (verified, `sanity.c`). A user who zero-initializes a key struct, fails to load a key,
  or derives an all-zero key from a KDF edge case gets the **public** hash and a forgeable MAC,
  with no error. There is also no separation between the 64-bit and 256-bit outputs (`pikaseko64`
  is literally `out[0]` of `pikaseko256`), nor any parameter/flag word for mode, key length,
  output length, XOF or tree use. **Fix:** a parameter word (version, keyed flag, output length,
  mode) xored into a register of the IV after the key, so "keyed with K=0" ≠ unkeyed; and reject
  or separately domain-tag the all-zero key.
* **Key format is `uint64_t key[8]`, little-endian lanes, fixed 512 bits.** A user with a byte
  string key must pack it into 8 little-endian u64s; doing `memcpy` on a big-endian host, or
  passing a `char key[64]` cast, silently changes the MAC. There is no key-length argument, so
  keys shorter than 512 bits must be padded by the caller (another place to get domain separation
  wrong). **Fix:** document the exact byte→lane mapping; consider a `(const uint8_t*, size_t)` key
  API that does the packing and length-tagging internally.
* **Output byte order unspecified.** `out[4]` is four u64 lanes; the natural digest is their
  little-endian encoding, but this is not stated, so two implementations can serialize the same
  256-bit value differently. `pikaseko64` returns a `uint64_t`, not bytes.
* **Little-endian only.** The portable reference mixes byte-views and lane-views (AES, length
  lane, `VSETBYTE`) and is correct only on little-endian hosts; there is no guard. The AVX-512
  build is x86-only.
* **No streaming / incremental API.** Only one-shot `(data, len)`. Users hashing large or
  chunked inputs must buffer the whole message; there is no `init/update/final`, and the grouped
  tail + length lane make a naive user-side chunking impossible to get right. **Fix:** document
  that no streaming exists, or add an Init/Update/Final that preserves the 3-steps-per-group
  schedule and injects the length only at Final.
* **Portable vs AVX naming collision.** `pikaseko64` (AVX-512) and `pikaseko64_portable` differ
  by a suffix and are "bit-identical"; a user may pick the portable one for "portability" and
  unknowingly take the non-constant-time path (finding #2). No dispatch, no warning.
* **Formal UB on `(NULL, 0)`** in the reference (`memcpy(dst, NULL, 0)`), and the `InitSbox`
  data race (finding #4) — both only in the portable build.

## Task 5 — SMHasher-style sweep

The SMHasher setup script of the development repository `git clone`s rurban/smhasher and builds with `make -j"$(nproc)"`,
i.e. it **requires network and uses all cores** — it cannot be used offline and would violate the
≤4-thread / shared-machine constraint. Per the task's stated fallback, I ran the 256-bit battery
(the SMHasher-style battery, `tools/battery.c` in this repository: avalanche, BIC, differential, sparse, cyclic,
two-byte, window, permutation, seed, length, with Poisson tails) on the installed build and on
reduced finals.

| build | verdict | worst 32-bit collision z |
|---|---|---:|
| installed (B16_FINAL=6) | PASS | −3.2 |
| B16_FINAL=1 | PASS | +3.1 |
| B16_FINAL=2 | PASS | −3.2 |

All clean (`logs/battery_*.txt`). The battery passes even down to one final step, which is the
designer's known point (the sum fold hides slow diffusion from statistical tests) — it is **not**
a safety signal, because the differential distinguisher of Task 2 and finding #7 lives below the battery's
resolution. Keep the margin from the targeted probes, not the battery.

## Prioritized fixes

1. **Domain separation + kill the zero-key special case** (Task 4, finding #3): add a parameter/flag
   word to the IV so keyed-with-K=0 ≠ unkeyed; separate 64- vs 256-bit and any future XOF/tree
   outputs; reject or tag the all-zero key. Removes the single most dangerous deployment footgun.
2. **Do not ship a table-S-box AES as a keyed path** (finding #2): constant-time portable AES, or
   refuse `pikaseko64_portable*` with a non-NULL key, or runtime dispatch to a constant-time
   fallback. Fix the `InitSbox` race (constant table or `call_once`) regardless (finding #4).
3. **Remove the key-value branch** in `InitRegs`; gate only on `key==NULL` (finding #3).
4. **Specify key packing and digest byte order; warn about little-endian-only and the absence of
   streaming** in `pikaseko.h` (Task 4).
5. Keep **≥ the installed 6 final steps**; reduced finals leak to targeted differential probes
   (and are statistically invisible, so do not trust the battery to catch a reduction).

## Files (in the development repository)

`sanity.c` (API/zero-key check); `kdiff.c`+`logs/kdiff.txt` (seed-independence probe, Task 1);
`nearcol.c`,`nc2.c`+`logs/nearcol_fixed.txt`,`logs/nc2.txt` (near-collisions, Task 2);
`distg.c`+`logs/distg.txt` (reduced-final distinguisher, Task 7); `logs/battery_*.txt`
(in-repo battery, Task 5). Objects in `obj/` are `-D`-flag reduced builds of the unmodified
source. No timing numbers are used in any conclusion; the side-channel claims are from source
and the emitted branch (`objdump obj/avx.o`).
