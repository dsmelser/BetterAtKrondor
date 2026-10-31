"""The SX.OVL "M32:" driver chunk (DRIVERS/SX/M32.ASM) against port/drivers/m32.c.

Both sides talk to the same MPU-401 model (port/platform/mpu401.c): always ready for
output, every command acknowledged with FEh, MIDI bytes passed on in UART mode. After
each slot call the AX and CX it returns, the MIDI bytes it sent and the whole chunk (the
driver keeps its state in it) must match.

Master volumes stay at 1Eh or below: above that, controller 7's scaling can overflow its
DIV, which the game's divide-error handler resolves and this machine does not model.
Velocity curves stay within the chunk (index 13 or below): past it the original reads
on into whatever follows the chunk in memory.
"""

from __future__ import annotations

import random
import struct

import pytest

from conftest import _require, ref_dir
from harness.machine import LOAD_SEG, Machine
from harness.mzexe import load_mz

CC_NUMBERS = (0x01, 0x07, 0x0A, 0x40, 0x7B, 0x4B, 0x00, 0x79)


@pytest.fixture(scope="session")
def m32_image():
    return load_mz(_require(ref_dir() / "OUT" / "M32.EXE", "reference M32.EXE"))


class Mpu:
    """The MPU-401 of port/platform/mpu401.c."""

    def __init__(self, m: Machine):
        self.uart = False
        self.ack = False
        self.midi = bytearray()
        m.port_in[0x331] = lambda m, port, size: 0x3F if self.ack else 0xBF
        m.port_in[0x330] = self.read_data
        m.port_out[0x331] = self.command
        m.port_out[0x330] = self.write_data

    def read_data(self, m, port, size):
        value = 0xFE if self.ack else 0xFF
        self.ack = False
        return value

    def command(self, m, port, size, value):
        if value == 0x3F:
            self.uart = True
        elif value == 0xFF:
            self.uart = False
        self.ack = True

    def write_data(self, m, port, size, value):
        if self.uart:
            self.midi.append(value)


class Pair:
    def __init__(self, image, native):
        self.m = Machine(image)
        self.mpu = Mpu(self.m)
        self.native = native
        self.size = len(image.image)
        native.call("m32.load", image.image)

    def call(self, slot: int, ax: int = 0, cx: int = 0, bank: bytes | None = None) -> tuple[int, int]:
        regs = {"bp": slot, "ax": ax, "cx": cx}
        payload = struct.pack("<HHH", slot, ax, cx)
        if bank is not None:
            seg = self.m.alloc(len(bank))
            self.m.write((seg, 0), bank)
            regs.update(es=seg, ax=0)
            payload += bank
        self.mpu.midi.clear()
        r = self.m.call_far((LOAD_SEG, 0), **regs)
        reply = self.native.call("m32.call", payload)
        ax_n, cx_n = struct.unpack_from("<HH", reply)
        what = f"slot {slot} AX={ax:04X} CX={cx:04X}"
        assert reply[4:] == bytes(self.mpu.midi), what
        assert (ax_n, cx_n) == (r.ax, r.cx), what
        assert self.native.call("m32.image") == self.m.read((LOAD_SEG, 0), self.size), what
        return r.ax, r.cx


def make_bank(rng: random.Random, messages: int) -> bytes:
    """Tables for 128 programs (some without a patch), then SysEx messages."""
    patch = bytes(0xFF if rng.random() < 0.15 else rng.randrange(0x80) for _ in range(128))
    transpose = bytes(rng.choice((0, 0, 12, 0xF4, rng.randrange(256))) for _ in range(128))
    volume = bytes(rng.choice((0, 0, 0x10, 0xF0, rng.randrange(256))) for _ in range(128))
    drums = bytes(0xFF if rng.random() < 0.3 else rng.randrange(0x80) for _ in range(128))
    curve_of = bytes(rng.choice((0, 0, 1, 3, rng.randrange(14))) for _ in range(128))
    curves = rng.randbytes(512)
    tables = patch + transpose + volume + drums + bytes([rng.randrange(256)]) + curve_of + curves
    assert len(tables) == 0x481
    sysex = b"".join(bytes([0xF0, 0x41, 0x10, 0x16, 0x12])
                     + bytes(rng.randrange(0x80) for _ in range(rng.randrange(3, 20)))
                     + b"\xF7" for _ in range(messages))
    return tables + struct.pack("<H", len(sysex)) + sysex


def random_call(rng: random.Random) -> tuple[int, int, int]:
    """Any slot but 2: its MPU reset leaves UART mode, after which nothing is sent."""
    slot = rng.choices((3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17),
                       (1, 12, 16, 1, 16, 8, 1, 6, 2, 3, 2, 1, 6))[0]
    channel = rng.choice((9, 9, 1, 2, 3, 8, 0, 15, rng.randrange(16), rng.randrange(256)))
    ah = rng.choice((0x00, 0xB0, 0xC0, 0xE0, rng.randrange(256)))
    ch, cl = rng.randrange(256), rng.randrange(256)
    if slot == 7:
        ch = rng.choice(CC_NUMBERS)
    elif slot == 12:
        cl = rng.choice((0xFF, 0, 1, 0x0C, 0x0F, rng.randrange(0x1F)))
    elif slot == 13:
        cl = rng.choice((0xFF, 1, 1, 1, 0, rng.randrange(256)))
    elif slot == 11:
        cl = rng.choice((0xFF, 0, 1, rng.randrange(256)))
    elif slot == 17:
        ch = rng.choice(CC_NUMBERS)
    elif slot in (4, 5, 8):
        cl = rng.choice((0, rng.randrange(0x80), rng.randrange(256)))
    return slot, ah << 8 | channel, ch << 8 | cl


def test_describe(m32_image, native):
    pair = Pair(m32_image, native)
    assert pair.call(0) == (0x0101, 0x0C20)


def test_without_a_bank(m32_image, native):
    """Cold, and the MPU-401 never put in UART mode: the state changes, nothing is sent.
    Every program's patch is FFh, so program changes only cut the channel's notes."""
    pair = Pair(m32_image, native)
    rng = random.Random(1)
    for _ in range(600):
        pair.call(*random_call(rng))


@pytest.mark.parametrize("seed", [2, 3])
def test_bank_and_messages(m32_image, native, seed):
    pair = Pair(m32_image, native)
    rng = random.Random(seed)
    assert pair.call(1, bank=make_bank(rng, 2)) == (0x0961, 0x0801)
    sent = 0
    for _ in range(2500):
        pair.call(*random_call(rng))
        sent += len(pair.mpu.midi)
    assert sent > 5000  # the comparison is not vacuous
    pair.call(2)  # reset: out of UART mode
    pair.call(5, 0x0001, 0x3C40)
    assert not pair.mpu.midi
