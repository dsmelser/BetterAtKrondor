"""LZWDEC.ASM (stream codec 2) against port/asm/lzwdec.c, through the stream API.

Both sides set up a memory stream as stream_open_from_memory does (minus its heap
allocations, which need the C runtime's start-up), then read it in chunks of random
sizes with stream_read: the original's STREAM.C/CODEC.C and decoder in Unicorn, the
same C natively with the translated decoder. Every chunk and its delivered count must
match, including reads past the end and streams cut short.

The encoder below writes what the decoder expects: 9- to 12-bit codes, LSB-first, in
groups of as many bytes as the code width; a width change or a reset (code 100h) pads
the group, since the decoder discards the rest of it.
"""

from __future__ import annotations

import random
import struct

import pytest

RESET = 0x100
MAX_CODES = 0x1000
DESC = 0xA000  # spare DGROUP space for the stream descriptor and its ring buffer
RING = 0xA100
SCRATCH = 0x3C8C + 0x1000  # the game's codec scratch, plus the slack native blocks get


def max_code(width: int) -> int:
    return MAX_CODES if width == 12 else (1 << width) - 1


class CodeWriter:
    def __init__(self) -> None:
        self.out = bytearray()
        self.group: list[int] = []
        self.width = 9
        self.next_code = 0x101  # the decoder's NextCode

    def flush(self, pad: bool) -> None:
        if not self.group:
            return
        acc = 0
        for i, code in enumerate(self.group):
            acc |= code << (i * self.width)
        size = self.width if pad else (len(self.group) * self.width + 7) // 8
        self.out += acc.to_bytes(size, "little")
        self.group = []

    def emit(self, code: int, kind: str = "normal") -> None:
        if self.next_code > max_code(self.width):  # the decoder widens before reading
            self.flush(pad=True)
            self.width += 1
        self.group.append(code)
        if len(self.group) == 8:
            self.flush(pad=True)
        if kind == "reset":
            self.flush(pad=True)
            self.width = 9
            self.next_code = 0x100
        elif kind == "normal" and self.next_code < MAX_CODES:
            self.next_code += 1


def lzw_encode(data: bytes, reset_every: int | None = None) -> bytes:
    w = CodeWriter()
    table = {bytes([i]): i for i in range(256)}
    next_entry = 0x101
    kind = "seed"
    since_reset = 0
    s = b""
    for byte in data:
        c = bytes([byte])
        if s + c in table:
            s += c
            continue
        w.emit(table[s], kind)
        kind = "normal"
        if next_entry < MAX_CODES:
            table[s + c] = next_entry
            next_entry += 1
        since_reset += 1
        if reset_every and since_reset >= reset_every:
            w.emit(RESET, "reset")
            table = {bytes([i]): i for i in range(256)}
            next_entry = 0x101
            since_reset = 0
        s = c
    if s:
        w.emit(table[s], kind)
    w.flush(pad=False)
    return bytes(w.out)


def stream_bytes(data: bytes, payload: bytes) -> bytes:
    return bytes([2]) + struct.pack("<I", len(data)) + payload


def open_original(m, stream: bytes) -> int:
    m.free_all()
    scratch, src, dst = m.alloc(SCRATCH), m.alloc(len(stream)), m.alloc(0x10000)
    m.write((src, 0), stream)
    desc = struct.pack("<HHHHHIIIIBBIB", RING, 0, scratch, 0, src, 5, len(stream),
                       struct.unpack_from("<I", stream, 1)[0], 0, 0, 0, 0, stream[0] | 0x40)
    seg = m.data_seg
    m.write((seg, DESC), desc)
    m.write((seg, RING), bytes(0x80))
    m.set_u16("_g_apStreamSlots", DESC)
    assert m.call_near("stream_select", 0).ax == 1
    m.call_near("lzh_decoder_init")
    return dst


def read_both(m, native, dst: int, count: int, what: str) -> bytes:
    m.write((dst, 0), bytes(count))
    got = m.call_far("stream_read", 0, 0, dst, count).ax
    reply = native.call("lzw.read", struct.pack("<H", count))
    assert struct.unpack_from("<H", reply)[0] == got, f"delivered count, {what}"
    assert reply[2:] == m.read((dst, 0), count), f"data, {what}"
    return reply[2:2 + got]


def check(m, native, data: bytes, payload: bytes, rng: random.Random, name: str,
          complete: bool = True) -> None:
    stream = stream_bytes(data, payload)
    dst = open_original(m, stream)
    native.call("lzw.open", stream)
    total, n, decoded = 0, 0, bytearray()
    while total < len(data) + 50:
        count = rng.choice((1, 2, 3, 7, rng.randrange(1, 300), rng.randrange(1, 4000)))
        decoded += read_both(m, native, dst, count, f"{name}, read {n} ({count} bytes at {total})")
        total += count
        n += 1
    if complete:  # the decoder must give back what was encoded (checks the encoder too)
        assert bytes(decoded[:len(data)]) == data, f"{name}: decoded data differs from the input"


def _text(rng: random.Random, size: int) -> bytes:
    words = [bytes(rng.choice(b"abcdefgh ") for _ in range(rng.randrange(2, 9))) for _ in range(40)]
    out = bytearray()
    while len(out) < size:
        out += rng.choice(words)
    return bytes(out[:size])


CASES = {
    "random": lambda rng: rng.randbytes(6000),
    "text": lambda rng: _text(rng, 20000),
    "runs": lambda rng: b"".join(bytes([rng.randrange(4)]) * rng.randrange(1, 900)
                                 for _ in range(60)),
    "image-like": lambda rng: bytes((x // 7 + y // 5) & 0x0F for y in range(120)
                                    for x in range(160)),
}


@pytest.mark.parametrize("case", sorted(CASES))
def test_decode(krondor, native, case):
    rng = random.Random(case)
    data = CASES[case](rng)
    check(krondor, native, data, lzw_encode(data), rng, case)


def test_decode_with_resets(krondor, native):
    rng = random.Random(20)
    data = _text(rng, 15000)
    for every in (1, 2, 300, 700):
        check(krondor, native, data, lzw_encode(data, every), rng, f"reset every {every}")


def test_truncated(krondor, native):
    rng = random.Random(21)
    data = rng.randbytes(3000)
    payload = lzw_encode(data)
    for cut in (0, 1, 2, 11, len(payload) // 2, len(payload) - 1):
        check(krondor, native, data, payload[:cut], rng, f"cut at {cut}", complete=False)
