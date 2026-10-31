"""DOSRW.ASM (a long transfer on a DOS handle, in calls of 7FFFh bytes) against
port/asm/dosrw.c.

Both sides talk to the same DOS model (its twin is in difftest/t_dosrw.c): one handle on a
file in memory, INT 21h AH=3Fh reads, 40h writes (0 bytes: the file ends at the position),
and a call that fails, or moves fewer bytes, when the case says so. The result, every call
(function, handle, count, where in the buffer), the file, its position and the buffer must
agree. The original normalizes its buffer pointer before each call, so the cases split the
same address between segment and offset in different ways.
"""

from __future__ import annotations

import random
import struct
from dataclasses import dataclass

import pytest

CHUNK = 0x7FFF
READ, WRITE = 0x3F, 0x40
EDGE_COUNTS = [
    0,
    1,
    2,
    CHUNK - 1,
    CHUNK,
    CHUNK + 1,
    0xFFFE,
    0xFFFF,
    0x10000,
    0x10001,
    2 * CHUNK - 1,
    2 * CHUNK,
    2 * CHUNK + 1,
    3 * CHUNK,
    3 * CHUNK + 5,
]


@dataclass
class Case:
    function: int
    handle: int
    count: int  # the routine's long, as a u32
    file_handle: int
    pos: int
    file: bytes
    buf: bytes
    fail_call: int = -1
    short_call: int = -1
    short_by: int = 0
    split: int = 0  # paragraphs moved from the pointer's segment into its offset
    phase: int = 0  # the buffer's offset in its paragraph


class DosModel:
    def __init__(self, case: Case, buf_linear: int):
        self.case = case
        self.data = bytearray(case.file)
        self.pos = case.pos
        self.buf_linear = buf_linear
        self.calls: list[tuple[int, int, int, int]] = []

    def int21(self, m) -> None:
        c = self.case
        ah, bx, cx = m.reg("ax") >> 8, m.reg("bx"), m.reg("cx")
        at = m.linear(m.reg("ds"), m.reg("dx"))
        if ah not in (READ, WRITE):
            m._fail(f"unmodelled INT 21h AH={ah:02X}h")
            return
        k = len(self.calls)
        self.calls.append((ah, bx, cx, at - self.buf_linear))
        if bx != c.file_handle or k == c.fail_call:
            m.set_reg("ax", 6)  # invalid handle
            m.set_reg("flags", m.reg("flags") | 1)
            return
        n = cx
        if k == c.short_call:
            n = max(n - c.short_by, 0)
        if ah == READ:
            n = min(n, max(len(self.data) - self.pos, 0))
            m.uc.mem_write(at, bytes(self.data[self.pos : self.pos + n]))
        elif cx == 0:
            n = 0
            if len(self.data) > self.pos:
                del self.data[self.pos :]
            else:
                self.data.extend(bytes(self.pos - len(self.data)))
        else:
            if self.pos > len(self.data):
                self.data.extend(bytes(self.pos - len(self.data)))
            self.data[self.pos : self.pos + n] = bytes(m.uc.mem_read(at, n))
        self.pos += n
        m.set_reg("ax", n)
        m.set_reg("flags", m.reg("flags") & ~1)


def run_original(m, case: Case):
    m.free_all()
    seg = m.alloc(len(case.buf) + 16)
    linear = m.linear(seg, case.phase)
    m.uc.mem_write(linear, case.buf)
    model = DosModel(case, linear)
    m.interrupts[0x21] = model.int21
    name = "dos_read" if case.function == READ else "dos_write"
    regs = m.call_far(
        name,
        16 * case.split + case.phase,
        seg - case.split,
        case.count & 0xFFFF,
        case.count >> 16,
        case.handle,
    )
    buf = bytes(m.uc.mem_read(linear, len(case.buf)))
    return regs.dx_ax, model.pos, bytes(model.data), buf, model.calls


def run_native(native, case: Case):
    payload = struct.pack(
        "<BHIHIiiHI",
        case.function,
        case.handle,
        case.count,
        case.file_handle,
        case.pos,
        case.fail_call,
        case.short_call,
        case.short_by,
        len(case.file),
    )
    payload += case.file + struct.pack("<I", len(case.buf)) + case.buf
    reply = native.call("dosrw.run", payload)
    result, pos, size = struct.unpack_from("<III", reply)
    at = 12
    data = reply[at : at + size]
    at += size
    buf = reply[at : at + len(case.buf)]
    at += len(case.buf)
    calls = [struct.unpack_from("<BHHI", reply, i) for i in range(at, len(reply), 9)]
    return result, pos, data, buf, calls


def check(m, native, case: Case) -> None:
    want = run_original(m, case)
    got = run_native(native, case)
    for what, w, g in zip(("result", "position", "file", "buffer", "calls"), want, got):
        assert g == w, f"{what} differs for {case}"


def make(rng, function, count, *, file_size=None, pos=0, buf_size=None, **kw) -> Case:
    handle = kw.pop("handle", 5)
    if file_size is None:
        file_size = pos + count if function == READ else rng.randrange(0x100)
    buf_size = count if buf_size is None else buf_size
    return Case(
        function,
        handle,
        count,
        kw.pop("file_handle", handle),
        pos,
        rng.randbytes(file_size),
        rng.randbytes(buf_size),
        split=kw.pop("split", rng.randrange(0x1000)),
        phase=kw.pop("phase", rng.randrange(16)),
        **kw,
    )


