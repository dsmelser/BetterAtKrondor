"""VGABLIT.ASM's texture span copiers against port/asm/vgablit.c.

The original copies runs from the texture at AC00h through the latches of a planar VGA
model; display memory, viewed in the port's chunky layout, must match the native copy byte
for byte, as must the map mask and the graphics controller's mode register left behind.
Span tables, texture parameters, destination pages and clipping of the runs are random.
"""

from __future__ import annotations

import random
import struct

import pytest

from harness.machine import Machine
from harness.vga import PlanarVga

CTX_DST, CTX_LUT = 0x18, 0x6F2
PAGES = (0xA000, 0xA400, 0xA800, 0xAC00)
PARAMS = ("g_vgablit_si_base", "g_vgablit_si_stride", "g_vgablit_row_count",
          "g_vgablit_row_sub", "g_vgablit_si_mask")


class Copier:
    def __init__(self, image, native, rng: random.Random):
        self.m = Machine(image, vga=True)
        self.vga = PlanarVga(self.m)
        self.n = native
        vram = rng.randbytes(4 * 0x10000)
        self.vga.load_chunky(vram)
        native.poke("g_bakVram", vram)
        # Mode Y as vga_init leaves it: write mode 0, every plane, the row-offset LUT.
        self.vga.gc[5] = 0x40
        self.vga.seq[2] = 0x0F
        native.call("polyrast.out", struct.pack("<HB", 0x3CE, 5))
        native.call("polyrast.out", struct.pack("<HB", 0x3CF, 0x40))
        native.call("polyrast.out", struct.pack("<HB", 0x3C4, 2))
        native.call("polyrast.out", struct.pack("<HB", 0x3C5, 0x0F))
        self.ctx = self.m.sym("_g_graphics_context")
        self.block = bytearray(native.peek("g_gfxContextBlock"))
        self.set_ctx(CTX_LUT, struct.pack("<200H", *(80 * k for k in range(200))))
        self.spans = self.m.alloc(0x1000)

    def set_ctx(self, off: int, data: bytes) -> None:
        self.m.write((self.ctx[0], self.ctx[1] + off), data)
        self.block[off:off + len(data)] = data
        self.n.poke("g_gfxContextBlock", bytes(self.block))

    def params(self, values) -> None:
        for name, value in zip(PARAMS, values):
            self.m.write("_" + name, struct.pack("<H", value & 0xFFFF))
            self.n.poke(name, struct.pack("<H", value & 0xFFFF))

    def check(self, reply: bytes, what: str) -> None:
        assert reply == bytes([self.vga.seq[2], self.vga.gc[5]]), f"registers after {what}"
        got, want = self.vga.chunky(), self.n.peek("g_bakVram")
        if got != want:
            at = next(i for i in range(len(got)) if got[i] != want[i])
            raise AssertionError(f"{what}: display memory differs at {at:#x} "
                                 f"(native {want[at]:#04x}, original {got[at]:#04x})")


def _span_table(rng: random.Random) -> bytes:
    first = rng.randrange(0, 200)
    count = rng.randrange(1, 40)
    rows = []
    for _ in range(count):
        x0 = rng.randrange(-8, 330)
        x1 = x0 + rng.choice((0, 1, 2, 3, rng.randrange(-10, 120), rng.randrange(4, 330)))
        rows += [x0 & 0xFFFF, x1 & 0xFFFF]
    return struct.pack(f"<{2 + len(rows)}H", first, count, *rows)


@pytest.mark.parametrize("seed", range(3))
def test_runs(krondor_image, fresh_native, seed):
    rng = random.Random(40 + seed)
    c = Copier(krondor_image, fresh_native, rng)
    for i in range(120):
        c.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))
        c.params((rng.choice((0x3D40, rng.randrange(0x4000), rng.randrange(0x10000))),
                  rng.choice((0x43, 80, rng.randrange(0x200))), rng.choice((0, 0, 1, 3, 7)),
                  rng.randrange(-20, 200), rng.choice((0xFF7F, 0xFFFF, 0x3FFF, rng.randrange(0x10000)))))
        table = _span_table(rng)
        tiled = rng.random() < 0.5
        c.m.write((c.spans, 0), table)
        c.m.call_far("_vgablit_planar_runs" if tiled else "_vgablit_planar_runs_untiled",
                     es=c.spans, si=0)
        reply = c.n.call("vgablit.runs", bytes([tiled]) + table)
        c.check(reply, f"{'tiled' if tiled else 'untiled'} runs {i}")


def test_line_slot(krondor_image, fresh_native):
    rng = random.Random(43)
    c = Copier(krondor_image, fresh_native, rng)
    for i in range(150):
        c.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))
        c.params((rng.randrange(0x4000), rng.randrange(0x200), rng.choice((0, 2)),
                  rng.randrange(200), 0xFFFF))
        x0, x1, y0 = rng.randrange(0, 320), rng.randrange(0, 330), rng.randrange(200)
        y1 = y0 if rng.random() < 0.8 else rng.randrange(200)
        page = rng.choice(PAGES)
        c.m.call_far("_vgablit_span_emit_textured", bx=x0, cx=y0, dx=x1, si=y1, es=page)
        reply = c.n.call("vgablit.line", struct.pack("<5H", page, x0, y0, x1, y1))
        c.check(reply, f"line {i} ({x0}, {y0})-({x1}, {y1})")
