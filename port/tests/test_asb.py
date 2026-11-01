"""The SX.OVL "ASB:" driver chunk (DRIVERS/SX/ASB.ASM) against port/drivers/asb.c.

Both sides run against the same Sound Blaster: port/platform/sbdsp.c natively, a model of it
here (the DSP's reset, identification, version, time constant, single-cycle blocks, hold and
go on, speaker; the 8237's channel 1; the 8259's mask; an empty bus elsewhere). After every
command the AX it returns, every port access (reads and writes, in order), the arguments, the
whole chunk, the card's state and the handlers at the vectors of IRQ 0, 2, 3, 5 and 7 must
match. A block's interrupt comes where the test puts it: in the probe's wait (natively at its
first poll; in the original at the wait loop's first instruction) or between commands.

The original divides 1000000 by the rate with DIV CX, which overflows under 16 Hz and is then
resolved by the game's divide-error handler; Unicorn would turn the fault into a double fault,
so the DIV is swapped for INT F0h, which divides here by the handler's rule (both images get the
swap). Natively the driver does not hook INT 10h, 0Dh and 74h nor read DOS's InDOS flag (slot
0): they serve only the file streaming the game never sends, so bytes 8Ah-9Dh of the chunk (the
saved vectors and the InDOS pointers) are left out of the comparison, the hooks count as no
handler, and with the card on IRQ 5 so is 86h: the vector the original saves there is its own
INT 0Dh hook. Commands 9, 10, 11 and 13 trap natively and are not sent.

After a failed install the game shuts the driver down (SNDINST.C). Where the DSP cannot be
reached (none at all, or a base whose ports lose their carry) that waits forever for the DSP to
take a byte, in the original as natively: those cases stop at the install.
"""

from __future__ import annotations

import dataclasses
import random
import struct
from collections import deque

import pytest
from unicorn import UC_HOOK_CODE

from conftest import _require, ref_dir
from harness.machine import LOAD_SEG, Machine, MachineError
from harness.mzexe import MzImage, load_mz

DISPATCH = 0x08BF
PROBE_WAIT = 0x1653  # the probe's loop on the byte its handlers set
DIV_RATE = 0x0BC9  # DIV CX in the time constant's computation
CD_CHECK = 0x16FB
HOOKS = (0x13AD, 0x13C0, 0x13D3)  # the INT 10h, 0Dh and 74h hooks
BIOS_TIMER = (0xF000, 0xFEA5)  # vector 8 at start: not the driver's
BIOS_IRQ = (0xF000, 0xFF53)  # a handler on the IRQ's vector before the driver's
SUCCESS = 0x13E6
MASKED = range(0x8A, 0x9E)


@pytest.fixture(scope="session")
def asb_image():
    return load_mz(_require(ref_dir() / "OUT" / "ASB.EXE", "reference ASB.EXE"))


def patched(image: MzImage, cd_byte: int) -> bytes:
    data = bytearray(image.image)
    assert data[DIV_RATE : DIV_RATE + 2] == b"\xf7\xf1", "no DIV CX where the rate is divided"
    data[DIV_RATE : DIV_RATE + 2] = b"\xcd\xf0"
    assert data[CD_CHECK] == 0xDC
    data[CD_CHECK] = cd_byte
    return bytes(data)


