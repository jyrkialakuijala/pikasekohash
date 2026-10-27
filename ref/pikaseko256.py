#!/usr/bin/env python3
# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
"""PikasekoHash-256 version 1 in plain Python, written from SPEC.md and independent of the C
code. Slow (milliseconds per short message), meant for checking implementations:

    python3 ref/pikaseko256.py                  # check test/test_vectors.txt (all but the checksum)
    python3 ref/pikaseko256.py --checksum       # also the checksum over lengths 0..2048 (seconds)
    python3 ref/pikaseko256.py --hex 616263     # the unkeyed 256-bit digest of 'abc'
    python3 ref/pikaseko256.py --hex 616263 --key 0011...   # keyed (key bytes in hex, any length)
    python3 ref/pikaseko256.py --hex 616263 --bits 64       # the 64-bit hash
    python3 ref/pikaseko256.py --hex 616263 --final 1       # reduced: one final step

Digests are printed as bytes in hex (the 64-bit hash also as an integer).
"""
import sys

MASK64 = (1 << 64) - 1
PI, E, PHI = 0x243F6A88, 0xB7E15162, 0x9E3779B9

# Section 2: step constants, words 96..127 of the hexadecimal expansion of pi.
C = [
    [0x62fb1341cee4c6e8, 0xef20cada36774c01, 0xd07e9efe2bf11fb4, 0x95dbda4dae909198,
     0xeaad8e716b93d5a0, 0xd08ed1d0afc725e0, 0x8e3c5b2f8e7594b7, 0x8ff6e2fbf2122b64],
    [0x8888b812900df01c, 0x4fad5ea0688fc31c, 0xd1cff191b3a8c1ad, 0x2f2f2218be0e1777,
     0xea752dfe8b021fa1, 0xe5a0cc0fb56f74e8, 0x18acf3d6ce89e299, 0xb4a84fe0fd13e0b7],
    [0x7cc43b81d2ada8d9, 0x165fa26680957705, 0x93cc7314211a1477, 0xe6ad206577b5fa86,
     0xc75442f5fb9d35cf, 0xebcdaf0c7b3e89a0, 0xd6411bd3ae1e7e49, 0x00250e2d2071b35e],
    [0x226800bb57b8e0af, 0x2464369bf009b91e, 0x5563911d59dfa6aa, 0x78c14389d95a537f,
     0x207d5ba202e5b9c5, 0x832603766295cfa9, 0x11c819684e734a41, 0xb3472dca7b14a94a],
]


# ---- AES (FIPS 197): the S-box from the field inverse and the affine map ----

def _xtime(x):
    return ((x << 1) ^ (0x1B if x & 0x80 else 0)) & 0xFF


def _gmul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a, b = _xtime(a), b >> 1
    return r


def _sbox():
    inv = [0] * 256
    for x in range(1, 256):
        for y in range(1, 256):
            if _gmul(x, y) == 1:
                inv[x] = y
                break
    box = []
    for x in range(256):
        b, s = inv[x], 0x63
        for i in range(5):  # b ^ rotl(b,1) ^ rotl(b,2) ^ rotl(b,3) ^ rotl(b,4) ^ 0x63
            s ^= ((b << i) | (b >> (8 - i))) & 0xFF
        box.append(s)
    return box


SBOX = _sbox()
assert SBOX[0] == 0x63 and SBOX[1] == 0x7C and SBOX[0x53] == 0xED


def aes_round_block(blk, key):
    """One AES encryption round on 16 bytes, as x86 AESENC: MixColumns(ShiftRows(SubBytes(x))) ^ k.
    Byte n of the block is row n % 4, column n // 4."""
    s = [SBOX[b] for b in blk]
    # ShiftRows: row r of column c comes from column (c + r) mod 4.
    t = [s[4 * ((c + r) % 4) + r] for c in range(4) for r in range(4)]
    out = bytearray(16)
    for c in range(4):
        a0, a1, a2, a3 = t[4 * c:4 * c + 4]
        out[4 * c + 0] = _xtime(a0) ^ (_xtime(a1) ^ a1) ^ a2 ^ a3 ^ key[4 * c + 0]
        out[4 * c + 1] = a0 ^ _xtime(a1) ^ (_xtime(a2) ^ a2) ^ a3 ^ key[4 * c + 1]
        out[4 * c + 2] = a0 ^ a1 ^ _xtime(a2) ^ (_xtime(a3) ^ a3) ^ key[4 * c + 2]
        out[4 * c + 3] = (_xtime(a0) ^ a0) ^ a1 ^ a2 ^ _xtime(a3) ^ key[4 * c + 3]
    return bytes(out)


