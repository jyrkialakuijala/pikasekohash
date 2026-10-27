# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
# Usage: python3 tools/step_svg.py docs/step.svg
# Generates docs/step.svg: one group of the step, and the groups for each t mod 4.
import sys
W, H = 900, 660
o = []
def t(x, y, s, size=14, anchor="middle", weight="normal", fill="#1f2328", fam="monospace"):
    o.append(f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}">{s}</text>')
def box(x, y, w, h, fill, stroke="#57606a", rx=6):
    o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="1.2"/>')
def line(x1, y1, x2, y2, col="#57606a", arrow=True, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    a = ' marker-end="url(#ah)"' if arrow else ""
    o.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{col}" stroke-width="1.4"{d}{a}/>')
o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">')
o.append('<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#57606a"/></marker></defs>')
o.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
t(W/2, 28, "PikasekoHash-256: one step, one group of four registers", 17, weight="bold", fam="sans-serif")
# registers
regs = ["r_i", "r_j", "r_i2", "r_j2"]
xs = [140, 340, 560, 760]
keys1 = ["k2", "kk", "k", "kk"]
keys2 = ["rot(k)", "rot(k2)", "rot(k2)", "rot(k)"]
for x, r in zip(xs, regs):
    box(x-50, 50, 100, 32, "#ddf4ff"); t(x, 71, r, 15, weight="bold")
# key boxes
for x in xs: line(x, 82, x, 238, dash="4 3")
box(195, 110, 210, 50, "#fff8c5"); t(300, 131, "k  = r_i ^ r_j"); t(300, 150, "own-pair key", 11, fam="sans-serif", fill="#57606a")
box(495, 110, 210, 50, "#fff8c5"); t(600, 131, "k2 = r_i2 ^ r_j2"); t(600, 150, "^ C[t mod 4] in the group of r0", 11, fam="sans-serif", fill="#57606a")
box(345, 180, 210, 34, "#fff8c5"); t(450, 202, "kk = k ^ k2")
for x0, x1 in ((140, 260), (340, 340)): line(x0, 82, x1, 108)
for x0, x1 in ((560, 560), (760, 640)): line(x0, 82, x1, 108)
line(300, 160, 420, 178); line(600, 160, 480, 178)
# AES chains
for x, k1, k2 in zip(xs, keys1, keys2):
    box(x-88, 240, 176, 36, "#dafbe1"); t(x, 263, f"aesenc( · , {k1})", 13)
    line(x, 276, x, 298)
    box(x-88, 300, 176, 36, "#dafbe1"); t(x, 323, f"aesenc( · , {k2})", 13)
    line(x, 336, x, 358)
    box(x-50, 360, 100, 30, "#ddf4ff"); t(x, 380, regs[xs.index(x)] + "'", 14, weight="bold")
t(W/2, 414, "All keys come from the state before the step. rot = rot1 (blocks move by one) on even t, rot2 on odd t;", 12, fam="sans-serif", fill="#57606a")
t(W/2, 431, "the rotated key is the only data movement between the four 128-bit blocks of a register.", 12, fam="sans-serif", fill="#57606a")
# group grids
t(W/2, 468, "Groups by t mod 4 (stride s, cross offset c). Registers 0..15; one colour per group.", 14, weight="bold", fam="sans-serif")
cols = ["#b6e3ff", "#ffd8b5", "#d8b9ff", "#aceebb"]
feed, mult, targ = {0, 5, 10, 15}, {1, 4, 11, 14}, {3, 6, 9, 12}
for ph in range(4):
    s = 1 << ph; c = 1 if s == 8 else 2 * s
    gid = {}
    g = 0
    for i in range(16):
        if i & (s | c): continue
        for r in (i, i ^ s, i ^ c, i ^ c ^ s): gid[r] = g
        g += 1
    gx = 70 + ph * 215
    t(gx + 70, 495, f"t mod 4 = {ph}: s = {s}, c = {c}", 12, fam="sans-serif")
    for r in range(16):
        cx, cy = gx + (r % 4) * 36, 505 + (r // 4) * 30
        sw = "2.4" if r in feed else "1"
        o.append(f'<rect x="{cx}" y="{cy}" width="32" height="26" rx="4" fill="{cols[gid[r]]}" stroke="#1f2328" stroke-width="{sw}"/>')
        t(cx + 16, cy + 18, str(r), 12)
t(W/2, H - 12, "Bold outline: feed registers r0, r5, r10, r15 (each in a different group at every stride).", 11, fam="sans-serif", fill="#57606a")
o.append('</svg>')
open(sys.argv[1], "w").write("\n".join(o) + "\n")
