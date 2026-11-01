"""VGAFILL.ASM, the sky's flat bands, against port/asm/vgafill.c.

Both sides write through the graphics controller of a planar VGA model, as the CPU does,
into the page the graphics context names. The registers start scrambled as often as not
(the routines use whatever write mode, bit mask and latches they meet); afterwards display
memory, in the port's chunky layout, and every VGA register must match.
"""

from __future__ import annotations

import random
import struct

from harness.machine import Machine
from harness.vga import PlanarVga

CTX_DST = 0x18  # wGfxBlitDstPage
PAGES = (0xA000, 0xA400, 0xA800, 0xAC00)
SEQ, GC = 0, 40  # offsets of the sequencer and graphics-controller registers in a snapshot
LATCHES = 95


class Pair:
    def __init__(self, image, native, rng: random.Random):
        self.m = Machine(image, vga=True)
        self.vga = PlanarVga(self.m)
        self.n = native
        vram = rng.randbytes(4 * 0x10000)
        self.vga.load_chunky(vram)
        native.poke("g_bakVram", vram)
        self.ctx = self.m.sym("_g_graphics_context")
        self.block = bytearray(native.peek("g_gfxContextBlock"))

    def page(self, seg: int) -> None:
        self.m.write((self.ctx[0], self.ctx[1] + CTX_DST), struct.pack("<H", seg))
        self.block[CTX_DST : CTX_DST + 2] = struct.pack("<H", seg)
        self.n.poke("g_gfxContextBlock", bytes(self.block))

    def registers(self, rng: random.Random) -> None:
        """Mode Y's (write mode 0, all planes, no set/reset) or scrambled ones: set/reset,
        its enable, the rotation and logical operation, the write mode, the bit mask, the
        map mask and the latches."""
        regs = bytearray(self.vga.regs())
        regs[GC + 0], regs[GC + 1], regs[GC + 3] = 0, 0, 0
        regs[GC + 5], regs[GC + 8], regs[SEQ + 2] = 0x40, 0xFF, 0x0F
        if rng.random() < 0.5:
            regs[GC + 0] = rng.randrange(16)
            regs[GC + 1] = rng.choice((0, rng.randrange(16)))
            regs[GC + 3] = rng.choice((0, rng.randrange(32)))
            regs[GC + 5] = 0x40 | rng.randrange(4)
            regs[GC + 8] = rng.choice((0xFF, rng.randrange(256)))
            regs[SEQ + 2] = rng.randrange(16)
        regs[LATCHES : LATCHES + 4] = rng.randbytes(4)
        self.vga.load_regs(bytes(regs))
        self.n.call("heg.regs", bytes(regs))

    def check(self, what: str) -> None:
        got, want = self.vga.chunky(), self.n.peek("g_bakVram")
        if got != want:
            at = next(i for i in range(len(got)) if got[i] != want[i])
            raise AssertionError(
                f"{what}: display memory differs at chunky {at:#x} "
                f"(native {want[at]:#04x}, original {got[at]:#04x})"
            )
        assert self.n.call("heg.regs") == self.vga.regs(), f"VGA registers after {what}"


def _words_clear_of_ffff(base: int, rows: int) -> bool:
    """No word of the middle runs at offset FFFFh (no original answer there)."""
    return all(
        (base + 4 - 0x50 * k + 2 * w) & 0xFFFF != 0xFFFF for k in range(rows) for w in range(0x24)
    )


def test_band(krondor_image, fresh_native):
    # The game's bands (rows of the 3-D view), then any rows: above the page, past its end,
    # around the 64 KB wrap, more rows than a page holds.
    rng = random.Random(1)
    pair = Pair(krondor_image, fresh_native, rng)
    for i in range(260):
        pair.page(rng.choice(PAGES))
        pair.registers(rng)
        if i < 160:
            start = rng.randrange(0, 200)
            count = rng.randrange(1, 201 - start)
        else:
            start = rng.choice((rng.randrange(-300, 300), rng.randrange(0x10000)))
            count = rng.choice((1, 2, rng.randrange(1, 400), rng.randrange(800, 900)))
        color = rng.randrange(0x10000)
        args = (start & 0xFFFF, count, color)
        pair.m.call_far("_vgaplanar_fill_band", *args)
        pair.n.call("vgafill.band", struct.pack("<3H", *args))
        pair.check(f"band {i} {start}+{count}")


def test_column_strip(krondor_image, fresh_native):
    # The ramp's rows (34 at most: the original reads its code past them) from offsets the
    # game passes (a row's start) and any others.
    rng = random.Random(2)
    pair = Pair(krondor_image, fresh_native, rng)
    cases = 0
    while cases < 200:
        if cases < 120:
            offset = 0x50 * rng.randrange(0, 200)
        else:
            offset = rng.randrange(0x10000)
        count = rng.choice((1, 2, 27, 29, 34, rng.randrange(1, 35)))
        if not _words_clear_of_ffff(offset, count):
            continue
        pair.page(rng.choice(PAGES))
        pair.registers(rng)
        pair.m.call_far("_vgaplanar_clear_column_strip", offset, count)
        pair.n.call("vgafill.strip", struct.pack("<2H", offset, count))
        pair.check(f"strip {cases} at {offset:#x}, {count} rows")
        cases += 1
