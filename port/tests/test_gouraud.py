"""GOURAUD.ASM's slanted quad rasterizer, its edge stepper, its slope comparison and the
corners' texture coordinates against port/asm/gouraud.c.

Quads of four random corners are drawn on test_polyrast's planar VGA model with random
textures, clip rectangles within the screen's rows (the original reads the row offset table,
which the test fills on both sides), pages and remap tables. Corners often share a coordinate,
so that vertical and horizontal edges, ties for the leftmost corner and edges without columns
come up, and corners often lie above the clip rectangle. There the port departs from the
original: a column clipped at the top skips as many steps of its texture walk as rows were
cut, and the original keeps that count for the columns after it, the port not. So the quads go
through the original's way on both sides (gouraud_blit_quad_as_original), and where the
original's stale count walked a texture column past its table, into its stack, the original's
display memory is adopted for the quads after. The port's way (polyrast_gouraud_blit_clipped,
which hands any quad without vertical sides to GOURAUD) is compared on quads with no corner
above the clip rectangle, where the two ways agree, and checked on its own for the fix: a quad
clipped at the top shows, from that row down, what it shows unclipped. The harness sets the
flags after the stepper's IDIV as DOSBox does.
"""

from __future__ import annotations

import random
import struct

import pytest

from test_polyrast import CTX_CLIP, CTX_DST, PAGES, QUAD_AT, RECORD_AT, Blitter

STEPPER = ("_edge_step_init", 91)  # the edge stepper, 91 bytes: JNS after IDIV
CTX_LUT = 0x6F2  # pRow_offset_lut[200] in the graphics context block
ROW_LUT = struct.pack("<200H", *(k * 80 for k in range(200)))
DISPATCH_RECORD_AT = 0xF200  # an ImageRecord in DGROUP's free space, above its data
OUTS_AT = 0xF210  # the dispatch's four results
EDGE_AT = 0xF220


def corners(rng: random.Random, y_from: int = -40) -> tuple[list[int], list[int]]:
    xs = [rng.randrange(-60, 380) for _ in range(4)]
    ys = [rng.randrange(y_from, 240) for _ in range(4)]
    for i in range(4):
        if rng.random() < 0.15:
            xs[i] = xs[rng.randrange(4)]
        if rng.random() < 0.15:
            ys[i] = ys[rng.randrange(4)]
    return xs, ys


def new_clip(b: Blitter, rng: random.Random) -> int:
    """A random clip rectangle within the screen's rows and a random page; its top row."""
    ymin = rng.randrange(0, 60)
    clip = bytes([rng.choice((0, 1, 0x10, 0x81))]) + struct.pack(
        "<4h", rng.randrange(-20, 100), rng.randrange(150, 340), ymin, rng.randrange(100, 200))
    b.set_ctx(CTX_CLIP, clip)
    b.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))
    return ymin


def texture(rng: random.Random) -> tuple[bytes, int, int]:
    width, height = rng.randrange(2, 70), rng.randrange(2, 70)
    pixels = bytes(0 if rng.random() < 0.3 else rng.randrange(1, 256)
                   for _ in range(width * height))
    return pixels, width, height


def new_remap(b: Blitter, rng: random.Random) -> None:
    remap = rng.choice((0, 0, 0x66 + 0x100 * rng.randrange(10), 0xA66 + 0x100 * rng.randrange(5)))
    b.state(struct.pack("<h", remap) + rng.randbytes(24))