class Card:
    """port/platform/sbdsp.c, as the driver sees it through the ports."""

    def __init__(self, present: bool, base: int, irq: int, version: int):
        self.present, self.base, self.irq, self.version = present, base, irq, version
        self.queue: deque[int] = deque()
        self.last = 0
        self.command = 0
        self.wanted = 0
        self.args: list[int] = []
        self.resetting = False
        self.tc = 0
        self.speaker = False
        self.active = self.halted = self.waiting = False
        self.length = 0
        self.flipflop = False
        self.masked = True
        self.mode = self.page = self.address = self.count = 0

    def is_dsp(self, port: int) -> bool:
        return port >= self.base and port - self.base in (6, 0x0A, 0x0C, 0x0E)

    def read(self, port: int) -> int:
        if port in (2, 3):
            raise AssertionError("the driver reads the DMA's registers")
        if not self.is_dsp(port) or not self.present:
            return 0xFF
        reg = port - self.base
        if reg == 0x0A:
            if self.queue:
                self.last = self.queue.popleft()
            return self.last
        if reg == 0x0C:
            return 0x7F
        if reg == 0x0E:
            return 0xFF if self.queue else 0x7F
        return 0xFF

    def write(self, port: int, value: int) -> None:
        if port in (2, 3):
            reg = "address" if port == 2 else "count"
            old = getattr(self, reg)
            new = (old & 0x00FF) | value << 8 if self.flipflop else (old & 0xFF00) | value
            setattr(self, reg, new)
            self.flipflop = not self.flipflop
        elif port == 0x0A:
            if value & 3 == 1:
                self.masked = bool(value & 4)
                if not self.masked and self.waiting:
                    self.waiting = self.halted = False
        elif port == 0x0B:
            if value & 3 == 1:
                self.mode = value
        elif port == 0x0C:
            self.flipflop = False
        elif port == 0x83:
            self.page = value
        elif self.is_dsp(port) and self.present:
            if port - self.base == 6:
                if value & 1:
                    self.resetting = True
                elif self.resetting:
                    self.resetting = False
                    self.reset()
            elif port - self.base == 0x0C:
                self.write_byte(value)

    def reset(self) -> None:
        self.active = self.halted = self.waiting = False
        self.queue.clear()
        self.wanted = 0
        self.speaker = False
        self.queue.append(0xAA)

    def write_byte(self, v: int) -> None:
        if self.wanted:
            self.args.append(v)
            self.wanted -= 1
            if not self.wanted:
                self.run(self.command, self.args)
            return
        self.command, self.args = v, []
        if v in (0x10, 0x40, 0xE0):
            self.wanted = 1
        elif v in (0x14, 0x48):
            self.wanted = 2
        else:
            self.run(v, [])

    def run(self, c: int, a: list[int]) -> None:
        if c == 0x14:
            self.active = True
            self.length = (a[0] | a[1] << 8) + 1
            if self.masked:
                self.waiting = True
            else:
                self.waiting = self.halted = False
        elif c == 0x40:
            self.tc = a[0]
        elif c == 0xD0:
            if self.active and not self.halted and not self.waiting:
                self.halted = True
        elif c == 0xD4:
            if self.active and self.halted:
                self.halted = False
        elif c in (0xD1, 0xD3):
            self.speaker = c == 0xD1
        elif c == 0xE0:
            if len(self.queue) < 8:
                self.queue.append(~a[0] & 0xFF)
        elif c == 0xE1:
            for b in (self.version >> 8, self.version & 0xFF):
                if len(self.queue) < 8:
                    self.queue.append(b)

    def finish(self) -> bool:
        if not self.active or self.halted or self.waiting:
            return False
        self.active = False
        return True

    def state(self) -> bytes:
        return struct.pack(
            "<BBBBIBBHHB",
            self.tc,
            self.speaker,
            self.active,
            self.halted,
            self.length,
            self.mode,
            self.page,
            self.address,
            self.count,
            self.masked,
        )