# FIPS 197, appendix B: the state after the first round of the example encryption.
assert aes_round_block(bytes.fromhex("193de3bea0f4e22b9ac68d2ae9f84808"),
                       bytes.fromhex("a0fafe1788542cb123a339392a6c7605")) == \
    bytes.fromhex("a49c7ff2689f352b6b5bea43026a5049")


# ---- Vectors (section 1): 64 bytes; lane l = bytes 8l .. 8l + 7 little-endian ----

def lanes(v):
    return [int.from_bytes(v[8 * l:8 * l + 8], "little") for l in range(8)]


def vec(ls):
    return b"".join((x & MASK64).to_bytes(8, "little") for x in ls)


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def add(a, b):
    return vec([x + y for x, y in zip(lanes(a), lanes(b))])


def mul32(a, b):
    return vec([(x & 0xFFFFFFFF) * (y & 0xFFFFFFFF) for x, y in zip(lanes(a), lanes(b))])


def rot1(x):  # block b of the result is block b + 1 of x
    return x[16:] + x[:16]


def rot2(x):
    return x[32:] + x[:32]


def aesenc(x, k):
    return b"".join(aes_round_block(x[16 * b:16 * b + 16], k[16 * b:16 * b + 16]) for b in range(4))


def rotl64(x, n):
    return ((x << n) | (x >> (64 - n))) & MASK64 if n else x


# ---- Sections 4 to 10 ----

VERSION = 1


def domain(keyed, key_hashed, key_len, out_len):
    """Section 4: the domain word."""
    return VERSION | (keyed << 8) | (key_hashed << 9) | (key_len << 16) | (out_len << 24)


def initial_state(key_words, dom):
    """Section 4. key_words: eight 64-bit integers (zero for the unkeyed hash)."""
    r = []
    for i in range(16):
        a = (PHI + i * PI) & 0xFFFFFFFF
        lanes_i = [a * ((PHI + l * E) & 0xFFFFFFFF) ^ rotl64(key_words[(l + i) % 8], (4 * i) % 64)
                   for l in range(8)]
        if i == 2:
            lanes_i[0] ^= dom
        r.append(vec(lanes_i))
    return r


def step(r, t):
    """Section 5."""
    s = 1 << (t % 4)
    c = 1 if s == 8 else 2 * s
    rot = rot1 if t % 2 == 0 else rot2
    new = list(r)
    for i in range(16):
        if i & s or i & c:
            continue
        j, i2 = i ^ s, i ^ c
        j2 = i2 ^ s
        k = xor(r[i], r[j])
        k2 = xor(r[i2], r[j2])
        if i == 0:
            k2 = xor(k2, vec(C[t % 4]))
        kk = xor(k, k2)
        new[i] = aesenc(aesenc(r[i], k2), rot(k))
        new[i2] = aesenc(aesenc(r[i2], k), rot(k2))
        new[j] = aesenc(aesenc(r[j], kk), rot(k2))
        new[j2] = aesenc(aesenc(r[j2], kk), rot(k))
    return new


F, M, G = (0, 5, 10, 15), (1, 4, 11, 14), (3, 6, 9, 12)


def packets_of(msg):
    """Section 6: padding and packets."""
    n = len(msg)
    padded = msg + b"\x80" + b"\x00" * ((55 - n) % 64) + (n & MASK64).to_bytes(8, "little")
    assert len(padded) % 64 == 0
    return [padded[i:i + 64] for i in range(0, len(padded), 64)]


def key_words_of(key):
    """Section 4: a key of any length to eight words and the key fields of the domain word."""
    hashed = 0
    if len(key) > 64:
        key, hashed = digest(key, None, 32), 1
    kb = key + b"\x00" * (64 - len(key))
    return [int.from_bytes(kb[8 * i:8 * i + 8], "little") for i in range(8)], hashed, len(key)


