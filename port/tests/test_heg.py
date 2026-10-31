"""The VMCODE.OVL "HEG:" driver chunk (DRIVERS/VMCODE/HEG.ASM) against port/drivers/heg.c.

The original runs on the planar VGA model. After every call its display memory (in the
port's chunky layout), the graphics-context block, the VGA's registers (with the index
registers, the attribute flip-flop and the latches), the DAC and the palette mirror must
match the native driver's. The original leaves registers for the next routine to rely on,
so many tests start each call from scrambled graphics-controller and sequencer registers.
"""

from __future__ import annotations

import random
import struct

import pytest

from conftest import _require, ref_dir
from harness.mzexe import load_mz
from harness.vga import CTX_SIZE, HegChunk

PAGES = (0xA000, 0xA800)
MIRROR_AT = 0x19E  # pPaletteScratchBuf: seg:off on one side, a host pointer on the other
MIRROR_SIZE = 0x60
OFF_FG, OFF_BG, OFF_STYLE = 0x00, 0x01, 0x02
OFF_SRC, OFF_DST = 0x16, 0x18
OFF_ADAPTER = 0x1D
OFF_HEIGHT = 0x6EC
SEQ, GC = 0, 40  # offsets of the sequencer and graphics-controller registers in a snapshot


def _u16s(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}H", *(v & 0xFFFF for v in values))


@pytest.fixture(scope="session")
def heg_image():
    return load_mz(_require(ref_dir() / "OUT" / "HEG.EXE", "reference HEG.EXE"))


@pytest.fixture
def original_ctx(krondor) -> bytes:
    ctx = bytearray(krondor.read("_g_graphics_context", CTX_SIZE))
    ctx[OFF_HEIGHT:OFF_HEIGHT + 2] = _u16s(0x15E)  # video_driver_load's height for the books
    return bytes(ctx)


class Pair:
    """The original chunk and the native driver, kept in the same state."""

    def __init__(self, heg_image, native, ctx: bytes):
        self.chunk = HegChunk(heg_image)
        self.native = native
        self.m = self.chunk.m
        self.vga = self.chunk.vga
        self.mirror_seg = self.m.alloc(MIRROR_SIZE)
        self.scratch = self.m.alloc(0x10000)  # arguments the original reads from memory
        self.chunk.write_ctx(ctx)
        native.poke("g_gfxContextBlock", ctx)
        self.set_mirror(bytes(MIRROR_SIZE))
        self.native.call("vga.dac", bytes(768))
        self.native.call("heg.regs", self.vga.regs())

    def set_mirror(self, data: bytes) -> None:
        self.m.write((self.mirror_seg, 0), data)
        self.m.write((self.chunk.ctx_seg, MIRROR_AT), _u16s(0, self.mirror_seg))
        self.native.call("heg.mirror", data)

    def init(self) -> None:
        assert self.chunk.init() == 2
        self.native.call("heg.init")

    def set_ctx(self, off: int, data: bytes) -> None:
        ctx = bytearray(self.chunk.ctx())
        ctx[off:off + len(data)] = data
        self.chunk.write_ctx(bytes(ctx))
        native_ctx = bytearray(self.native.peek("g_gfxContextBlock"))
        native_ctx[off:off + len(data)] = data
        self.native.poke("g_gfxContextBlock", bytes(native_ctx))

    def set_ctx_u16(self, off: int, value: int) -> None:
        self.set_ctx(off, _u16s(value))

    def set_ctx_u8(self, off: int, value: int) -> None:
        self.set_ctx(off, bytes([value & 0xFF]))

    def fill_vram(self, rng: random.Random) -> None:
        data = rng.randbytes(4 * 0x10000)
        self.vga.load_chunky(data)
        self.native.poke("g_bakVram", data)

    def scramble(self, rng: random.Random) -> None:
        """Set/reset, its enable, the data rotation and logical operation, the write and
        read modes, the bit mask, the map mask and the latches the next routine meets."""
        regs = bytearray(self.vga.regs())
        regs[GC + 0] = rng.randrange(16)
        regs[GC + 1] = rng.choice((0, 0, rng.randrange(16)))
        regs[GC + 3] = rng.choice((0, 0, rng.randrange(32)))
        regs[GC + 5] = rng.choice((0x02, 0x02, rng.randrange(4) | rng.choice((0, 8))))
        regs[GC + 8] = rng.choice((0xFF, rng.randrange(256)))
        regs[SEQ + 2] = rng.choice((0x0F, rng.randrange(16)))
        regs[95:99] = rng.randbytes(4)
        self.vga.load_regs(bytes(regs))
        self.native.call("heg.regs", bytes(regs))

    def assert_same(self, what: str = "") -> None:
        ctx_o, ctx_n = self.chunk.ctx(), self.native.peek("g_gfxContextBlock")
        mask = slice(MIRROR_AT, MIRROR_AT + 4)
        assert ctx_o[:mask.start] + ctx_o[mask.stop:] == ctx_n[:mask.start] + ctx_n[mask.stop:], \
            f"graphics context differs {what}"
        vram_o, vram_n = self.vga.chunky(), self.native.peek("g_bakVram")
        if vram_o != vram_n:
            first = next(i for i in range(len(vram_o)) if vram_o[i] != vram_n[i])
            raise AssertionError(f"display memory differs {what}: first at chunky {first:#x} "
                                 f"(original {vram_o[first]:#04x}, native {vram_n[first]:#04x})")
        regs_o, regs_n = self.vga.regs(), self.native.call("heg.regs")
        if regs_o != regs_n:
            first = next(i for i in range(len(regs_o)) if regs_o[i] != regs_n[i])
            raise AssertionError(f"VGA registers differ {what}: first at snapshot byte {first} "
                                 f"(original {regs_o[first]:#04x}, native {regs_n[first]:#04x})")
        assert self.vga.dac_bytes() == self.native.call("vga.dac"), f"DAC differs {what}"
        assert self.m.read((self.mirror_seg, 0), MIRROR_SIZE) == \
            self.native.call("heg.mirror"), f"palette mirror differs {what}"


