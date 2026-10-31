"""The VMCODE.OVL "VGA:" driver chunk (DRIVERS/VMCODE/VGA.ASM) against port/drivers/vga.c.

The original runs on a planar VGA model; its display memory, viewed in the port's chunky
layout, must match the native driver's byte for byte, as must the graphics-context
block, the DAC, the CRTC start address and the palette cache.
"""

from __future__ import annotations

import random
import struct

import pytest

from conftest import _require, ref_dir
from harness.mzexe import load_mz
from harness.vga import CTX_SIZE, VgaChunk

PAGES = (0xA000, 0xA400, 0xA800, 0xAC00)
CACHE_AT = 0x19E  # pPaletteScratchBuf: seg:off on one side, a host pointer on the other
CACHE_SIZE = 0x600
OFF_SRC, OFF_DST = 0x16, 0x18  # wGfxBlitSrcPage, wGfxBlitDstPage
OFF_STYLE, OFF_FG, OFF_BG = 0x02, 0x00, 0x01


def _u16s(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}H", *(v & 0xFFFF for v in values))


@pytest.fixture(scope="session")
def vga_image():
    return load_mz(_require(ref_dir() / "OUT" / "VGA.EXE", "reference VGA.EXE"))


@pytest.fixture
def original_ctx(krondor) -> bytes:
    return krondor.read("_g_graphics_context", CTX_SIZE)


class Pair:
    """The original chunk and the native driver, kept in the same state."""

    def __init__(self, vga_image, native, ctx: bytes):
        self.chunk = VgaChunk(vga_image)
        self.native = native
        self.m = self.chunk.m
        self.cache_seg = self.m.alloc(CACHE_SIZE)
        self.scratch = self.m.alloc(0x10000)  # arguments the original reads from memory
        self.chunk.write_ctx(ctx)
        native.poke("g_gfxContextBlock", ctx)
        self.set_cache(bytes(CACHE_SIZE))
        self.native.call("vga.dac", bytes(768))

    def set_cache(self, data: bytes) -> None:
        self.m.write((self.cache_seg, 0), data)
        self.m.write((self.chunk.ctx_seg, CACHE_AT), _u16s(0, self.cache_seg))
        self.native.call("vga.palette_cache", data)

    def init(self) -> None:
        assert self.chunk.init() == 8
        self.native.call("vga.init")

    def set_ctx_u16(self, off: int, value: int) -> None:
        ctx = bytearray(self.chunk.ctx())
        ctx[off:off + 2] = _u16s(value)
        self.chunk.write_ctx(bytes(ctx))
        native_ctx = bytearray(self.native.peek("g_gfxContextBlock"))
        native_ctx[off:off + 2] = _u16s(value)
        self.native.poke("g_gfxContextBlock", bytes(native_ctx))

    def set_ctx_u8(self, off: int, value: int) -> None:
        ctx = bytearray(self.chunk.ctx())
        ctx[off] = value & 0xFF
        self.chunk.write_ctx(bytes(ctx))
        native_ctx = bytearray(self.native.peek("g_gfxContextBlock"))
        native_ctx[off] = value & 0xFF
        self.native.poke("g_gfxContextBlock", bytes(native_ctx))

    def fill_vram(self, rng: random.Random) -> None:
        data = rng.randbytes(4 * 0x10000)
        self.chunk.vga.load_chunky(data)
        self.native.poke("g_bakVram", data)

    def assert_same(self, what: str = "") -> None:
        ctx_o, ctx_n = self.chunk.ctx(), self.native.peek("g_gfxContextBlock")
        mask = slice(CACHE_AT, CACHE_AT + 4)
        assert ctx_o[:mask.start] + ctx_o[mask.stop:] == ctx_n[:mask.start] + ctx_n[mask.stop:], \
            f"graphics context differs {what}"
        vram_o, vram_n = self.chunk.vga.chunky(), self.native.peek("g_bakVram")
        if vram_o != vram_n:
            first = next(i for i in range(len(vram_o)) if vram_o[i] != vram_n[i])
            raise AssertionError(f"display memory differs {what}: first at chunky {first:#x} "
                                 f"(original {vram_o[first]:#04x}, native {vram_n[first]:#04x})")
        assert self.chunk.vga.dac_bytes() == self.native.call("vga.dac"), f"DAC differs {what}"
        assert self.chunk.vga.crtc_start() == struct.unpack(
            "<H", self.native.call("vga.crtc_start"))[0], f"CRTC start differs {what}"
        assert self.m.read((self.cache_seg, 0), CACHE_SIZE) == \
            self.native.call("vga.palette_cache"), f"palette cache differs {what}"


