"""DISTDIR.ASM (the octagonal distance) against port/asm/distdir.c.

Coordinates and deltas over the whole 32-bit range, near zero, and where the
differences, the absolute values and 3 * min overflow: the original keeps the true sign
of a difference (JGE after SUB), compares the deltas signed and wraps its sums. The
routines read their stack arguments through DS ([BX+4], frameless), so DS is SS, as in
the game.
"""

from __future__ import annotations

import random
import struct

EDGES = [
    0,
    1,
    -1,
    2,
    7,
    8,
    0x7FFFFFFF,
    -0x7FFFFFFF,
    -0x80000000,
    0x40000000,
    -0x40000000,
    0x2AAAAAAB,
    0x55555555,
    0xFFFF,
    0x10000,
    -0x10000,
]


def values(rng, n):
    out = []
    for _ in range(n):
        kind = rng.random()
        if kind < 0.3:
            out.append(rng.choice(EDGES))
        elif kind < 0.6:
            out.append(rng.randrange(-5000, 5000))
        else:
            out.append(rng.randrange(-0x80000000, 0x80000000))
    return out


def test_between_points(krondor, native):
    rng = random.Random(1)
    m = krondor
    seg = m.alloc(16)
    cases = [tuple(values(rng, 4)) for _ in range(3000)]
    cases += [(a, b, c, d) for a in EDGES[:8] for c in EDGES[:8] for b, d in ((0, 0), (5, -9))]
    want = []
    for x1, y1, x2, y2 in cases:
        m.write((seg, 0), struct.pack("<4i", x1, y1, x2, y2))
        want.append(m.call_far("distdir_octagonal_distance", 0, 8, ds=seg, ss=seg).dx_ax)
    payload = b"".join(struct.pack("<4i", *c) for c in cases)
    got = list(struct.unpack(f"<{len(cases)}I", native.call("distdir.points", payload)))
    assert got == want


def test_from_deltas(krondor, native):
    rng = random.Random(2)
    cases = [tuple(values(rng, 2)) for _ in range(3000)]
    cases += [(a, b) for a in EDGES for b in EDGES]
    want = [
        krondor.call_far(
            "distdir_octagonal_distance_dxdy",
            dx & 0xFFFF,
            (dx >> 16) & 0xFFFF,
            dy & 0xFFFF,
            (dy >> 16) & 0xFFFF,
        ).dx_ax
        for dx, dy in cases
    ]
    payload = b"".join(struct.pack("<2i", *c) for c in cases)
    got = list(struct.unpack(f"<{len(cases)}I", native.call("distdir.deltas", payload)))
    assert got == want