# Each test gets a native process of its own: the driver leaves the shared one at 640x350,
# which other modules' tests (the mouse's ranges) would see.
@pytest.fixture
def pair(heg_image, fresh_native, original_ctx) -> Pair:
    p = Pair(heg_image, fresh_native, original_ctx)
    p.init()
    return p


def test_init(heg_image, fresh_native, original_ctx):
    rng = random.Random(1)
    p = Pair(heg_image, fresh_native, original_ctx)
    p.fill_vram(rng)
    p.scramble(rng)
    p.init()
    p.assert_same("after heg_init")


def test_blit_chunky(pair):
    rng = random.Random(2)
    pair.fill_vram(rng)
    for i in range(120):
        pair.scramble(rng)
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(-16, 660), rng.randrange(-2, 352)
        w = rng.choice((640, rng.randrange(0, 700)))
        h = rng.choice((1, 1, rng.randrange(-1, 12)))
        off = rng.randrange(0, 40)
        size = max(w >> 3, 1) * 4 * max(h, 1)
        data = rng.randbytes(off + size)
        pair.m.write((pair.scratch, 0), data)
        pair.chunk.call(17, off, pair.scratch, x, y, w, h)
        pair.native.call("heg.blit_chunky", _u16s(x, y, w, h, off) + struct.pack("<I", size) + data)
        pair.assert_same(f"after blit {i} ({x}, {y}, {w}x{h}, offset {off})")


def _rect_bytes(x: int, w: int, h: int) -> int:
    """What slots 5 and 7 move: the bytes from x's to (x + w)'s, in whole words, for each
    row of each plane."""
    first = (x & 0xFFFF) >> 3
    span = ((((w + x) & 0xFFFF) >> 3) - first + 1) & 0xFFFF
    return (span + 1) // 2 * 2 * h * 4


