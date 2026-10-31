"""POLYRAST.ASM's scaled sprite blit, textured quad blit and edge stepper against
port/asm/polyrast.c.

The original draws on a planar VGA model; display memory, viewed in the port's chunky
layout, must match the native blit's byte for byte, as must the raster state (the edge
steppers), the map mask left in the sequencer and the RLE scratch buffer. Sprites are
random, plain and RLE-packed, drawn at random places, scales (negative: flipped), modes,
clip rectangles, pages and remap tables. Quads have vertical sides (the others go to
GOURAUD.ASM, not in C yet) and come in either direction.
"""

from __future__ import annotations

import random
import struct

import pytest

from harness.machine import Machine
from harness.vga import PlanarVga

RECORD_AT = 0xF000  # an ImageRecord in DGROUP's free space, above its data
QUAD_AT = 0xF020  # a quad's xs[4] and ys[4]
EDGE_AT = 0xF100
CTX_CLIP, CTX_DST = 0x03, 0x18
TABLES = 0xF00
PAGES = (0xA000, 0xA400, 0xA800)


def rle(data: bytes, rng: random.Random) -> bytes:
    out, i = bytearray(), 0
    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 0x7F:
            run += 1
        if run > 2 or rng.random() < 0.2:
            out += bytes([0x80 | run, data[i]])
            i += run
        else:
            n = min(rng.randrange(1, 0x7F), len(data) - i)
            out += bytes([n]) + data[i:i + n]
            i += n
    return bytes(out) + b"\x00"


class Blitter:
    def __init__(self, image, native, rng: random.Random):
        self.m = Machine(image, vga=True)
        self.vga = PlanarVga(self.m)
        self.n = native
        self.rng = rng
        self.dseg = self.m.data_seg
        self.tables_at = self.m.sym("_g_abFogRemapTable")
        # The scratch buffer and the sprite data, on each side.
        self.scratch_seg = self.m.alloc(0x4000)
        self.m.write("_g_pMainScratchBuf", struct.pack("<HH", 0, self.scratch_seg))
        _, _, self.n_scratch = struct.unpack("<HHI", native.call("polyrast.block", struct.pack("<I", 0x4000)))
        native.call("polyrast.scratch", struct.pack("<I", self.n_scratch))
        self.sprite_seg = self.m.alloc(0x8000)
        self.n_seg, self.n_off, self.n_sprite = struct.unpack(
            "<HHI", native.call("polyrast.block", struct.pack("<I", 0x8000)))
        vram = rng.randbytes(4 * 0x10000)
        self.vga.load_chunky(vram)
        native.poke("g_bakVram", vram)
        tables = rng.randbytes(TABLES)
        self.m.write(self.tables_at, tables)
        native.poke("g_polyrastTables", tables)
        self.vga.seq[2] = 0x0F  # the map mask, which a clipped-out blit leaves alone
        native.call("polyrast.out", struct.pack("<HB", 0x3C4, 2))
        native.call("polyrast.out", struct.pack("<HB", 0x3C5, 0x0F))
        self.ctx = bytearray(native.peek("g_gfxContextBlock"))

    def set_ctx(self, off: int, data: bytes) -> None:
        self.m.write((self.m.sym("_g_graphics_context")[0], self.m.sym("_g_graphics_context")[1] + off), data)
        self.ctx[off:off + len(data)] = data
        self.n.poke("g_gfxContextBlock", bytes(self.ctx))

    def state(self, data: bytes) -> None:
        self.m.write("_g_polyRasterState", data)
        self.n.poke("g_polyRasterState", data)

    def blit(self, pixels: bytes, packed: bool, width: int, height: int, x: int, y: int,
             flags: int, w: int, h: int, colour: int) -> None:
        off = self.rng.choice((0, 0x10, 0x123))
        self.m.write((self.sprite_seg, off), pixels)
        self.n.call("polyrast.write", struct.pack("<I", self.n_sprite + off) + pixels)
        kind = 0x80 if packed else self.rng.choice((0, 0x01, 0x40))
        self.m.write((self.dseg, RECORD_AT), struct.pack("<HHHhh", self.sprite_seg, off, kind, width, height))
        args = (x, y, flags, w, h, colour)
        self.m.call_far("_polyrast_spr_scaled_blit_planar", RECORD_AT, *(a & 0xFFFF for a in args))
        record = struct.pack("<HHHhh", self.n_seg, (self.n_off + off) & 0xFFFF, kind, width, height)
        (mask,) = struct.unpack("<B", self.n.call(
            "polyrast.blit", record + struct.pack("<hhHhhh", *args)))
        what = f"{width}x{height} {'rle' if packed else 'raw'} at ({x},{y}) flags {flags:#x} {w}x{h}"
        assert self.n.peek("g_polyRasterState") == self.m.read("_g_polyRasterState", 26), what
        assert mask == self.vga.seq[2], what
        if packed:
            size = width * height
            assert self.n.call("polyrast.read", struct.pack("<II", self.n_scratch, size)) == \
                self.m.read((self.scratch_seg, 0), size), what
        got, want = self.vga.chunky(), self.n.peek("g_bakVram")
        if got != want:
            at = next(i for i in range(len(got)) if got[i] != want[i])
            raise AssertionError(f"{what}: display memory differs at {at:#x} "
                                 f"(native {want[at]:#04x}, original {got[at]:#04x})")


    def quad(self, pixels: bytes, width: int, height: int, xs: tuple, ys: tuple) -> None:
        off = self.rng.choice((0, 0x10, 0x123))
        self.m.write((self.sprite_seg, off), pixels)
        self.n.call("polyrast.write", struct.pack("<I", self.n_sprite + off) + pixels)
        corners = struct.pack("<8h", *xs, *ys)
        self.m.write((self.dseg, RECORD_AT), struct.pack("<HHHhh", self.sprite_seg, off, 0, width, height))
        self.m.write((self.dseg, QUAD_AT), corners)
        self.m.call_far("_polyrast_gouraud_blit_clipped", RECORD_AT, QUAD_AT, QUAD_AT + 8)
        record = struct.pack("<HHHhh", self.n_seg, (self.n_off + off) & 0xFFFF, 0, width, height)
        reply = self.n.call("polyrast.quad", record + corners)
        what = f"{width}x{height} onto xs {xs} ys {ys}"
        assert reply[1:] == self.m.read((self.dseg, QUAD_AT), 16), what
        assert self.n.peek("g_polyRasterState") == self.m.read("_g_polyRasterState", 26), what
        assert reply[0] == self.vga.seq[2], what
        got, want = self.vga.chunky(), self.n.peek("g_bakVram")
        if got != want:
            at = next(i for i in range(len(got)) if got[i] != want[i])
            raise AssertionError(f"{what}: display memory differs at {at:#x} "
                                 f"(native {want[at]:#04x}, original {got[at]:#04x})")