@pytest.fixture
def pair(vga_image, native, original_ctx) -> Pair:
    p = Pair(vga_image, native, original_ctx)
    p.init()
    return p


def test_context_initial_values(fresh_native, original_ctx):
    assert fresh_native.peek("g_gfxContextBlock") == original_ctx


def test_init(vga_image, native, original_ctx):
    p = Pair(vga_image, native, original_ctx)
    p.fill_vram(random.Random(1))
    p.init()
    p.assert_same("after vga_init")


def test_glyph(pair):
    rng = random.Random(2)
    pair.fill_vram(rng)
    for i in range(60):
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        pair.set_ctx_u8(OFF_STYLE, rng.choice((0, 0, 1, 4)))
        pair.set_ctx_u8(OFF_FG, rng.randrange(256))
        pair.set_ctx_u8(OFF_BG, rng.randrange(256))
        y, x, rows = rng.randrange(-4, 204), rng.randrange(-8, 330), rng.randrange(1, 16)
        glyph = rng.randbytes(rows)
        pair.m.write((pair.scratch, 0), glyph)
        # The glyph routine reads its bytes from ES:SI (it pops the caller's ES into DS).
        pair.chunk.call(1, bp=y, dx=x, cx=rows, es=pair.scratch, si=0)
        pair.native.call("vga.glyph", _u16s(y, x, rows) + glyph)
        pair.assert_same(f"after glyph {i} at ({x}, {y}) x{rows}")


def test_page_copy(pair):
    rng = random.Random(3)
    pair.fill_vram(rng)
    for i in range(40):
        pair.set_ctx_u16(OFF_SRC, rng.choice(PAGES))
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(0, 320), rng.randrange(0, 200)
        w, h = rng.randrange(1, 321 - x), rng.randrange(1, 201 - y)
        pair.chunk.call(4, x, y, w, h)
        pair.native.call("vga.present", _u16s(x, y, w, h))
        pair.assert_same(f"after copy {i} ({x}, {y}, {w}x{h})")


def test_save_and_paste_rect(pair):
    rng = random.Random(4)
    pair.fill_vram(rng)
    for i in range(40):
        pair.set_ctx_u16(OFF_SRC, rng.choice(PAGES))
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(-3, 330), rng.randrange(0, 200)
        w, h = rng.randrange(1, 40), rng.randrange(1, 30)
        pair.chunk.call(5, 0, pair.scratch, x, y, w, h)
        saved = pair.m.read((pair.scratch, 0), w * h)
        assert pair.native.call("vga.save_rect", _u16s(x, y, w, h)) == saved, f"save {i}"
        data = rng.randbytes(w * h)
        pair.m.write((pair.scratch, 0), data)
        pair.chunk.call(7, 0, pair.scratch, x, y, w, h)
        pair.native.call("vga.paste_rect", _u16s(x, y, w, h) + data)
        pair.assert_same(f"after paste {i} ({x}, {y}, {w}x{h})")


def test_pixels(pair):
    rng = random.Random(5)
    pair.fill_vram(rng)
    for i in range(200):
        pair.set_ctx_u16(OFF_SRC, rng.choice(PAGES))
        pair.set_ctx_u16(OFF_DST, rng.choice(PAGES))
        x, y = rng.randrange(-8, 330), rng.randrange(-4, 204)
        got = pair.chunk.call(21, x, y).ax
        assert struct.unpack("<H", pair.native.call("vga.getpixel", _u16s(x, y)))[0] == got
        color = rng.randrange(0x10000)
        pair.chunk.call(22, x, y, color)
        pair.native.call("vga.putpixel", _u16s(x, y, color))
    pair.assert_same("after the pixel run")


def test_vsync_flips(pair):
    for wait in (0, 1, 1, 0, 1):
        pair.chunk.call(8, wait)
        pair.native.call("vga.vsync", _u16s(wait))
        pair.assert_same(f"after vsync({wait})")