@pytest.mark.parametrize("function", [READ, WRITE])
def test_counts_around_a_call(krondor, native, function):
    rng = random.Random(function)
    for count in EDGE_COUNTS:
        for pos in (0, 7):
            check(krondor, native, make(rng, function, count, pos=pos))


def test_reads_reaching_the_end_of_the_file(krondor, native):
    rng = random.Random(1)
    for count in EDGE_COUNTS[1:]:
        for missing in sorted({1, 2, CHUNK - 1, CHUNK, CHUNK + 1, count // 2, count} - {0}):
            if missing <= count:
                check(krondor, native, make(rng, READ, count, file_size=count - missing))


@pytest.mark.parametrize("function", [READ, WRITE])
def test_failing_and_short_calls(krondor, native, function):
    rng = random.Random(10 + function)
    for count in (1, CHUNK, CHUNK + 1, 2 * CHUNK + 3, 3 * CHUNK + 5):
        for k in range(4):
            check(krondor, native, make(rng, function, count, fail_call=k))
            for short_by in (1, 2, CHUNK):
                check(krondor, native, make(rng, function, count, short_call=k, short_by=short_by))
        check(krondor, native, make(rng, function, count, handle=5, file_handle=6))


def test_zero_bytes(krondor, native):
    # A read of nothing succeeds; a write of nothing ends the file at the position (cut, or
    # extended with zeros).
    rng = random.Random(2)
    for function in (READ, WRITE):
        for file_size, pos in ((0, 0), (100, 0), (100, 40), (100, 100), (100, 160)):
            check(krondor, native, make(rng, function, 0, file_size=file_size, pos=pos))


@pytest.mark.parametrize("function", [READ, WRITE])
def test_counts_past_2_gb(krondor, native, function):
    # The count is unsigned: a negative long runs calls until one fails.
    rng = random.Random(20 + function)
    for count in (0x80000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x12345678, 0x00010000 | 0x80000000):
        for k in range(4):
            buf_size = (k + 1) * CHUNK
            check(
                krondor,
                native,
                make(rng, function, count, file_size=4 * CHUNK, buf_size=buf_size, fail_call=k),
            )


def test_random(krondor, native):
    rng = random.Random(1993)
    for _ in range(300):
        function = rng.choice((READ, WRITE))
        count = rng.choice((rng.randrange(0x100), rng.randrange(0x20000), rng.choice(EDGE_COUNTS)))
        pos = rng.choice((0, rng.randrange(0x400)))
        file_size = rng.choice((None, rng.randrange(pos + count + 1)))
        kw = {}
        if rng.random() < 0.3:
            kw["fail_call"] = rng.randrange(4)
        if rng.random() < 0.3:
            kw["short_call"], kw["short_by"] = rng.randrange(4), rng.randrange(1, 0x8000)
        if rng.random() < 0.1:
            kw["file_handle"] = rng.randrange(20)
        check(
            krondor,
            native,
            make(
                rng, function, count, file_size=file_size, pos=pos, handle=rng.randrange(20), **kw
            ),
        )


def test_host_files(native, tmp_path):
    # Natively the calls go to the runtime's handle functions on a real file, which must do
    # what the model does: reads stop at the end of the file, a write of nothing cuts or
    # extends the file at the position.
    rng = random.Random(3)
    steps = [
        ("W", rng.randbytes(0x12345)),
        ("S", 0x100),
        ("R", 0x10000),
        ("S", 0x11000),
        ("R", 0x2000),
        ("S", 0x5000),
        ("W", b""),
        ("S", 0x6000),
        ("W", b""),
        ("S", 0),
        ("R", 0x7000),
        ("S", 0x5800),
        ("W", rng.randbytes(CHUNK + 3)),
        ("R", 0),
        ("S", 0x9000),
        ("W", b""),
        ("R", 1),
    ]
    data, pos, want = bytearray(), 0, []
    payload = struct.pack("<H", len(str(tmp_path / "f"))) + str(tmp_path / "f").encode()
    for kind, arg in steps:
        if kind == "S":
            payload += b"S" + struct.pack("<I", arg)
            pos = arg
            want.append(struct.pack("<I", pos))
        elif kind == "W":
            payload += b"W" + struct.pack("<I", len(arg)) + arg
            if arg:
                data.extend(bytes(max(pos - len(data), 0)))
                data[pos : pos + len(arg)] = arg
                pos += len(arg)
            elif len(data) > pos:
                del data[pos:]
            else:
                data.extend(bytes(pos - len(data)))
            want.append(struct.pack("<I", len(arg)))
        else:
            payload += b"R" + struct.pack("<I", arg)
            moved = data[pos : pos + arg]
            pos += len(moved)
            want.append(struct.pack("<I", arg if len(moved) == arg else 0))
            want.append(bytes(moved) + bytes(arg - len(moved)))
    want.append(struct.pack("<II", pos, len(data)) + bytes(data))
    assert native.call("dosrw.host", payload) == b"".join(want)