def test_save_and_paste_rect(pair):
    rng = random.Random(3)
    pair.fill_vram(rng)
    for i in range(60):
        pair.scramble(rng)
        pair.set_ctx_u16(OFF_SRC, rng.choice(PAGES))
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(0, 660), rng.randrange(0, 350)
        w, h = rng.choice((0x27F, rng.randrange(0, 120))), rng.randrange(1, 12)
        off = rng.randrange(0, 40)
        size = _rect_bytes(x, w, h)
        pair.m.write((pair.scratch, 0), bytes(off + size))
        pair.chunk.call(5, off, pair.scratch, x, y, w, h)
        saved = pair.native.call("heg.save_rect", _u16s(x, y, w, h, off) + struct.pack("<I", size))
        assert pair.m.read((pair.scratch, 0), off + size) == saved, f"save {i}"
        pair.assert_same(f"after save {i}")
        data = rng.randbytes(off + size)
        pair.m.write((pair.scratch, 0), data)
        pair.scramble(rng)
        pair.chunk.call(7, off, pair.scratch, x, y, w, h)
        pair.native.call("heg.paste_rect", _u16s(x, y, w, h, off) + struct.pack("<I", size) + data)
        pair.assert_same(f"after paste {i} ({x}, {y}, {w}x{h})")


def test_blit_size(pair):
    rng = random.Random(4)
    for w, h in [(0, 0), (1, 1), (7, 3), (8, 1), (0x27F, 1), (0x280, 350), (-1, 5), (5, -1)] + [
            (rng.randrange(0x10000), rng.randrange(0x10000)) for _ in range(300)]:
        assert pair.chunk.call(6, w, h).dx_ax == struct.unpack(
            "<I", pair.native.call("heg.blit_size", _u16s(w, h)))[0], f"size of {w}x{h}"


def test_present(pair):
    rng = random.Random(5)
    pair.fill_vram(rng)
    for i in range(50):
        pair.scramble(rng)
        pair.set_ctx_u16(OFF_SRC, rng.choice(PAGES))
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(-8, 640), rng.randrange(0, 350)
        w, h = rng.randrange(0, 650 - x), rng.randrange(1, 351 - y)
        pair.chunk.call(4, x, y, w, h)
        pair.native.call("heg.present", _u16s(x, y, w, h))
        pair.assert_same(f"after copy {i} ({x}, {y}, {w}x{h})")


def test_glyph(pair):
    rng = random.Random(6)
    pair.fill_vram(rng)
    for i in range(80):
        if rng.random() < 0.3:
            pair.scramble(rng)
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        pair.set_ctx_u8(OFF_STYLE, rng.choice((0, 0, 1, 4)))
        pair.set_ctx_u8(OFF_FG, rng.randrange(256))
        pair.set_ctx_u8(OFF_BG, rng.randrange(256))
        y, x, rows = rng.randrange(-2, 352), rng.randrange(-8, 650), rng.randrange(1, 16)
        glyph = rng.randbytes(rows)
        pair.m.write((pair.scratch, 0), glyph)
        # The glyph routine reads its bytes from ES:SI (it pops the caller's ES into DS).
        pair.chunk.call(1, bp=y, dx=x, cx=rows, es=pair.scratch, si=0)
        pair.native.call("heg.glyph", _u16s(y, x, rows) + glyph)
        pair.assert_same(f"after glyph {i} at ({x}, {y}) x{rows}")


def test_pixels(pair):
    rng = random.Random(7)
    pair.fill_vram(rng)
    for i in range(200):
        if rng.random() < 0.3:
            pair.scramble(rng)
        pair.set_ctx_u16(OFF_SRC, rng.choice(PAGES))
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(-8, 650), rng.randrange(-2, 352)
        got = pair.chunk.call(21, x, y).ax
        assert struct.unpack("<H", pair.native.call("heg.getpixel", _u16s(x, y)))[0] == got, \
            f"pixel {i} at ({x}, {y})"
        pair.assert_same(f"after reading pixel {i} at ({x}, {y})")
        color = rng.randrange(0x10000)
        pair.chunk.call(22, x, y, color)
        pair.native.call("heg.putpixel", _u16s(x, y, color))
        pair.assert_same(f"after pixel {i} at ({x}, {y})")


def test_vsync_flips(pair):
    for wait in (0, 1, 1, 0, 1):
        pair.chunk.call(8, wait)
        pair.native.call("heg.vsync", _u16s(wait))
        pair.assert_same(f"after vsync({wait})")


