"""DECOMP.ASM (the BMX images' LZSS and RLE codecs) against port/asm/decomp.c.

The codecs refill their buffer through the game's res_fread. On the original its entry is
patched with INT F1h + RETF, and the hook below serves the test's bytes; the translation
reads the same bytes through its refill hook. Both write into a 64 KB segment of their
own, and the bytes read, the buffer and the whole segment must agree.

The streams: LZSS flag bytes with literals and back-references (to written output or
anywhere in the segment, of every length), RLE literal and repeat runs; whole, cut short,
or shorter than the count the caller gives (the buffer's stale bytes are then decoded
too), through buffers from 1 byte to the game's 2500. RLE output also wraps at 64 KB.
LZSS moves words, and a word across the segment's end faults on a 286 and later (Unicorn
reads on past the segment instead), so its output and back-references stay clear of FFFFh,
also when stale bytes decode. With nothing to read LZSS takes a flag byte from the empty
buffer and reads on past it, through memory the two sides cannot share: a stream cut
short keeps a byte.
"""

from __future__ import annotations

import random
import struct

import pytest

SEGMENT = 0x10000
LZSS, RLE = 1, 2
BUF_SIZES = [1, 2, 3, 7, 30, 31, 32, 33, 100, 1250, 2500]


class Source:
    """res_fread(ptr, size, count, stream): what is left, up to size * count, into DS:ptr."""

    def __init__(self, data: bytes):
        self.data, self.pos = data, 0

    def read(self, m) -> None:
        ptr, size, count = struct.unpack("<HHH", m.read((m.reg("ss"), m.reg("sp") + 4), 6))
        chunk = self.data[self.pos : self.pos + size * count]
        m.uc.mem_write(m.linear(m.reg("ds"), ptr), chunk)
        self.pos += len(chunk)
        m.set_reg("ax", len(chunk) // size if size else 0)


def run_original(m, codec, off, remaining, buf, segment, source):
    m.free_all()
    seg = m.alloc(SEGMENT)
    m.write((seg, 0), segment)
    bufseg = m.alloc(len(buf) + 16)
    m.write((bufseg, 0), buf)
    rf_seg, rf_off = m.sym("res_fread")
    m.uc.mem_write(m.linear(rf_seg, rf_off), bytes([0xCD, 0xF1, 0xCB]))  # INT F1h; RETF
    src = Source(source)
    m.interrupts[0xF1] = src.read
    name = "decomp_lzss" if codec == LZSS else "decomp_rle"
    m.call_far(name, off, seg, remaining, 0, len(buf), 0x1234, ds=bufseg)
    return src.pos, m.read((bufseg, 0), len(buf)), m.read((seg, 0), SEGMENT)


def run_native(native, codec, off, remaining, buf, segment, source):
    payload = struct.pack("<BHHHI", codec, off, remaining, len(buf), len(source))
    reply = native.call("decomp.run", payload + source + buf + segment)
    (pos,) = struct.unpack_from("<I", reply)
    return pos, reply[4 : 4 + len(buf)], reply[4 + len(buf) :]


def check(m, native, codec, off, remaining, buf, segment, source, what):
    want = run_original(m, codec, off, remaining, buf, segment, source)
    got = run_native(native, codec, off, remaining, buf, segment, source)
    for name, w, g in zip(("bytes read", "buffer", "segment"), want, got):
        assert g == w, f"{name} differs: {what}"


def lzss_stream(rng, start: int, flags: int, low: bool = False) -> bytes:
    """Flag bytes with their literals and back-references. With low, every byte is below
    80h: decoded again from stale bytes, it still makes no offset near the segment's end."""
    top = 0x80 if low else 0x100
    limit = 0xC000 if low else 0xF000
    out = start
    data = bytearray()
    for _ in range(flags):
        flag = rng.choice((top - 1, 0x00, rng.randrange(top)))
        data.append(flag)
        for bit in range(8):
            if out > limit - 0x110:
                break
            if flag >> bit & 1:
                data.append(rng.randrange(top))
                out += 1
            else:
                length = rng.choice((0, 1, 2, top - 1, rng.randrange(top)))
                if out > start and rng.random() < 0.7:
                    src = rng.randrange(start, out)  # earlier output, overlapping too
                else:
                    src = rng.randrange(0, limit - 0x110)
                if low:
                    src &= 0x7F7F
                data += struct.pack("<HB", src, length)
                out += length + 5
    return bytes(data)


def rle_stream(rng, runs: int, end: bool = True) -> bytes:
    data = bytearray()
    for _ in range(runs):
        count = rng.choice((1, 2, 127, rng.randrange(1, 128)))
        if rng.random() < 0.5:
            data += bytes([0x80 | count, rng.randrange(256)])
        else:
            data.append(count)
            data += rng.randbytes(count)
    if end:
        data.append(rng.choice((0x00, 0x80)))
    return bytes(data)


@pytest.mark.parametrize("buf_size", BUF_SIZES)
def test_lzss(krondor, native, buf_size):
    rng = random.Random(buf_size)
    for case in range(12):
        off = rng.choice((0, 1, rng.randrange(0x400)))
        stale = case % 4 == 2
        stream = lzss_stream(rng, off, rng.choice((1, 2, 5, 40, 150)), low=stale)
        if case % 4 == 1:
            stream = stream[: rng.randrange(1, len(stream) + 1)]  # cut short
        remaining = len(stream)
        buf = rng.randbytes(buf_size)
        if stale:  # the caller's count runs past the data: the buffer's stale bytes decode
            remaining = min(len(stream) + rng.randrange(1, 40), 0xFFFF)
            buf = bytes(buf_size)
        check(
            krondor,
            native,
            LZSS,
            off,
            remaining,
            buf,
            rng.randbytes(SEGMENT),
            stream,
            f"LZSS case {case}, {len(stream)} bytes from offset {off:04X}",
        )


@pytest.mark.parametrize("buf_size", BUF_SIZES)
def test_rle(krondor, native, buf_size):
    rng = random.Random(100 + buf_size)
    for case in range(12):
        off = rng.choice((0, 1, rng.randrange(0x10000), 0xFFF0, 0xFFFF))  # wraps at 64 KB
        stream = rle_stream(rng, rng.choice((1, 3, 20, 400)), end=case % 3 != 0)
        if case % 4 == 1:
            stream = stream[: rng.randrange(len(stream) + 1)]
        remaining = len(stream)
        buf = rng.randbytes(buf_size)
        if case % 4 == 2:
            remaining = min(len(stream) + rng.randrange(1, 300), 0xFFFF)
        check(
            krondor,
            native,
            RLE,
            off,
            remaining,
            buf,
            rng.randbytes(SEGMENT),
            stream,
            f"RLE case {case}, {len(stream)} bytes at {off:04X}",
        )