def quad_as_original(b: Blitter, pixels: bytes, width: int, height: int, xs: list[int],
                     ys: list[int]) -> bool:
    """Both GOURAUDs on a quad, the original's way. Returns whether a texture column went
    past the table (the original read its stack): then the original's display memory is
    taken over instead of compared."""
    off = b.rng.choice((0, 0x10, 0x123))
    b.m.write((b.sprite_seg, off), pixels)
    b.n.call("polyrast.write", struct.pack("<I", b.n_sprite + off) + pixels)
    corners_ = struct.pack("<8h", *xs, *ys)
    b.m.write((b.dseg, RECORD_AT), struct.pack("<HHHhh", b.sprite_seg, off, 0, width, height))
    b.m.write((b.dseg, QUAD_AT), corners_)
    b.m.call_far("_gfx_gouraud_blit_quad", RECORD_AT, QUAD_AT, QUAD_AT + 8)
    record = struct.pack("<HHHhh", b.n_seg, (b.n_off + off) & 0xFFFF, 0, width, height)
    mask, past = b.n.call("gouraud.quad", record + corners_)
    what = f"{width}x{height} onto xs {xs} ys {ys}"
    assert mask == b.vga.seq[2], what
    if past:
        b.n.poke("g_bakVram", b.vga.chunky())
        return True
    got, want = b.vga.chunky(), b.n.peek("g_bakVram")
    if got != want:
        at = next(i for i in range(len(got)) if got[i] != want[i])
        raise AssertionError(f"{what}: display memory differs at {at:#x} "
                             f"(native {want[at]:#04x}, original {got[at]:#04x})")
    return False


@pytest.mark.parametrize("seed", range(3))
def test_slanted_quads(krondor_image, fresh_native, seed):
    """Any quad, the original's way, the stale skip count included."""
    rng = random.Random(300 + seed)
    b = Blitter(krondor_image, fresh_native, rng)
    b.m.model_division_flags(*STEPPER)
    b.set_ctx(CTX_LUT, ROW_LUT)
    stack_reads = 0
    for step in range(100):
        if step % 8 == 0:
            new_clip(b, rng)
        new_remap(b, rng)
        xs, ys = corners(rng)
        stack_reads += quad_as_original(b, *texture(rng), xs, ys)
    assert stack_reads < 20, "too few quads compared"


@pytest.mark.parametrize("seed", range(2))
def test_slanted_quads_the_port_way(krondor_image, fresh_native, seed):
    """Quads with no corner above the clip rectangle, through polyrast_gouraud_blit_clipped:
    no column is clipped at the top, so the port's way is the original's."""
    rng = random.Random(400 + seed)
    b = Blitter(krondor_image, fresh_native, rng)
    b.m.model_division_flags(*STEPPER)
    b.set_ctx(CTX_LUT, ROW_LUT)
    ymin = new_clip(b, rng)
    for step in range(100):
        if step % 8 == 0:
            ymin = new_clip(b, rng)
        new_remap(b, rng)
        xs, ys = corners(rng, ymin)
        pixels, width, height = texture(rng)
        b.quad(pixels, width, height, tuple(xs), tuple(ys))


def test_top_clip_is_a_crop(krondor_image, fresh_native):
    """The port's fix, on its own: a quad whose top edge rises above the clip rectangle at
    its left and sinks below it to the right (so columns after the clipped ones are drawn)
    shows, from the clip's top row down, what it shows drawn with the top at 0, and nothing
    above."""
    rng = random.Random(11)
    b = Blitter(krondor_image, fresh_native, rng)
    b.set_ctx(CTX_LUT, ROW_LUT)
    b.set_ctx(CTX_DST, struct.pack("<H", 0xA400))
    page = 0x4000 * 4  # the page's bytes in the chunky layout, 4 a planar byte
    for _ in range(20):
        width, height = rng.randrange(8, 60), rng.randrange(8, 60)
        pixels = bytes(rng.randrange(1, 256) for _ in range(width * height))
        b.n.call("polyrast.write", struct.pack("<I", b.n_sprite) + pixels)
        record = struct.pack("<HHHhh", b.n_seg, b.n_off & 0xFFFF, 0, width, height)
        top = rng.randrange(10, 80)
        xs = (rng.randrange(0, 40), rng.randrange(200, 319), rng.randrange(200, 319),
              rng.randrange(0, 40))
        ys = (rng.randrange(-40, top - 5), rng.randrange(top + 5, 120), rng.randrange(130, 199),
              rng.randrange(100, 199))
        b.state(struct.pack("<h", rng.choice((0, 0x66))) + bytes(24))
        background = rng.randbytes(4 * 0x10000)
        drawn = []
        for ymin in (0, top):
            b.set_ctx(CTX_CLIP, bytes([1]) + struct.pack("<4h", 0, 319, ymin, 199))
            b.n.poke("g_bakVram", background)
            b.n.call("polyrast.quad", record + struct.pack("<8h", *xs, *ys))
            drawn.append(b.n.peek("g_bakVram"))
        full, cropped = drawn
        rows = slice(page + top * 80 * 4, page + 200 * 80 * 4)
        assert cropped[rows] == full[rows], (xs, ys, top)
        above = slice(page, page + top * 80 * 4)
        assert cropped[above] == background[above], (xs, ys, top)
        assert full[above] != background[above], "nothing reached above the top"