class Pair:
    def __init__(
        self,
        image: MzImage,
        native,
        *,
        present=True,
        base=0x220,
        irq=7,
        version=0x0201,
        cd: int | None = None,
        cd_byte=0xDC,
        mask=0xB8,
        chained=False,
    ):
        data = patched(image, cd_byte)
        self.m = m = Machine(dataclasses.replace(image, image=data))
        self.native = native
        self.card = Card(present, base, irq, version)
        self.mask = mask
        self.cd = cd
        self.trace: list[tuple[int, int, int]] = []
        self.irqs = 0
        self.stack = m.alloc(0x10000)
        for port in list(range(0x200, 0x400)) + [2, 3, 0x0A, 0x0B, 0x0C, 0x83, 0x20, 0x21]:
            m.port_in[port] = self._in
            m.port_out[port] = self._out
        m.interrupts[0x21] = self._dos
        m.interrupts[0x2F] = self._multiplex
        m.interrupts[0xF0] = self._div_cx
        m.set_vector(8, *BIOS_TIMER)
        at = Machine.linear(LOAD_SEG, PROBE_WAIT)
        m.uc.hook_add(UC_HOOK_CODE, self._probe_wait, None, at, at)
        native.call("asb.load", data)
        native.call("asb.config", struct.pack("<BHBHB", present, base, irq, version, mask))
        if cd is not None:
            native.call("asb.cd", struct.pack("<b", cd))
        self.chained = chained  # the IRQ's vector holds another handler before the driver's
        self.probe5 = False
        if chained:
            m.set_vector(8 + irq, *BIOS_IRQ)
            native.call("asb.vector", bytes([8 + irq]))
        self.size = len(data)

    # ---- the model's hardware ----------------------------------------------------------------

    def _in(self, m, port, size):
        value = self.mask if port == 0x21 else self.card.read(port)
        self.trace.append((port, value, 0))
        return value

    def _out(self, m, port, size, value):
        self.trace.append((port, value, 1))
        if port == 0x21:
            self.mask = value
        elif port != 0x20:
            self.card.write(port, value)

    def _deliverable(self) -> bool:
        return not self.mask >> self.card.irq & 1 and self.m.vector(8 + self.card.irq) != (0, 0)

    def _probe_wait(self, uc, address, size, _user):
        if self.card.finish() and self._deliverable():
            self.irqs += 1
            self.m.deliver_through_vector(8 + self.card.irq)

    def _dos(self, m):
        ah, al = m.reg("ax") >> 8, m.reg("ax") & 0xFF
        if ah == 0x35:
            seg, off = m.vector(al)
            m.set_reg("es", seg)
            m.set_reg("bx", off)
        elif ah == 0x25:
            m.set_vector(al, m.reg("ds"), m.reg("dx"))
        elif ah == 0x34:
            m.set_reg("es", 0x0070)
            m.set_reg("bx", 0x0100)
        elif ah == 0x19:
            m.set_reg("ax", (m.reg("ax") & 0xFF00) | 2)  # the game runs from C:
        elif ah == 0x3E:
            m.set_reg("flags", m.reg("flags") & ~1)
        else:
            raise MachineError(f"INT 21h AH={ah:02X}h")

    def _multiplex(self, m):
        if m.reg("ax") == 0x150B and self.cd is not None:
            m.set_reg("bx", 0xADAD)
            m.set_reg("ax", 0x5AD8 if m.reg("cx") == self.cd else 0)

    def _div_cx(self, m):
        """DIV CX, an overflow resolved as the game's divide-error handler does: DX:AX halved
        (arithmetically) until the quotient fits."""
        dxax, divisor = m.reg("dx") << 16 | m.reg("ax"), m.reg("cx")
        assert divisor, "a rate of 0 Hz hangs the original"
        while dxax // divisor > 0xFFFF:
            dxax = dxax >> 1 | dxax & 0x80000000
        m.set_reg("ax", dxax // divisor)
        m.set_reg("dx", dxax % divisor)

    # ---- comparing ----------------------------------------------------------------------------

    def _vector(self, n: int) -> int:
        seg, off = self.m.vector(n)
        if (seg, off) == (0, 0) or (seg == LOAD_SEG and off in HOOKS):
            return 0
        return off if seg == LOAD_SEG else -1

    def compare(self, what: str) -> None:
        image = bytearray(self.m.read((LOAD_SEG, 0), self.size))
        mine = bytearray(self.native.call("asb.image"))
        masked = list(MASKED) + (list(range(0x86, 0x8A)) if self.card.irq == 5 else [])
        if self.probe5:
            masked += list(range(0x15DB, 0x15DF))
        if self.chained:  # the saved vectors hold that handler: a host pointer natively
            slot = 0x15D3 + 4 * (2, 3, 5, 7).index(self.card.irq)
            masked += list(range(0x86, 0x8A)) + list(range(slot, slot + 4))
        for i in masked:
            image[i] = mine[i] = 0
        assert mine == image, f"{what}: the chunk"
        state = self.native.call("asb.state")
        vectors = [self._vector(n) for n in (8, 0x0A, 0x0B, 0x0D, 0x0F)]
        assert state[0] == self.mask, f"{what}: the 8259's mask"
        assert list(struct.unpack_from("<5i", state, 1)) == vectors, f"{what}: the vectors"
        assert state[21:] == self.card.state(), f"{what}: the card"

    def _native_trace(self, reply: bytes, at: int) -> tuple[list[tuple[int, int, int]], int]:
        (n,) = struct.unpack_from("<I", reply, at)
        at += 4
        entries = [struct.unpack_from("<HBB", reply, at + 4 * i) for i in range(n)]
        return [(p, v, w) for p, v, w in entries], at + 4 * n

    def call(self, cmd: int, *words: int) -> int:
        args = struct.pack(f"<{len(words)}H", *words)
        self.m.write((self.stack, 0), args)
        self.trace = []
        r = self.m.call_far((LOAD_SEG, DISPATCH), ax=cmd, si=0, ss=self.stack, sp=0xFFF0)
        reply = self.native.call("asb.call", struct.pack("<HH", cmd, len(args)) + args)
        what = f"command {cmd} {words}"
        ax, after = struct.unpack_from("<H", reply)[0], reply[2 : 2 + len(args)]
        trace, _ = self._native_trace(reply, 2 + len(args))
        assert trace == self.trace, f"{what}: the ports"
        assert ax == r.ax, f"{what}: AX {ax:04X}, the original {r.ax:04X}"
        assert after == self.m.read((self.stack, 0), len(args)), f"{what}: the arguments"
        self.compare(what)
        return r.ax

    def irq(self) -> bool:
        """The block under way ends now, on both sides."""
        self.trace = []
        ended = self.card.finish()
        if ended and self._deliverable():
            self.irqs += 1
            self.m.call_interrupt(self.m.vector(8 + self.card.irq), ss=self.stack, sp=0xFFF0)
        reply = self.native.call("asb.irq")
        trace, _ = self._native_trace(reply, 1)
        assert reply[0] == ended, "whether a block was under way"
        assert trace == self.trace, "the interrupt's ports"
        self.compare("the interrupt")
        return ended


def test_install(asb_image, fresh_native):
    pair = Pair(asb_image, fresh_native)
    assert pair.call(0, 0, 1) == SUCCESS
    assert pair.irqs == 1  # the probe's
    assert pair.call(4, 0) == 0x0001  # nothing plays
    assert pair.call(7) == LOAD_SEG and pair.call(15) == LOAD_SEG  # a bare RET: CS
    assert pair.call(14) == 0 and pair.call(8) == 0 and pair.call(1) == 0
    pair.call(2)


PROBES = {
    "absent": dict(present=False),
    "irq2": dict(irq=2),
    "irq3": dict(irq=3, mask=0xF8),
    "irq5": dict(irq=5),
    "irq4": dict(irq=4),  # not a line the probe tries: no interrupt, no install
    "dsp101": dict(version=0x0101),  # the oldest it takes
    "dsp105": dict(version=0x0105),
    "dsp200": dict(version=0x0200),  # auto-init blocks from here on
    "dsp100": dict(version=0x0100),  # too old
    "dsp8001": dict(version=0x8001),  # "too old": the comparison is signed
    "base240": dict(base=0x240),
    "base2f8": dict(base=0x2F8),  # some ports are base + reg in DL alone: the carry is lost
    "base2f2": dict(base=0x2F2),  # and read data is that less 4 in DL: here it is the card's
    "cd-none": dict(cd_byte=0xCD),
    "cd-elsewhere": dict(cd_byte=0xCD, cd=3),
    "cd-here": dict(cd_byte=0xCD, cd=2),
    "cd-byte00": dict(cd_byte=0x00),  # not CDh: no check
}


@pytest.mark.parametrize("name", sorted(PROBES))
def test_probe(asb_image, fresh_native, name):
    config = PROBES[name]
    pair = Pair(asb_image, fresh_native, **config)
    ax = pair.call(0, config.get("base", 0x220) if name.startswith("base") else 0, 1)
    if name not in ("absent", "base2f8"):
        pair.call(2)  # what the game does next, success or not
    assert (ax == SUCCESS) == (
        name
        in (
            "irq2",
            "irq3",
            "irq5",
            "dsp101",
            "dsp105",
            "dsp200",
            "base240",
            "base2f2",
            "cd-here",
            "cd-byte00",
        )
    )


def test_install_again(asb_image, fresh_native):
    """A second install, the card moved to IRQ 4 meanwhile: its probe must not take the line the
    first found for an answer. (The original's INT 0Dh hook is then the vector its probe saves for
    IRQ 5.)"""
    pair = Pair(asb_image, fresh_native)
    assert pair.call(0, 0, 1) == SUCCESS
    pair.card = Card(True, 0x220, 4, 0x0201)
    pair.native.call("asb.config", struct.pack("<BHBHB", True, 0x220, 4, 0x0201, pair.mask))
    pair.probe5 = True
    assert pair.call(0, 0, 1) == 0


def test_vector_given_back(asb_image, fresh_native):
    """A handler on the IRQ's vector before the driver: the probe and each block hook over it,
    and stopping gives it back."""
    pair = Pair(asb_image, fresh_native, chained=True)
    assert pair.call(0, 0, 1) == SUCCESS
    for loop in (0, 0x100, 0):
        pair.call(3, loop, 11025, 0x0010, 0x6000, 300)
        pair.irq()
        pair.call(4, loop)
    pair.call(5)
    pair.call(2)


def block(rng: random.Random) -> tuple[int, int, int, int]:
    """A rate, a far pointer (paragraph seg, offset) and a count, some across a 64 KB page."""
    rate = rng.choice(
        (11025, 22050, 8000, 16, 4000, rng.randrange(16, 45000), rng.randrange(1, 16))
    )
    count = rng.choice((1, 2, rng.randrange(1, 0x400), rng.randrange(1, 0x8000), 0))
    seg = rng.randrange(0x6000, 0x9000)
    off = (
        rng.randrange(0x10000 - (count or 0x10000) + 1)
        if rng.random() < 0.7
        else rng.randrange(0x10000 - 16)
    )
    if rng.random() < 0.25:  # to the 64 KB page's end, or across it
        linear = (seg << 4) + off
        page_end = (linear | 0xFFFF) + 1
        off = (off + page_end - linear - count + rng.choice((0, 0, 1, 7, -1))) & 0xFFFF
    return rate, seg, off, count


@pytest.mark.parametrize("seed,irq", [(1, 7), (2, 7), (3, 5), (4, 2)])
def test_playing(asb_image, fresh_native, seed, irq):
    rng = random.Random(seed)
    pair = Pair(asb_image, fresh_native, irq=irq)
    assert pair.call(0, 0, 1) == SUCCESS
    played = ends = crossed = restarts = 0
    for _ in range(800):
        what = rng.choices(("play", "irq", "poll", "stop", "rate", "other"), (6, 8, 6, 2, 1, 2))[0]
        if what == "play":
            rate, seg, off, count = block(rng)
            loop = rng.choice((0, 0, 1)) << 8
            pair.call(3, loop | rng.randrange(256), rate, off, seg, count)
            played += 1
            crossed += ((seg << 4) + off & 0xFFFF) + (count or 0x10000) > 0x10000
        elif what == "irq":
            ends += pair.irq()
        elif what == "poll":
            restarts += pair.call(4, rng.choice((0, 0x100, rng.randrange(0x10000)))) >> 8
        elif what == "stop":
            pair.call(rng.choice((5, 12)))
        elif what == "rate":
            pair.call(6, rng.choice((11025, rng.randrange(16, 45000))))
        else:
            pair.call(rng.choice((1, 7, 8, 14, 15)))
    pair.call(2)
    assert played > 150 and ends > 100  # the comparison is not vacuous
    assert crossed > 20 and restarts > 5