def _image_table(pair, sizes: list[tuple[int, int]]) -> int:
    """Records and a NULL-terminated table of near pointers to them, in one segment."""
    seg = pair.m.alloc(0x100 + 10 * len(sizes))
    table = b""
    for i, (w, h) in enumerate(sizes):
        rec_off = 0x100 + 10 * i
        pair.m.write((seg, rec_off), _u16s(0xDEAD, 0xBEEF, 0, w, h))
        table += _u16s(rec_off)
    pair.m.write((seg, 0), table + _u16s(0))
    return seg


def _image_args(sizes) -> bytes:
    return _u16s(len(sizes), *(v for wh in sizes for v in wh))


def test_image_install(pair):
    rng = random.Random(6)
    for _ in range(30):
        sizes = [(rng.randrange(0, 400), rng.randrange(0, 300)) for _ in range(rng.randrange(0, 9))]
        seg = _image_table(pair, sizes)
        r = pair.chunk.call(13, 0, 0x80, ds=seg)
        total, aux = struct.unpack("<IH", pair.native.call("vga.image_install", _image_args(sizes)))
        assert total == r.dx_ax
        assert aux == pair.m.u16((seg, 0x80))


def test_image_decode(pair):
    rng = random.Random(7)
    for i in range(12):
        sizes = [(rng.randrange(1, 64), rng.randrange(1, 64)) for _ in range(rng.randrange(1, 6))]
        packed = sum(w * h for w, h in sizes) // 2 + rng.randrange(0, 50)
        data = rng.randbytes(packed)
        seg = _image_table(pair, sizes)
        dst = pair.m.alloc(2 * packed + 16)
        pair.m.write((dst, 0), data)
        pair.chunk.call(14, 0, 0, dst, packed & 0xFFFF, packed >> 16, 0, 0, 0, 0, ds=seg)
        reply = pair.native.call("vga.image_decode", _image_args(sizes) +
                                 struct.pack("<I", packed) + data)
        for k in range(len(sizes)):
            got_off, got_far = struct.unpack_from("<IH", reply, 6 * k)
            img_seg, img_off = struct.unpack("<HH", pair.m.read((seg, 0x100 + 10 * k), 4))
            assert (got_off, got_far) == ((img_seg * 16 + img_off) - dst * 16, img_off), f"{i}.{k}"
        assert reply[6 * len(sizes):] == pair.m.read((dst, 0), 2 * packed), f"decode {i}"


def test_unpack_nibbles(pair):
    rng = random.Random(8)
    for i in range(12):
        count = rng.randrange(1, 3000)
        src, dst = rng.randbytes(count), rng.randbytes(2 * count)
        s, d = pair.m.alloc(count), pair.m.alloc(2 * count)
        pair.m.write((s, 0), src)
        pair.m.write((d, 0), dst)
        pair.chunk.call(15, 0, s, 0, d, count)
        assert pair.native.call("vga.unpack", _u16s(count) + src + dst) == \
            pair.m.read((d, 0), 2 * count), f"unpack {i}"


def _palette(rng: random.Random) -> bytes:
    return bytes(rng.randrange(64) for _ in range(768))


def test_palette_set(pair):
    rng = random.Random(9)
    for i in range(4):
        pal = _palette(rng)
        pair.m.write((pair.scratch, 0), pal)
        pair.chunk.call(20, 0, pair.scratch)
        pair.native.call("vga.palette_set", pal)
        pair.assert_same(f"after palette_set {i}")


def test_dac_write(pair):
    rng = random.Random(10)
    for i in range(30):
        start, count = rng.randrange(256), rng.randrange(1, 300)
        rgb = bytes(rng.randrange(256) for _ in range(3 * count))
        pair.m.write((pair.scratch, 0), rgb)
        pair.chunk.call(35, 0, start, count, ds=pair.scratch)
        pair.native.call("vga.dac_write", _u16s(start, count) + rgb)
        pair.assert_same(f"after dac write {i} ({start}, {count})")


def test_palette_lerp(pair):
    rng = random.Random(11)
    pair.set_cache(_palette(rng) + _palette(rng))
    for i in range(40):
        start = rng.randrange(256)
        count, target, weight = rng.randrange(1, 257 - start), rng.randrange(256), rng.randrange(64)
        pair.chunk.call(34, start, count, target, weight)
        pair.native.call("vga.lerp", _u16s(start, count, target, weight))
        pair.assert_same(f"after lerp {i} ({start}, {count}, {target}, {weight})")
