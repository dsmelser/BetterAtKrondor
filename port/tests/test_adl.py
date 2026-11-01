"""The SX.OVL "ADL:" driver chunk (DRIVERS/SX/ADL.ASM) against port/drivers/adl.c.

Both sides write the same OPL2 model (port/platform/opl.c): the address port latches the
register, each data write is recorded as a (register, value) pair, the status port reads 06h.
After each slot call the AX and CX it returns, the register writes and the whole chunk (the
driver keeps its state in it) must match. The calls also pass BX: the reset returns it in CX.

The bank is the driver's 5383 bytes: 190 patches of 28 bytes (two operators of 13, two
waveforms), then the 62 rhythm notes' pitches and a byte, which land on the tables after the
5320-byte patch buffer.

Channels stay within 0-15 except in queries, which only read: past 15 the per-channel arrays
reach into the voice tables, the age order and the operator tables, which the driver then
indexes its patches and registers by. Programs stay within the bank's 190 patches, and the
patches' total levels within 3Fh: the loudness divides a level times (3Fh less a total level) by
3Fh, which cannot overflow then. An overflow is the game's divide-error handler's to resolve
(the translation does it as the handler does), and this machine does not model the handler.
"""

from __future__ import annotations

import random
import struct

import pytest

from conftest import _require, ref_dir
from harness.machine import LOAD_SEG, Machine
from harness.mzexe import load_mz

BANK_SIZE = 0x1507
CC_NUMBERS = (0x07, 0x0A, 0x40, 0x4B, 0x7B, 0x01, 0x79, 0x00)


@pytest.fixture(scope="session")
def adl_image():
    return load_mz(_require(ref_dir() / "OUT" / "ADL.EXE", "reference ADL.EXE"))


class Opl:
    """The OPL2 of port/platform/opl.c at 388h/389h."""

    def __init__(self, m: Machine):
        self.address = 0
        self.writes: list[tuple[int, int]] = []
        m.port_in[0x388] = lambda m, port, size: 0x06
        m.port_out[0x388] = self.select
        m.port_out[0x389] = self.write

    def select(self, m, port, size, value):
        self.address = value & 0xFF

    def write(self, m, port, size, value):
        self.writes.append((self.address, value & 0xFF))


class Pair:
    def __init__(self, image, native):
        self.m = Machine(image)
        self.opl = Opl(self.m)
        self.native = native
        self.size = len(image.image)
        self.calls = 0
        self.writes = 0
        native.call("adl.load", image.image)

    def call(
        self, slot: int, ax: int = 0, cx: int = 0, bx: int = 0, bank: bytes | None = None
    ) -> tuple[int, int]:
        regs = {"bp": slot, "ax": ax, "bx": bx, "cx": cx}
        payload = struct.pack("<HHHH", slot, ax, bx, cx)
        if bank is not None:
            seg = self.m.alloc(len(bank))
            self.m.write((seg, 0), bank)
            regs.update(es=seg, ax=0)
            payload += bank
        self.opl.writes.clear()
        r = self.m.call_far((LOAD_SEG, 0), **regs)
        reply = self.native.call("adl.call", payload)
        ax_n, cx_n = struct.unpack_from("<HH", reply)
        what = f"slot {slot} AX={ax:04X} BX={bx:04X} CX={cx:04X}"
        assert list(zip(reply[4::2], reply[5::2])) == self.opl.writes, what
        assert (ax_n, cx_n) == (r.ax, r.cx), what
        assert self.native.call("adl.image") == self.m.read((LOAD_SEG, 0), self.size), what
        self.calls += 1
        self.writes += len(self.opl.writes)
        return r.ax, r.cx


def make_operator(rng: random.Random) -> bytes:
    """Key scale level, multiplier, feedback, attack, sustain level, sustaining, decay, release,
    total level, tremolo, vibrato, key scale rate, frequency modulation (0: additive)."""
    if rng.random() < 0.1:  # any bytes, the total level kept
        op = bytearray(rng.randbytes(13))
        op[8] = rng.randrange(0x40)
        return bytes(op)
    return bytes(
        (
            rng.randrange(4),
            rng.randrange(16),
            rng.randrange(8),
            rng.randrange(16),
            rng.randrange(16),
            rng.randrange(2),
            rng.randrange(16),
            rng.randrange(16),
            rng.choice((0, 0x3F, rng.randrange(0x40))),
            rng.randrange(2),
            rng.randrange(2),
            rng.randrange(2),
            rng.choice((0, 0, 1, rng.randrange(256))),
        )
    )


def make_bank(rng: random.Random) -> bytes:
    patches = b"".join(
        make_operator(rng) + make_operator(rng) + bytes((rng.randrange(256), rng.randrange(256)))
        for _ in range(190)
    )
    pitches = bytes(rng.choice((rng.randrange(0x20, 0x50), rng.randrange(256))) for _ in range(62))
    bank = patches + pitches + bytes((rng.randrange(256),))
    assert len(bank) == BANK_SIZE
    return bank


