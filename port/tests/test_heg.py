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


SPRITE_DATA = 0x20000  # the images' block: two segments on each side
SPRITE_AT = 0xF0  # the record slots 29 and 3 get, between slot 14's table and its records
OFF_CLIP = 0x03  # bClip_enabled, then xmin, xmax, ymin, ymax
SCREEN = (1, 0, 639, 0, 349)


class Sprites:
    """Images for slots 29 and 3: a block of two segments with the same bytes on both
    sides, images installed by slot 14 at its start. The original's master copies the
    record into its stack frame through DS, so DS must be SS, as in the game: one segment
    holds slot 14's table and records, the record a blit gets, and the stack."""

    def __init__(self, pair: Pair):
        self.pair = pair
        self.dgroup = pair.m.alloc(0x10000)
        self.data = pair.m.alloc(SPRITE_DATA)
        self.blits = 0

    def load(self, rng: random.Random, sizes: list[tuple[int, int]], off: int = 0) -> list:
        """Random bytes, then images of the given sizes from offset off; their records
        (segment relative to the block's, plane offset, mask offset, width, height)."""
        m = self.pair.m
        data = rng.randbytes(SPRITE_DATA)
        m.write((self.data, 0), data)
        table = b""
        for i, (w, h) in enumerate(sizes):
            m.write((self.dgroup, 0x100 + 10 * i), _u16s(0xDEAD, 0xBEEF, 0xF00D, w, h))
            table += _u16s(0x100 + 10 * i)
        m.write((self.dgroup, 0), table + _u16s(0))
        packed = sum(w // 2 * h for w, h in sizes)
        if sizes:
            self.pair.chunk.call(14, 0, off, self.data, packed & 0xFFFF, packed >> 16, 0, 0, 0, 0,
                                 ds=self.dgroup)
        got = self.pair.native.call("heg.sprite_data", _u16s(off) + struct.pack("<I", packed)
                                    + _sizes_arg(sizes) + data)
        records = []
        for k in range(len(sizes)):
            seg, o, mask, w, h = struct.unpack("<5H", m.read((self.dgroup, 0x100 + 10 * k), 10))
            records.append((seg - self.data, o, mask, w, h))
        n = 10 * len(sizes)
        assert got[:n] == b"".join(_u16s(*r) for r in records), f"installed records {sizes}"
        assert got[n:] == m.read((self.data, 0), SPRITE_DATA), f"installed images {sizes}"
        self.pair.assert_same(f"after installing {sizes}")
        return records

    def clip(self, enabled: int, xmin: int, xmax: int, ymin: int, ymax: int) -> None:
        self.pair.set_ctx(OFF_CLIP, bytes([enabled]) + _u16s(xmin, xmax, ymin, ymax))

    def blit(self, slot: int, flags: int, x: int, y: int, record: tuple, what: str = "",
             page: int | None = None) -> None:
        """Slot 3 or 29 on both sides, onto the page given or else A000 and A800 by turns
        (the context's initial page, 0, is outside display memory)."""
        page = PAGES[self.blits & 1] if page is None else page
        self.blits += 1
        self.pair.set_ctx_u16(OFF_DST, page)
        rseg, off, mask, w, h = record
        self.pair.m.write((self.dgroup, SPRITE_AT), _u16s(self.data + rseg, off, mask, w, h))
        args = (SPRITE_AT, x, y) if slot == 3 else (SPRITE_AT, x, y, flags)
        self.pair.chunk.call(slot, *args, ds=self.dgroup, ss=self.dgroup)
        self.pair.native.call("heg.sprite", _u16s(slot, flags, x, y, rseg, off, mask, w, h))
        self.pair.assert_same(f"after sprite {what}: slot {slot} flags {flags:#x} at ({x}, {y}) "
                              f"on {page:04X}, record {record}")


def _record(rseg: int, off: int, w: int, h: int, plane: int | None = None) -> tuple:
    """A record into the random bytes: planes of (w / 8) * h bytes, then the mask."""
    plane = (w >> 3) * h if plane is None else plane
    return rseg, off, (off + 4 * plane) & 0xFFFF, w, h


def test_sprite_edges(pair):
    """Every flip at each edge of the clip rectangle, from inside to just outside, with the
    edges moved through a byte so x takes every value mod 8: whole bytes, a pixel more or
    less, touching, one pixel left in, gone."""
    rng = random.Random(11)
    pair.fill_vram(rng)
    s = Sprites(pair)
    s.load(rng, [])
    i = 0
    for shift in range(8):
        xmin, xmax, ymin, ymax = 40 + shift, 599 + shift, 20, 300
        s.clip(1, xmin, xmax, ymin, ymax)
        for w, h in ((24, 5), (21, 3)):
            rec = _record(0x123 + i % 7, rng.randrange(0x40, 0x8000), w, h)
            xs = [xmin - d for d in (0, 1, 5, 8, 9, 13, 16, w - 1, w, w + 6)]
            xs += [xmax + 1 - w + d for d in (0, 1, 5, 8, 9, 13, 16, w - 1, w, w + 6)]
            xs.append(xmin + 37)
            for x in xs:
                for flags in range(4):
                    if i % 5 == 0:
                        pair.scramble(rng)
                    s.blit(29, flags, x, 100 + i % 23, rec, f"{i} (clip x {xmin}..{xmax})")
                    i += 1
        w, h = 16, 12
        rec = _record(0x340, rng.randrange(0x40, 0x8000), w, h)
        ys = [ymin - d for d in (0, 1, 4, h - 1, h, h + 3)]
        ys += [ymax + 1 - h + d for d in (0, 1, 4, h - 1, h, h + 3)]
        for y in ys:
            for flags in range(4):
                s.blit(29, flags, xmin + 50 + i % 8, y, rec, f"{i} (clip y {ymin}..{ymax})")
                i += 1
        # Corners: cut on two sides at once.
        for x, y in ((xmin - 3, ymin - 2), (xmax - 9, ymax - 4), (xmin - 11, ymax - 1),
                     (xmax - 2, ymin - 7)):
            for flags in range(4):
                s.blit(29, flags, x, y, rec, f"{i} (corner)")
                i += 1
    # Wider and taller than the clip rectangle: cut on both sides.
    for shift in range(8):
        s.clip(1, 100 + shift, 131 + shift, 50, 57)
        rec = _record(0x200, rng.randrange(0x40, 0x8000), 64, 20)
        for flags in range(4):
            s.blit(29, flags, 90, 45, rec, f"{i} (both sides)")
            s.blit(29, flags, 70 + shift, 41, rec, f"{i} (both sides)")
            i += 1


def test_sprite_quirks(pair):
    """Tall sprites cut by 256 rows and more (the source moves by an 8-bit product), widths
    whose byte count passes 255 or is 0 (256 bytes a row), widths that are not a multiple of
    8, inverted clip rectangles, rows above the top (bit 14 of y), clipping off, a negative
    height, the flags' high byte and flags above 3; slot 3."""
    rng = random.Random(12)
    pair.fill_vram(rng)
    s = Sprites(pair)
    s.load(rng, [])
    s.clip(*SCREEN)
    i = 0
    for w in (8, 16):
        rec = _record(0x10, rng.randrange(0x40, 0x100), w, 300)
        for y in (-255, -256, -257, -280, -299, -300, 100, 349):
            for flags in range(4):
                s.blit(29, flags, 200 + i % 8, y, rec, f"{i} (tall)")
                i += 1
    for w in (2048, 2056, 2047, 2100):  # 256 bytes a row and more
        rec = _record(0x20, rng.randrange(0x40, 0x100), w, 3)
        for x, y in ((-1000, 10), (-900, -2), (-1500 - i % 8, 347), (5, 20)):
            for flags in (0, 3):
                s.blit(29, flags, x, y, rec, f"{i} (wide)")
                i += 1
    rec = _record(0x30, 0x200, 0, 2, plane=4)
    for clip in ((0, 0, 639, 0, 349), SCREEN):
        s.clip(*clip)
        for flags in range(4):
            s.blit(29, flags, 300 + i % 8, 30, rec, f"{i} (width 0, clip {clip})")
            i += 1
    for w in (1, 7, 9, 12, 21, 30):
        rec = _record(0x40, rng.randrange(0x40, 0x100), w, 4)
        for clip in ((0, 0, 639, 0, 349), (1, 3, 600, 0, 349)):
            s.clip(*clip)
            for x in (-5, 0, 1, 4, 9, 590 - w // 2, 600 - w):
                for flags in range(4):
                    if i % 7 == 0:
                        pair.scramble(rng)
                    s.blit(29, flags, x, 40, rec, f"{i} (width {w})")
                    i += 1
    # Inverted horizontally: the byte count wraps. (Cut on both sides by an inverted rectangle,
    # the rows come to ymax - ymin + 1, 0 or a count near 65536.)
    rec = _record(0x50, 0x80, 32, 6)
    for clip in ((1, 120, 100, 0, 349), (1, 140, 100, 0, 349), (1, 0, 639, 27, 27)):
        s.clip(*clip)
        for x in (90, 100, 104, 117):
            for flags in range(4):
                s.blit(29, flags, x, 25, rec, f"{i} (clip {clip})")
                i += 1
    s.clip(0, 0, 639, 0, 349)
    for y in (-1, -3, -100, -16384, 16384 + 7, 0x7FF0):
        for flags in range(4):
            s.blit(29, flags, 64 + i % 8, y, rec, f"{i} (unclipped)")
            i += 1
    # A negative height whose bottom and top differences overflow: the original tests their
    # sign (JS), not the comparison, and the two cuts leave the clip rectangle's 350 rows.
    s.clip(*SCREEN)
    negative = _record(0x60, 0x40, 8, -17000 & 0xFFFF)
    for flags in range(4):
        s.blit(29, flags, 100 + i % 8, -16000, negative, f"{i} (height -17000)")
        i += 1
    s.clip(*SCREEN)
    for y in (-1, -3, -5):
        for flags in range(4):
            s.blit(29, flags, 64 + i % 8, y, rec, f"{i} (above the top)")
            i += 1
    for flags in (0x100, 0x102, 0xFF03, 0x8001):
        for x in (-7, 300, 630):
            s.blit(29, flags, x, 344, rec, f"{i} (high byte)")
            i += 1
    # Flags above 3 jump through the words after the dispatch table, but a sprite clipped
    # away leaves before the dispatch.
    for flags in (4, 7, 0x80, 0xFF):
        for x, y in ((-32, 100), (640, 100), (300, -6), (300, 350)):
            s.blit(29, flags, x, y, rec, f"{i} (clipped away)")
            i += 1
    for x, y in ((-7, 3), (300, 100), (630, 345), (5000, 5)):
        s.blit(3, 0, x, y, rec, f"{i} (slot 3)")
        i += 1


def test_sprites(pair):
    """Images installed by slot 14 and random records, at random places, flips, clip
    rectangles and pages, from scrambled registers."""
    rng = random.Random(13)
    pair.fill_vram(rng)
    s = Sprites(pair)
    sizes = [(8, 4), (16, 10), (24, 7), (32, 20), (40, 3), (64, 33), (128, 12), (12, 6),
             (20, 5), (16, 270)]
    records = s.load(rng, sizes, rng.randrange(16))
    for i in range(400):
        if rng.random() < 0.6:
            rec = rng.choice(records)
        else:
            w, h = rng.choice((8, 16, 24, 40, 64, rng.randrange(1, 100))), rng.randrange(1, 40)
            rec = _record(rng.randrange(0x1000), rng.randrange(0x10000), w, h)
        w, h = rec[3], rec[4]
        if rng.random() < 0.2:
            clip = SCREEN
        else:
            xmin, ymin = rng.randrange(-20, 400), rng.randrange(-20, 250)
            clip = (rng.choice((1, 1, 1, 0, 0x80)), xmin, xmin + rng.randrange(0, 300),
                    ymin, ymin + rng.randrange(0, 150))
        s.clip(*clip)
        if clip[0]:
            x = rng.randrange(clip[1] - w - 12, clip[2] + 12)
            y = rng.randrange(clip[3] - h - 6, clip[4] + 6)
        else:
            x, y = rng.randrange(-30, 650), rng.randrange(-h - 5, 360)
        if rng.random() < 0.4:
            pair.scramble(rng)
        slot = 3 if rng.random() < 0.15 else 29
        flags = rng.randrange(4) | rng.choice((0, 0, 0, 0, 0x100, 0xA500))
        s.blit(slot, flags, x, y, rec, f"{i} (clip {clip})", page=rng.choice(PAGES))
