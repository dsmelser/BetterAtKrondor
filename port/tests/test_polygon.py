"""POLYGON.ASM (draw_polygon, the polygon rasterizer) and POLYCLIP.ASM (its clipper)
against port/asm/polygon.c and port/asm/polyclip.c.

The renderer's line (2) and span (27) slots are probes on both sides that log what reaches
them: the page and the endpoints of each line, and each span table as it is when the slot
is called (the table's place in the span buffer, its header and rows). The original's
probes are INT F2h / INT F3h stubs served here. The span buffer is a block of each side's
own: a whole segment and the paragraph before it, so that rows anywhere in it, and the
header in front of them, land in the block; it starts with the same random bytes on both
sides, so rows the edges leave alone show stale data alike.

After every call the result, the module's state words, the probe log, the graphics context
(the vertex arrays, the count and the clip rectangle among it) and the span buffer must
match.
"""

from __future__ import annotations

import math
import random
import struct

import pytest

CTX_SIZE = 0xAB6
CTX_CLIP = 0x03  # bClip_enabled, then clip.xmin, xmax, ymin, ymax
CTX_FILL = 0x0C  # bGfx_fill_enabled, bGfx_fill_color, bGfx_outline_color
CTX_DST = 0x18  # wGfxBlitDstPage
CTX_POLY = 0xAC  # pPolygonX, then pPolygonY, ...XClipped, ...YClipped, ...XSaved, ...YSaved
CTX_COUNT = 0x19C  # nPolygonClipVertexCount
CTX_HOST_PTR = slice(0x19E, 0x1A2)  # pPaletteScratchBuf: a far pointer, natively a host one
CTX_YRES_DOUBLED = 0x6E8
CTX_SPAN_SEG = 0xAB2  # wSpanTableBufSeg: each side's own
SPAN_BLOCK = 0x10010
TABLE_ROWS_LOGGED = 1024
STATE_SIZE = 26  # g_wDrawPolyTopVertexOff .. g_bDrawPolyReplayPending
STATE_RET_SEG = 0x12  # g_wDrawPolySpanBufRetSeg
XS_AT, YS_AT = 0xF000, 0xF100  # the vertex arrays, in DGROUP under the test's stack
PAGES = (0xA000, 0xA400, 0xA800)
PROBES = bytes([0xCD, 0xF2, 0xCB, 0x90, 0xCD, 0xF3, 0xCB])  # INT F2h; RETF / INT F3h; RETF
IDIV_BP, INT_F0 = bytes([0xF7, 0xFD]), bytes([0xCD, 0xF0])
CLIPPER_SIZE = 1153


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def _trunc_div(a: int, d: int) -> int:
    q = abs(a) // abs(d)
    return q if (a < 0) == (d < 0) else -q


def _masked(ctx: bytes) -> bytes:
    b = bytearray(ctx)
    b[CTX_HOST_PTR] = bytes(4)
    b[CTX_SPAN_SEG : CTX_SPAN_SEG + 2] = bytes(2)
    return bytes(b)