def digest(msg, key=None, out_len=32, final_steps=6):
    """PikasekoHash-256 version 1: the out_len-byte digest (32, or 8 for the 64-bit hash)
    of msg, keyed with the bytes key (any length, possibly empty) or unkeyed (key None).
    final_steps other than 6 gives the reduced versions of analysis/challenges.md."""
    assert out_len in (8, 32)
    if key is None:
        words, dom = [0] * 8, domain(0, 0, 0, out_len)
    else:
        words, hashed, klen = key_words_of(key)
        dom = domain(1, hashed, klen, out_len)
    r = initial_state(words, dom)
    t = 0
    pending = []  # Section 7: products of the previous group, xored in after the next step
    packets = packets_of(msg)
    for u in range(0, len(packets), 4):
        q = packets[u:u + 4]
        r = step(r, t); t += 1
        for k, p in pending:
            r[G[k]] = xor(r[G[k]], p)
        for k, p in enumerate(q):
            r[F[k]] = add(r[F[k]], p)
        r = step(r, t); t += 1
        r = step(r, t); t += 1
        pending = [(k, mul32(r[M[k]], r[F[k]])) for k in range(len(q))]
    for f in range(final_steps):  # Section 8
        r = step(r, t); t += 1
        if f == 0:
            for k, p in pending:
                r[G[k]] = xor(r[G[k]], p)
    if final_steps == 0:
        for k, p in pending:
            r[G[k]] = xor(r[G[k]], p)
    a = r[0]
    for i in range(1, 16):
        a = add(a, r[i])
    s = aesenc(aesenc(a, add(r[1], r[2])), rot1(add(r[3], r[4])))
    out = lanes(add(s, rot2(s)))[:4]
    return b"".join(x.to_bytes(8, "little") for x in out)[:out_len]


def absorb(r, t, packets):
    """One absorption in the self-contained form used by analysis/challenges.md: from the
    state r (sixteen 64-byte vectors) before step t: step t, inject the 1..4 packets, steps
    t + 1 and t + 2, form the products, step t + 3, xor the products in. Returns the state;
    the next absorption in this form starts at step t + 3 with its injection (its own first
    step is the step t + 3 already run)."""
    r = step(r, t)
    for k, p in enumerate(packets):
        r[F[k]] = add(r[F[k]], p)
    r = step(r, t + 1)
    r = step(r, t + 2)
    prods = [mul32(r[M[k]], r[F[k]]) for k in range(len(packets))]
    r = step(r, t + 3)
    for k, p in enumerate(prods):
        r[G[k]] = xor(r[G[k]], p)
    return r


# ---- Test vectors (section 11) ----

def vector_lines():
    """The lines of test/test_vectors.txt except the checksum."""
    buf = bytes(i % 256 for i in range(4200))
    keys = [(n, bytes(range(n)) if n <= 256 else None) for n in (0, 32, 64, 100)]
    lines = []
    for n in (0, 1, 55, 56, 63, 64, 255, 256, 1023, 1024, 1025, 4096):
        lines.append("unkeyed256 len %5d  %s" % (n, digest(buf[:n]).hex()))
        lines.append("unkeyed64  len %5d  %s" % (n, digest(buf[:n], None, 8).hex()))
        for klen, key in keys:
            lines.append("keyed256 klen %3d len %5d  %s" % (klen, n, digest(buf[:n], key).hex()))
        lines.append("keyed64  klen %3d len %5d  %s" % (32, n, digest(buf[:n], bytes(range(32)), 8).hex()))
    return lines


def checksum_line():
    buf = bytes(i % 256 for i in range(2048))
    acc = 0
    for n in range(2049):
        d = digest(buf[:n])
        for i in range(4):
            acc = ((acc * 0x9E3779B97F4A7C15) & MASK64) ^ int.from_bytes(d[8 * i:8 * i + 8], "little")
    return "unkeyed256, all lengths 0..2048 folded: %016x" % acc


def check_vectors(path, checksum):
    with open(path) as f:
        want = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("PikasekoHash")]
    got = vector_lines() + ([checksum_line()] if checksum else [])
    bad = 0
    for i, line in enumerate(got):
        if line not in want:
            bad += 1
            print("MISMATCH", line)
    print("Python reference: %d of %d test vectors reproduced" % (len(got) - bad, len(got)))
    return bad == 0


def main(argv):
    if "--print-vectors" in argv:  # writes test/test_vectors.txt (used when the function changes)
        print("PikasekoHash-256 version %d test vectors (data byte i = i mod 256, key byte i = i; digests as bytes)" % VERSION)
        for line in vector_lines():
            print(line)
        print(checksum_line())
        return 0
    if "--hex" in argv:
        msg = bytes.fromhex(argv[argv.index("--hex") + 1])
        key = bytes.fromhex(argv[argv.index("--key") + 1]) if "--key" in argv else None
        bits = int(argv[argv.index("--bits") + 1]) if "--bits" in argv else 256
        final = int(argv[argv.index("--final") + 1]) if "--final" in argv else 6
        d = digest(msg, key, bits // 8, final)
        print(d.hex() + ("  (%016x)" % int.from_bytes(d, "little") if bits == 64 else ""))
        return 0
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test", "test_vectors.txt")
    return 0 if check_vectors(path, "--checksum" in argv) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