def random_call(rng: random.Random) -> tuple[int, int, int, int]:
    """(slot, AX, BX, CX): every slot, mostly notes and controllers."""
    slot = rng.choices(
        (0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
        (1, 1, 1, 14, 26, 1, 14, 5, 1, 6, 2, 3, 2, 1, 1, 1, 5),
    )[0]
    channel = rng.choice((9, 9, 0, 1, 2, 3, rng.randrange(16)))
    ah = rng.choice((0x00, 0x90, 0xB0, 0xC0, 0xE0, rng.randrange(256)))
    ch, cl = rng.randrange(256), rng.randrange(256)
    bx = rng.choice((0, rng.randrange(0x10000)))
    if slot in (4, 5):
        ch = rng.choice(
            (
                rng.randrange(0x0C, 0x6C),
                rng.randrange(0x24, 0x54),
                rng.randrange(0x30, 0x40),
                rng.randrange(256),
            )
        )
        if slot == 5:
            cl = rng.choice((0, rng.randrange(1, 0x80), rng.randrange(0x80), rng.randrange(256)))
    elif slot == 7:
        ch = rng.choice(CC_NUMBERS)
        if ch == 0x4B:
            cl = rng.choice((0, 1, 2, 3, 4, 9, 12, rng.randrange(16)))
        elif ch == 0x40:
            cl = rng.choice((0, 0x7F, rng.randrange(256)))
    elif slot == 8:
        cl = rng.choice((rng.randrange(0x80), rng.randrange(0x80), rng.randrange(0xBE)))
    elif slot == 10:  # CH the low 7 bits, CL the high: 0040h is the centre
        ch, cl = rng.choice(((0, 0x40), (0, 0), (0x7F, 0x7F), (1, 0x40), (0x7F, 0x3F), (ch, cl)))
    elif slot == 11:
        cl = rng.choice((0xFF, rng.randrange(256)))
    elif slot == 12:
        cl = rng.choice((0xFF, 0x0F, 0, rng.randrange(0x10), rng.randrange(256)))
    elif slot == 13:
        cl = rng.choice((0xFF, 0, 1, 1, rng.randrange(256)))
    elif slot == 17:
        channel = rng.randrange(256)
        ah = rng.choice((0xE0, 0xC0, 0xB0, 0xB0, rng.randrange(256)))
        ch = rng.choice((0x07, 0x0A, 0x40, 0x4B, rng.randrange(256)))
        cl = rng.choice((0xFF, rng.randrange(256)))
    return slot, ah << 8 | channel, bx, ch << 8 | cl


def run_random(pair: Pair, rng: random.Random, count: int) -> None:
    for _ in range(count):
        slot, ax, bx, cx = random_call(rng)
        pair.call(slot, ax, cx, bx)


def test_describe(adl_image, native):
    pair = Pair(adl_image, native)
    assert pair.call(0) == (0x0103, 0x0009)


def test_reset_swaps_bx_and_cx(adl_image, native):
    """The reset pushes BX then CX and pops them in the same order."""
    pair = Pair(adl_image, native)
    ax, cx = pair.call(2, 0x1111, 0x5678, 0x1234)
    assert cx == 0x1234
    assert pair.opl.writes[:3] == [(0, 0), (1, 0), (2, 0)] and pair.opl.writes[0xF6] == (1, 0x20)


def test_without_a_bank(adl_image, native):
    """Cold: the patches are the empty bank's zeros until a reset loads the default ones."""
    pair = Pair(adl_image, native)
    run_random(pair, random.Random(1), 800)
    assert pair.writes > 1000


@pytest.mark.parametrize("seed", [2, 3, 4])
def test_bank_and_messages(adl_image, native, seed):
    pair = Pair(adl_image, native)
    rng = random.Random(seed)
    assert pair.call(1, bank=make_bank(rng)) == (0x2446, 0x0800)
    run_random(pair, rng, 2000)
    assert pair.call(1, bank=make_bank(rng)) == (0x2446, 0x0800)  # another, mid-song
    run_random(pair, rng, 2000)
    assert pair.writes > 30000  # the comparison is not vacuous


def test_voices_stolen_and_reserved(adl_image, native):
    """More notes than voices, across channels with voices reserved (controller 75) and owed,
    the sustain pedal holding notes, then everything let go."""
    pair = Pair(adl_image, native)
    rng = random.Random(5)
    pair.call(1, bank=make_bank(rng))
    for channel, program in ((0, 3), (1, 40), (2, 77), (3, 12), (9, 0)):
        pair.call(8, channel, program)
    pair.call(7, 0, 0x4B03)  # channel 0: three voices
    pair.call(7, 1, 0x4B05)  # channel 1: five
    pair.call(7, 2, 0x4B04)  # channel 2: four, one owed
    pair.call(7, 0, 0x4000 | 0x7F)  # channel 0's pedal down
    for i in range(30):
        channel = (0, 1, 2, 3, 9)[i % 5]
        pair.call(5, channel, (0x30 + 3 * i) << 8 | 0x64)
        if i % 4 == 3:
            pair.call(4, channel, (0x30 + 3 * (i - 2)) << 8)
    pair.call(7, 1, 0x4B01)  # channel 1 gives back four: the owed one is paid
    pair.call(7, 3, 0x4B0C)  # channel 3 wants twelve
    pair.call(7, 0, 0x4000)  # the pedal up: the held notes go
    for channel in (0, 1, 2, 3, 9):
        pair.call(7, channel, 0x7B00)  # all notes off
        assert pair.call(17, 0xB000 | channel, 0x4BFF)[0] < 16
    assert pair.writes > 600
