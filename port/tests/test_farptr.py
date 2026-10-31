"""FARPTR.ASM (the far-pointer runtime) against port/asm/farptr.c.

Buffers sit at seg:0000 in the original's machine and in a DOS block natively, so the
offsets agree, and so does the address parity the copy routines look at.
"""

from __future__ import annotations

import random
import struct


def far(seg: int, offset: int) -> tuple[int, int]:
    """(offset, segment) of byte `offset` of the block at seg:0000, normalized."""
    return offset & 15, seg + (offset >> 4)


def load(m, data: bytes) -> int:
    m.free_all()
    seg = m.alloc(len(data))
    m.write((seg, 0), data)
    return seg


def test_memset(krondor, native):
    m, rng = krondor, random.Random(13)
    for i in range(40):
        size = rng.choice((64, 300, 0x9000, 0x12000))
        data = rng.randbytes(size)
        at = rng.randrange(0, 40)
        count = rng.choice((0, 1, 2, 9, 10, 11, rng.randrange(0, size - at)))
        value = rng.randrange(0x10000)
        seg = load(m, data)
        m.call_far("memset_far", *far(seg, at), value, count & 0xFFFF, count >> 16)
        want = native.call("farptr.memset", struct.pack("<I", size) + data +
                           struct.pack("<IHI", at, value, count))
        assert want == m.read((seg, 0), size), f"memset {i}: at {at} count {count}"


def test_memcpy_overlapping(krondor, native):
    m, rng = krondor, random.Random(14)
    for i in range(300):
        data = rng.randbytes(256)
        src = rng.randrange(16, 120)
        dst = src + rng.choice((-3, -2, -1, 0, 1, 2, 3, rng.randrange(-16, 100)))
        count = rng.randrange(0, 256 - max(src, dst))
        seg = load(m, data)
        m.call_far("fmemcpy_far", *far(seg, dst), *far(seg, src), count)
        want = native.call("farptr.memcpy", struct.pack("<I", len(data)) + data +
                           struct.pack("<IIH", dst, src, count))
        assert want == m.read((seg, 0), len(data)), f"memcpy {i}: dst {dst} src {src} n {count}"


def test_memmove(krondor, native):
    m, rng = krondor, random.Random(15)
    for i in range(40):
        size = rng.choice((512, 0x9000, 0x14000))
        data = rng.randbytes(size)
        src = rng.randrange(0, size // 3)
        dst = max(0, src + rng.choice((-1, 1, 2, -7, rng.randrange(-src, size // 3))))
        count = rng.randrange(0, size - max(src, dst))
        seg = load(m, data)
        m.call_far("fmemmove", *far(seg, dst), *far(seg, src), count & 0xFFFF, count >> 16)
        want = native.call("farptr.memmove", struct.pack("<I", size) + data +
                           struct.pack("<III", dst, src, count))
        assert want == m.read((seg, 0), size), f"memmove {i}: dst {dst} src {src} n {count}"


def test_compare_and_normalize(krondor, native):
    m, rng = krondor, random.Random(16)
    base = 0x5000
    for _ in range(500):
        a, b = rng.randrange(0x40000), rng.randrange(0x40000)
        if rng.random() < 0.2:
            b = a
        # The same addresses written with arbitrary segment/offset splits.
        sa = base + (a >> 4) - rng.randrange(0, min(0x0FFF, a >> 4) + 1)
        sb = base + (b >> 4) - rng.randrange(0, min(0x0FFF, b >> 4) + 1)
        oa, ob = a + (base << 4) - (sa << 4), b + (base << 4) - (sb << 4)
        got = m.call_far("far_ptr_cmp", oa, sa, ob, sb).ax
        want = struct.unpack("<H", native.call("farptr.cmp", struct.pack("<II", a, b)))[0]
        assert want == got, (hex(a), hex(b))
        r = m.call_far("normalize_far_ptr_thunk", oa, sa)
        assert r.ax < 16 and (r.dx << 4) + r.ax == (sa << 4) + oa