class Pair:
    """The original, with its probes and span block, and the native side, in one state."""

    def __init__(self, m, native, rng: random.Random):
        self.m, self.native = m, native
        self.log = bytearray()
        probes = m.alloc(16)
        m.write((probes, 0), PROBES)
        vt_seg, vt_off = m.sym("_g_renderer_vtable")
        m.write((vt_seg, vt_off + 4 * 2), struct.pack("<HH", 0, probes))
        m.write((vt_seg, vt_off + 4 * 27), struct.pack("<HH", 4, probes))
        m.interrupts[0xF2] = self._probe_line
        m.interrupts[0xF3] = self._probe_spans
        self.ctx = m.sym("_g_graphics_context")
        self.block = m.alloc(SPAN_BLOCK)
        self.span_seg = self.block + 1
        span = rng.randbytes(SPAN_BLOCK)
        m.write((self.block, 0), span)
        native.call("polygon.span", span)
        # The original's context, but the native host pointer and each side's span segment.
        original = bytearray(self.read_ctx())
        native_ctx = native.peek("g_gfxContextBlock")
        original[CTX_HOST_PTR] = native_ctx[CTX_HOST_PTR]
        native.poke("g_gfxContextBlock", bytes(original))
        self.set_ctx_original(CTX_SPAN_SEG, struct.pack("<H", self.span_seg))
        self.divide_overflows = 0

    # --- the original's probes -------------------------------------------------------

    def _probe_line(self, m) -> None:
        self.log += struct.pack("<6H", 2, *(m.reg(r) for r in ("es", "bx", "cx", "dx", "si")))

    def _probe_spans(self, m) -> None:
        at = (m.reg("es") << 4) + m.reg("si") - (self.block << 4)
        assert 0 <= at <= SPAN_BLOCK - 4, f"span table outside the span block (at {at:#x})"
        rows = struct.unpack("<H", self.read_block(at + 2, 2))[0]
        size = min(4 + 4 * min(rows, TABLE_ROWS_LOGGED), SPAN_BLOCK - at)
        self.log += struct.pack("<HI", 27, at) + self.read_block(at, size)

    def read_block(self, at: int, size: int) -> bytes:
        return self.m.read((self.block + at // 16, at % 16), size)

    # --- state -------------------------------------------------------------------------

    def read_ctx(self) -> bytes:
        return self.m.read(self.ctx, CTX_SIZE)

    def set_ctx_original(self, off: int, data: bytes) -> None:
        self.m.write((self.ctx[0], self.ctx[1] + off), data)

    def set_ctx(self, off: int, data: bytes) -> None:
        self.set_ctx_original(off, data)
        block = bytearray(self.native.peek("g_gfxContextBlock"))
        block[off : off + len(data)] = data
        self.native.poke("g_gfxContextBlock", bytes(block))

    def set_state(self, rng: random.Random, clip_on: bool | None = None) -> str:
        if clip_on is None:
            clip_on = rng.random() < 0.6
        enabled = rng.choice((1, 1, 0x80)) if clip_on else 0
        if rng.random() < 0.3:
            xmin, xmax, ymin, ymax = 13, 306, 11, 131  # the 3-D view
        else:
            xmin, ymin = rng.randrange(-50, 300), rng.randrange(-50, 190)
            xmax, ymax = xmin + rng.randrange(-5, 330), ymin + rng.randrange(-5, 210)
        clip = bytes([enabled]) + struct.pack("<4h", xmin, xmax, ymin, ymax)
        fill = bytes([rng.choice((0, 1, 1, 1, 0x40)), rng.randrange(3), rng.randrange(3)])
        doubled = rng.choice((0, 0, 0, 1))
        self.set_ctx(CTX_CLIP, clip)
        self.set_ctx(CTX_FILL, fill)
        self.set_ctx(CTX_YRES_DOUBLED, bytes([doubled]))
        self.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))
        return f"clip {clip.hex()} fill {fill.hex()} doubled {doubled}"

    def kind(self, seg: int) -> int:
        return 0 if seg == 0 else 1 if seg == self.span_seg else 2

    def check(self, reply_native: bytes, reply_original: bytes, what: str) -> None:
        assert reply_native == reply_original, f"result, state or probes differ: {what}"
        ctx_o, ctx_n = _masked(self.read_ctx()), _masked(self.native.peek("g_gfxContextBlock"))
        if ctx_o != ctx_n:
            at = next(i for i in range(CTX_SIZE) if ctx_o[i] != ctx_n[i])
            raise AssertionError(
                f"graphics context differs at {at:#x} (original {ctx_o[at]:#04x}, "
                f"native {ctx_n[at]:#04x}): {what}"
            )
        span_o, span_n = self.m.read((self.block, 0), SPAN_BLOCK), self.native.call("polygon.span")
        if span_o != span_n:
            at = next(i for i in range(SPAN_BLOCK) if span_o[i] != span_n[i])
            raise AssertionError(
                f"span buffer differs at {at:#x} (original {span_o[at]:#04x}, "
                f"native {span_n[at]:#04x}): {what}"
            )

    # --- calls -------------------------------------------------------------------------

    def draw(self, xs: list[int], ys: list[int], count: int | None = None, what: str = "") -> None:
        if count is None:
            count = len(xs)
        m = self.m
        m.write((m.data_seg, XS_AT), struct.pack(f"<{len(xs)}H", *(v & 0xFFFF for v in xs)))
        m.write((m.data_seg, YS_AT), struct.pack(f"<{len(ys)}H", *(v & 0xFFFF for v in ys)))
        self.log = bytearray()
        ax = m.call_far("draw_polygon", count & 0xFFFF, XS_AT, YS_AT).ax
        state = bytearray(m.read("_g_wDrawPolyTopVertexOff", STATE_SIZE))
        ret_seg = struct.unpack_from("<H", state, STATE_RET_SEG)[0]
        state[STATE_RET_SEG : STATE_RET_SEG + 2] = struct.pack("<H", self.kind(ret_seg))
        original = struct.pack("<H", self.kind(ax)) + bytes(state) + bytes(self.log)
        n = max(count, 0)
        payload = struct.pack(f"<h{2 * n}H", count, *(v & 0xFFFF for v in xs[:n] + ys[:n]))
        native = self.native.call("polygon.draw", payload)
        self.check(native, original, f"draw {count}: {list(zip(xs, ys))} {what}")

    def clip(self, xs: list[int], ys: list[int], what: str = "") -> None:
        n = len(xs)
        self.set_ctx(CTX_COUNT, struct.pack("<h", n))
        self.set_ctx(CTX_POLY, struct.pack(f"<{n}H", *(v & 0xFFFF for v in xs)))
        self.set_ctx(CTX_POLY + 40, struct.pack(f"<{n}H", *(v & 0xFFFF for v in ys)))
        self.log = bytearray()
        self.m.call_far("polygon_clip_viewport")
        self.native.call("polygon.clip")
        self.check(b"", bytes(self.log), f"clip {list(zip(xs, ys))} {what}")

    def divide_by_handler_rule(self) -> None:
        """Swap the clipper's IDIV BP instructions (one per cut: 16) for INT F0h, whose
        handler divides here by the divide-error handler's rule: Unicorn turns a second
        divide fault it did not deliver itself into a double fault."""
        seg, off = self.m.sym("polygon_clip_viewport")
        code = self.m.read((seg, off), CLIPPER_SIZE)
        sites = [k for k in range(CLIPPER_SIZE - 1) if code[k : k + 2] == IDIV_BP]
        assert len(sites) == 16, f"IDIV BP sites in the clipper: {sites}"
        for k in sites:
            self.m.write((seg, off + k), INT_F0)
        self.m.interrupts[0xF0] = self._idiv_bp

    def _idiv_bp(self, machine) -> None:
        dxax = _s16(machine.reg("dx")) << 16 | machine.reg("ax")
        divisor = _s16(machine.reg("bp"))
        q = _trunc_div(dxax, divisor)
        if not -0x8000 <= q <= 0x7FFF:
            self.divide_overflows += 1
            while not -0x8000 <= q <= 0x7FFF:
                dxax >>= 1
                q = _trunc_div(dxax, divisor)
        machine.set_reg("ax", q)
        machine.set_reg("dx", dxax - q * divisor)