def test_edge_stepper(krondor, native):
    """The stepper over count + 1 steps; its fraction follows the quotient's sign, read with
    JNS right after IDIV (the flags DOSBox Staging sets there, which the harness models)."""
    m = krondor
    m.model_division_flags(*STEPPER)
    rng = random.Random(7)
    for _ in range(3000):
        start, end = rng.randrange(0x10000), rng.randrange(0x10000)
        if rng.random() < 0.5:
            start, end = rng.randrange(64), rng.randrange(64)
        acc = start << 16 | rng.randrange(0x10000)
        step = end << 16 | rng.randrange(0x10000)
        count = rng.choice((0, 1, 2, 7, 63, 0x7FFE, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1, 400)))
        m.write((m.data_seg, EDGE_AT), struct.pack("<II", acc, step))
        m.call_far("_edge_step_init", EDGE_AT, count)
        want = m.read((m.data_seg, EDGE_AT), 8)
        got = native.call("gouraud.step", struct.pack("<IIH", acc, step, count))
        assert got == want, f"start {start:#x} end {end:#x} count {count:#x}"


def test_slopes(krondor, native):
    """The way round the quad from its leftmost corner: the corner's x is no greater than
    its neighbours', as the rasterizer has it (a negative count of columns would fault the
    original's unsigned division)."""
    m = krondor
    rng = random.Random(8)
    for _ in range(3000):
        x0, y0 = rng.randrange(-60, 380), rng.randrange(-40, 240)
        reach = rng.choice((6, 6, 40, 300))
        xa, xb = x0 + rng.randrange(reach), x0 + rng.randrange(reach)
        ya, yb = y0 + rng.randrange(-reach, reach), y0 + rng.randrange(-reach, reach)
        r = rng.random()
        if r < 0.15:
            ya = y0
        elif r < 0.3:
            yb = y0
        elif r < 0.4:
            ya = yb = y0
        if rng.random() < 0.1:
            xa = x0
        if rng.random() < 0.1:
            xb = x0
        args = (ya, xa, y0, x0, yb, xb)
        want = m.call_near("_compare_edge_slopes", *(a & 0xFFFF for a in args)).ax
        (got,) = struct.unpack("<H", native.call("gouraud.slopes", struct.pack("<6h", *args)))
        assert got == want, args


def test_corner_coordinates(krondor, native):
    """Each ordered pair of neighbouring corners, for textures of any size."""
    m = krondor
    rng = random.Random(9)
    pairs = [(0, 1), (1, 0), (1, 2), (2, 1), (2, 3), (3, 2), (3, 0), (0, 3)]
    for v0, v1 in pairs * 25:
        width = rng.choice((0, 1, 2, 3, rng.randrange(-5, 400)))
        height = rng.choice((0, 1, 2, 3, rng.randrange(-5, 400)))
        m.write((m.data_seg, DISPATCH_RECORD_AT),
                struct.pack("<HHHhh", 0x1234, 0x10, 0, width, height))
        m.write((m.data_seg, OUTS_AT), b"\x55" * 8)
        m.call_near("_gfx_gouraud_span_dispatch", v0, v1, DISPATCH_RECORD_AT, OUTS_AT,
                    OUTS_AT + 2, OUTS_AT + 4, OUTS_AT + 6)
        want = m.read((m.data_seg, OUTS_AT), 8)
        got = native.call("gouraud.dispatch", struct.pack("<4h", v0, v1, width, height))
        assert got == want, (v0, v1, width, height)