def test_palette(pair):
    rng = random.Random(8)
    for i in range(30):
        palette = bytes(rng.randrange(64) for _ in range(48))
        pair.m.write((pair.scratch, 0), palette)
        pair.chunk.call(20, 0, pair.scratch)
        pair.native.call("heg.palette_set", palette)
        pair.assert_same(f"after palette {i}")
        # Within the mirror's 16 colours, as the game blends: past them each side reads
        # whatever memory follows its mirror.
        start = rng.randrange(16)
        count, target = rng.randrange(1, 17 - start), rng.randrange(16)
        weight = rng.choice((0, 63, rng.randrange(64), rng.randrange(256)))
        pair.chunk.call(34, start, count, target, weight)
        pair.native.call("heg.lerp", _u16s(start, count, target, weight))
        pair.assert_same(f"after blend {i} ({start}+{count} to {target}, {weight}/63)")
    # A palette at segment 0 is ignored.
    pair.chunk.call(20, 0x40, 0)
    pair.native.call("heg.palette_set")
    pair.assert_same("after a null palette")


def test_border(pair):
    for adapter in (2, 12):
        pair.set_ctx_u8(OFF_ADAPTER, adapter)
        for color in (0, 3, 7, 8, 9, 15, 0x1F, 0xFFFF):
            pair.chunk.call(25, color)
            pair.native.call("heg.border", _u16s(color))
            pair.assert_same(f"after border {color:#x} on adapter {adapter}")


def _image_table(pair, sizes: list[tuple[int, int]]) -> int:
    """Records and a NULL-terminated table of near pointers to them, in one segment."""
    seg = pair.m.alloc(0x100 + 10 * len(sizes))
    table = b""
    for i, (w, h) in enumerate(sizes):
        rec_off = 0x100 + 10 * i
        pair.m.write((seg, rec_off), _u16s(0xDEAD, 0xBEEF, 0xF00D, w, h))
        table += _u16s(rec_off)
    pair.m.write((seg, 0), table + _u16s(0))
    return seg


def _sizes_arg(sizes: list[tuple[int, int]]) -> bytes:
    return _u16s(len(sizes)) + b"".join(_u16s(w, h) for w, h in sizes)


def test_image_install(pair):
    rng = random.Random(9)
    for i in range(40):
        sizes = [(rng.choice((16, 32, 64, rng.randrange(0x10000))),
                  rng.choice((8, 40, rng.randrange(0x10000)))) for _ in range(rng.randrange(0, 8))]
        seg = _image_table(pair, sizes)
        pair.m.write((seg, 0xF0), _u16s(0x5555))
        r = pair.chunk.call(13, 0, 0xF0, ds=seg)
        total, aux = struct.unpack("<IH", pair.native.call("heg.image_install", _sizes_arg(sizes)))
        assert (r.dx_ax, pair.m.u16((seg, 0xF0))) == (total, aux), f"image set {i}: {sizes}"


def test_image_decode(pair):
    rng = random.Random(10)
    pair.fill_vram(rng)
    for i in range(20):
        pair.scramble(rng)
        sizes = [(rng.choice((8, 16, 24, 32, 64, 128)), rng.randrange(1, 40))
                 for _ in range(rng.randrange(1, 6))]
        packed = sum(w // 2 * h for w, h in sizes)
        size = packed + packed // 4 + 16
        data = rng.randbytes(packed) + bytes(size - packed)
        seg = _image_table(pair, sizes)
        buf = pair.m.alloc(size)
        pair.m.write((buf, 0), data)
        pair.chunk.call(14, 0, 0, buf, packed & 0xFFFF, packed >> 16, 0, 0, 0, 0, ds=seg)
        got = pair.native.call("heg.image_decode",
                               _sizes_arg(sizes) + struct.pack("<II", packed, size) + data)
        records = b""
        for k in range(len(sizes)):
            r_seg, r_off, r_mask, w, h = struct.unpack("<5H", pair.m.read((seg, 0x100 + 10 * k), 10))
            records += _u16s(r_seg - buf, r_off, r_mask, w, h)
        assert got[:len(records)] == records, f"image records {i}: {sizes}"
        assert got[len(records):] == pair.m.read((buf, 0), size), f"installed images {i}: {sizes}"
        pair.assert_same(f"after image set {i}")