@pytest.fixture
def pair(krondor, fresh_native) -> Pair:
    return Pair(krondor, fresh_native, random.Random(99))


# --- polygons -------------------------------------------------------------------------


def _convex(rng: random.Random, n: int, box: tuple[int, int, int, int]) -> tuple[list, list]:
    x0, y0, x1, y1 = box
    cx, cy = rng.randrange(x0, x1), rng.randrange(y0, y1)
    rx, ry = rng.randrange(2, (x1 - x0) // 2), rng.randrange(2, (y1 - y0) // 2)
    angles = sorted(rng.random() * 2 * math.pi for _ in range(n))
    if rng.random() < 0.5:
        angles.reverse()
    xs = [round(cx + rx * math.cos(a)) for a in angles]
    ys = [round(cy + ry * math.sin(a)) for a in angles]
    return xs, ys


def _scattered(rng: random.Random, n: int, box: tuple[int, int, int, int]) -> tuple[list, list]:
    x0, y0, x1, y1 = box
    return [rng.randrange(x0, x1) for _ in range(n)], [rng.randrange(y0, y1) for _ in range(n)]


def _polygon(rng: random.Random, box: tuple[int, int, int, int]) -> tuple[list, list]:
    n = rng.choice((3, 3, 4, 4, 4, 5, 6, 7, 8, 10, 12))
    xs, ys = (_convex if rng.random() < 0.7 else _scattered)(rng, n, box)
    r = rng.random()
    if r < 0.1:  # a vertex repeated, or the last one on the first
        k = rng.randrange(n)
        xs.insert(k, xs[k])
        ys.insert(k, ys[k])
    elif r < 0.15:
        xs.append(xs[0])
        ys.append(ys[0])
    elif r < 0.25:  # some edges level, upright or at 45 degrees
        for k in range(1, n):
            choice = rng.randrange(3)
            if choice == 0:
                ys[k] = ys[k - 1]
            elif choice == 1:
                xs[k] = xs[k - 1]
            else:
                d = abs(ys[k] - ys[k - 1])
                xs[k] = xs[k - 1] + rng.choice((d, -d))
    return xs, ys


EDGES = [
    # two vertices, one row, one point, collinear
    ([10, 50], [20, 60]),
    ([30, 30], [5, 90]),
    ([10, 40, 25], [50, 50, 50]),
    ([40, 10, 25, 33], [77, 77, 77, 77]),
    ([7, 7, 7], [9, 9, 9]),
    ([10, 20, 30], [10, 20, 30]),
    ([10, 10, 10], [0, 5, 9]),
    ([0, 30, 60], [0, 10, 20]),
    # the same slope on both sides of the top vertex: filled twice
    ([10, 14, 30, 18], [0, 4, 30, 8]),
    ([10, 10, 40, 10], [0, 5, 20, 9]),
    ([50, 40, 0, 30], [10, 20, 60, 30]),
    # level top and bottom edges, upright and 45-degree sides
    ([0, 100, 100, 0], [0, 0, 80, 80]),
    ([20, 60, 100, 60], [50, 10, 50, 90]),
    ([0, 300, 150], [100, 101, 102]),
    ([5, 6, 200], [0, 150, 151]),
    ([150, 10, 290], [0, 199, 199]),
    # repeats, the closing vertex, a triangle drawn as a span of one row
    ([10, 10, 50, 50, 30, 10], [10, 10, 10, 60, 80, 10]),
    ([90, 20, 20, 160], [40, 40, 40, 41]),
    # three vertices that leave two once repeats are dropped: a line
    ([10, 60, 10], [20, 70, 20]),
    ([10, 10, 60], [20, 20, 70]),
    ([10, 60, 60], [20, 70, 70]),
]


def test_edge_cases(pair):
    rng = random.Random(1)
    for i, (xs, ys) in enumerate(EDGES):
        for fill in (b"\x01\x02\x02", b"\x01\x02\x01", b"\x00\x02\x01"):
            for clip_on in (False, True):
                pair.set_state(rng, clip_on)
                pair.set_ctx(CTX_FILL, fill)
                pair.set_ctx(CTX_CLIP, bytes([int(clip_on)]) + struct.pack("<4h", 0, 319, 0, 199))
                for flip in (False, True):  # both orders round
                    vx, vy = (xs[::-1], ys[::-1]) if flip else (xs, ys)
                    pair.draw(vx, vy, what=f"edge case {i} fill {fill.hex()} clip {clip_on}")


def test_counts(pair):
    # Under three vertices nothing is filled; a negative count leaves the arrays and the
    # count alone (a triangle first, so that there is a count to keep).
    rng = random.Random(2)
    for count in (3, -1, -0x8000, 3, 0, 3, 1, 2):
        pair.set_state(rng)
        xs, ys = (
            [rng.randrange(-20, 340) for _ in range(3)],
            [rng.randrange(0, 200) for _ in range(3)],
        )
        pair.draw(xs, ys, count, what=f"count {count}")


def test_two_left_by_the_clipper(pair):
    # The clipper leaves exactly two vertices only against an inverted rectangle: an edge
    # from a vertex left of both bounds to one right of both, the third past both. Two
    # vertices draw a line, never the span a row of them would make.
    rng = random.Random(7)
    for fill in (b"\x01\x02\x02", b"\x01\x02\x01", b"\x00\x02\x01"):
        pair.set_state(rng, True)
        pair.set_ctx(CTX_FILL, fill)
        pair.set_ctx(CTX_CLIP, b"\x01" + struct.pack("<4h", 100, 50, 0, 199))
        pair.draw([10, 200, 70], [50, 50, 40], what=f"fill {fill.hex()}")
        pair.draw([10, 200, 70], [50, 90, 40], what=f"fill {fill.hex()}")


def test_extreme_slopes(pair):
    # x across more than 32767: a level edge at the top vertex turns rightwards in 16 bits,
    # so its stand-in slopes (7FFFh, and 8001h negated) meet an edge whose slope is one
    # less, which only the exact stand-in tells from the same slope.
    rng = random.Random(8)
    cases = [
        ([-30000, 30000, -2770], [10, 10, 11]),  # next level, rightwards: 7FFFh
        ([-2770, 30000, -30000], [11, 10, 10]),  # previous level, rightwards: 7FFFh
        ([-2766, 30000, 29990], [11, 10, 10]),  # previous level, leftwards: 8001h
    ]
    for xs, ys in cases:
        for fill in (b"\x01\x02\x02", b"\x01\x02\x01"):
            pair.set_state(rng, False)
            pair.set_ctx(CTX_FILL, fill)
            pair.draw(xs, ys, what=f"fill {fill.hex()}")


def test_twenty_vertices(pair):
    # With an outline of its own colour, the copy of the outline's vertices closes the
    # polygon one past the arrays: the 21st y lands on the vertex count.
    rng = random.Random(3)
    for fill in (b"\x01\x02\x02", b"\x01\x02\x01", b"\x00\x02\x01"):
        for clip_on in (False, True):
            pair.set_state(rng, clip_on)
            pair.set_ctx(CTX_FILL, fill)
            xs, ys = _convex(rng, 20, (20, 20, 300, 180))
            ys[0] = 7
            pair.draw(xs, ys, what=f"20 vertices fill {fill.hex()} clip {clip_on}")


def test_random_polygons(pair):
    rng = random.Random(4)
    for i in range(1500):
        clip_on = rng.random() < 0.6
        state = pair.set_state(rng, clip_on)
        # Unclipped, an edge spans at most 400 rows (the original's stub chain).
        box = (-400, -400, 800, 800) if clip_on else (-150, -90, 470, 300)
        xs, ys = _polygon(rng, box)
        pair.draw(xs, ys, what=f"{i}: {state}")


def test_clipper(pair):
    # polygon_clip_viewport alone: convex and scattered polygons against clip rectangles
    # around, inside and across them, inverted ones too, vertices on the bounds.
    rng = random.Random(5)
    for i in range(1500):
        xmin, ymin = rng.randrange(-60, 300), rng.randrange(-60, 200)
        xmax, ymax = xmin + rng.randrange(-20, 330), ymin + rng.randrange(-20, 220)
        pair.set_ctx(CTX_CLIP, b"\x01" + struct.pack("<4h", xmin, xmax, ymin, ymax))
        n = rng.choice((0, 1, 2, 3, 4, 5, 6, 8, 10))
        if rng.random() < 0.6:
            xs, ys = _convex(rng, max(n, 1), (-200, -200, 520, 400))
            xs, ys = xs[:n], ys[:n]
        else:
            xs, ys = _scattered(rng, n, (-200, -200, 520, 400))
        for k in range(n):  # some vertices exactly on a bound
            if rng.random() < 0.1:
                xs[k] = rng.choice((xmin, xmax))
            if rng.random() < 0.1:
                ys[k] = rng.choice((ymin, ymax))
        pair.clip(xs, ys, f"{i}: clip ({xmin}, {ymin})-({xmax}, {ymax})")


def test_divide_overflow(pair):
    """Coordinates across the 16-bit range: the clipper's and draw_line's quotients
    overflow, and the divide-error handler's halving decides where the cut lands."""
    pair.divide_by_handler_rule()
    rng = random.Random(6)
    for i in range(600):
        xmin, ymin = rng.randrange(-0x8000, 0x7000), rng.randrange(-0x8000, 0x7000)
        xmax = min(0x7FFF, xmin + rng.randrange(0x10000))
        ymax = min(0x7FFF, ymin + rng.randrange(0x10000))
        pair.set_ctx(CTX_CLIP, b"\x01" + struct.pack("<4h", xmin, xmax, ymin, ymax))
        n = rng.choice((3, 4, 5, 6))
        xs, ys = _scattered(rng, n, (-0x8000, -0x8000, 0x8000, 0x8000))
        pair.clip(xs, ys, f"{i}: clip ({xmin}, {ymin})-({xmax}, {ymax})")
    assert pair.divide_overflows > 20