def _new_clip(b: Blitter, rng: random.Random) -> None:
    clip = bytes([rng.choice((0, 1, 0x10, 0x81))]) + struct.pack(
        "<4h", rng.randrange(-20, 100), rng.randrange(150, 340), rng.randrange(-20, 60),
        rng.randrange(100, 220))
    b.set_ctx(CTX_CLIP, clip)
    b.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))


@pytest.mark.parametrize("seed", range(3))
def test_textured_quad(krondor_image, fresh_native, seed):
    rng = random.Random(100 + seed)
    b = Blitter(krondor_image, fresh_native, rng)
    for step in range(120):
        width, height = rng.randrange(1, 60), rng.randrange(1, 60)
        pixels = bytes(0 if rng.random() < 0.3 else rng.randrange(1, 256) for _ in range(width * height))
        if step % 8 == 0:
            _new_clip(b, rng)
        remap = rng.choice((0, 0, 0x66 + 0x100 * rng.randrange(10), 0xA66 + 0x100 * rng.randrange(5)))
        b.state(struct.pack("<h", remap) + rng.randbytes(24))
        x0, x1 = rng.randrange(-60, 330), rng.randrange(-60, 380)
        y0, y1 = rng.randrange(-40, 210), rng.randrange(-40, 210)
        y3, y2 = y0 + rng.randrange(0, 160), y1 + rng.randrange(0, 160)
        if rng.random() < 0.1:
            x1 = x0  # no width: nothing
        b.quad(pixels, width, height, (x0, x1, x1, x0), (y0, y1, y2, y3))


@pytest.mark.parametrize("seed", range(4))
def test_scaled_blit(krondor_image, fresh_native, seed):
    rng = random.Random(seed)
    b = Blitter(krondor_image, fresh_native, rng)
    for step in range(150):
        width, height = rng.randrange(1, 40), rng.randrange(1, 40)
        pixels = bytes(0 if rng.random() < 0.3 else rng.randrange(1, 256) for _ in range(width * height))
        packed = rng.random() < 0.3
        if step % 10 == 0:
            _new_clip(b, rng)
        remap = rng.choice((0, 0, 0x66 + 0x100 * rng.randrange(10), 0xA66 + 0x100 * rng.randrange(5)))
        b.state(struct.pack("<h", remap) + rng.randbytes(24))
        scale = rng.choice((0.5, 1, 1, 1.5, 2, 3))
        w = max(1, int(width * scale)) * rng.choice((1, 1, -1))
        h = max(1, int(height * scale)) * rng.choice((1, 1, -1))
        if rng.random() < 0.05:
            w = rng.choice((0, 1, -1))
        flags = rng.choice((0, 0, 1, 2, 3, 4, 8, 12, 5, 10))
        b.blit(rle(pixels, rng) if packed else pixels, packed, width, height,
               rng.randrange(-50, 330), rng.randrange(-50, 230), flags, w, h, rng.randrange(256))


def test_edge_stepper(krondor, native):
    m = krondor
    rng = random.Random(5)
    for _ in range(3000):
        start, end = rng.randrange(0x10000), rng.randrange(0x10000)
        if rng.random() < 0.5:
            start, end = rng.randrange(64), rng.randrange(64)
        acc = start << 16 | rng.randrange(0x10000)
        step = end << 16 | rng.randrange(0x10000)
        steps = rng.choice((0, 1, 2, 7, 63, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1, 400)))
        m.write((m.data_seg, EDGE_AT), struct.pack("<II", acc, step))
        m.call_far("_polyrast_edge_init_step", EDGE_AT, steps)
        want = m.read((m.data_seg, EDGE_AT), 8)
        got = native.call("polyrast.step", struct.pack("<IIH", acc, step, steps))
        assert got == want, f"start {start:#x} end {end:#x} steps {steps:#x}"
