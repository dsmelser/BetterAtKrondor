"""PALBLEND.ASM (the blended colour's nearest palette entry) against port/asm/palblend.c.

The original reads the palette through the far pointer g_pAaPaletteBuf from the segment's
start, as the game's palettes from alloc_far are; the translation reads it at the pointer.
Palettes of 6-bit DAC values and of full bytes (the absolute differences run to 255), and
palettes built around each pair's average: exact matches and totals of 2 and 3 (the early
return), channels one under and at the threshold of 12h, best totals of 27h and 28h (found
or -1), several ever closer matches in a row. Indexes with their high byte set, which the
original masks off.

Once the threshold drops it equals the best total, so it never rejects an entry the test of
the total would accept: the drop only saves time, and translations without it are
equivalent (mutate.py's survivors).
"""

from __future__ import annotations

import random
import struct


def lookups(krondor, native, palette: bytes, pairs: list[tuple[int, int]]) -> None:
    m = krondor
    seg = m.alloc(len(palette))
    m.write((seg, 0), palette)
    m.set_u32("g_pAaPaletteBuf", seg << 16)
    want = [m.call_far("palette_blend_lookup", a, b).ax for a, b in pairs]
    m.free_all()
    payload = palette + b"".join(struct.pack("<HH", a, b) for a, b in pairs)
    got = list(struct.unpack(f"<{len(pairs)}H", native.call("palblend.lookup", payload)))
    assert got == want


def random_pairs(rng, n: int) -> list[tuple[int, int]]:
    pairs = []
    for _ in range(n):
        a, b = rng.randrange(256), rng.randrange(256)
        if rng.random() < 0.1:
            a |= rng.randrange(1, 256) << 8
        if rng.random() < 0.1:
            b |= rng.randrange(1, 256) << 8
        pairs.append((a, b))
    return pairs


def test_random_palettes(krondor, native):
    rng = random.Random(1)
    for top in (64, 64, 64, 256, 256, 16):
        palette = bytes(rng.randrange(top) for _ in range(768))
        lookups(krondor, native, palette, random_pairs(rng, 1500))


def test_palettes_around_the_average(krondor, native):
    """Entries 10h-E6h placed at chosen distances from the average of entries 0 and 1."""
    rng = random.Random(2)
    offsets = [0, 1, -1, 2, -2, 3, 0x11, -0x11, 0x12, -0x12, 0x13, 0xD, 0xE, 0xF, 0x14, 0x15]
    for case in range(400):
        avg = [rng.randrange(0x20, 0x60) for _ in range(3)]
        spread = [rng.randrange(0, 0x20) for _ in range(3)]
        palette = bytearray(rng.randrange(256) for _ in range(768))
        palette[0:3] = bytes(a - s for a, s in zip(avg, spread))
        palette[3:6] = bytes(a + s for a, s in zip(avg, spread))
        if case % 4 == 0:
            palette[3:6] = bytes(a + s + 1 for a, s in zip(avg, spread))  # the halving floors
        count = rng.randrange(1, 40)
        for _ in range(count):
            i = rng.randrange(0x10, 0xE7)
            palette[i * 3 : i * 3 + 3] = bytes(
                max(0, min(255, a + rng.choice(offsets))) for a in avg
            )
        if case % 5 == 0:  # a total of exactly 27h or 28h, under the threshold per channel
            i = rng.randrange(0x10, 0xE7)
            total = rng.choice([0x27, 0x28, 0x26, 0x29])
            parts = [0x11, 0x11, total - 0x22]
            palette[i * 3 : i * 3 + 3] = bytes(a + p for a, p in zip(avg, parts))
        if case % 7 == 0:  # a run of ever closer entries: the threshold drops step by step
            start = rng.randrange(0x10, 0xD0)
            for k, d in enumerate([0x11, 0x0F, 0x0C, 0x09, 0x06, 0x04, 0x03]):
                palette[(start + k) * 3 : (start + k) * 3 + 3] = bytes(
                    a + (d if c == 0 else 0) for c, a in enumerate(avg)
                )
        pairs = [(0, 1), (1, 0), (0x100, 0x201), (0, 0), (1, 1)] + random_pairs(rng, 20)
        lookups(krondor, native, bytes(palette), pairs)


def test_edge_entries(krondor, native):
    """The scan's ends: entry 10h and E6h match, entries 0Fh and E7h never do."""
    for match in (0x0F, 0x10, 0xE6, 0xE7):
        palette = bytearray([0x3F] * 768)
        palette[0:3] = bytes([0x10, 0x20, 0x30])
        palette[match * 3 : match * 3 + 3] = bytes([0x10, 0x20, 0x30])
        lookups(krondor, native, bytes(palette), [(0, 0), (match, match), (0, match)])
