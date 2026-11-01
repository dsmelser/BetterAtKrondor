"""R3D.ASM's polygon clippers against port/asm/r3dclip.c.

The clippers are near routines in R3D's segment that run with DS there (their state) and
SS on DGROUP (they read the clip rectangle through SS). Both sides get the same polygon
arrays and vertex caches as two blocks laid out as in the original's segment: the low
words from g_aPolyInX to the end of g_aVertexScrY, the high words from g_aPolyInXHi to the
end of g_aVertexScrYHi. They start random but for the fields a case sets; after each call
both blocks, and the output offset the edge routines leave in DI, must match.

Their IDIVs overflow for far-apart coordinates, and the game's divide-error handler then
halves the dividend until the quotient fits. Unicorn turns a second divide fault into a
double fault, so each IDIV is swapped for an interrupt that divides here by the handler's
rule, as in test_drawline.py. A zero divisor, on which the original hangs (and the
translation stops), discards the case.
"""

from __future__ import annotations

import math
import random
import struct

import pytest
from capstone import CS_ARCH_X86, CS_MODE_16, Cs

from harness.machine import MachineError

LO_SIZE, HI_SIZE = 0xCBE, 0xA96
# Offsets in the low block ...
IN_X, IN_Y, CLIP_X, CLIP_Y, COUNT, POLY_VERTICES, CLIP_STATE = 0, 0x28, 0x50, 0x78, 0xA0, 0xA2, 0xCA
CAM_X, CAM_Y, CAM_Z, SCR_X, SCR_Y = 0x2C8, 0x4C6, 0x6C4, 0x8C2, 0xAC0
# ... and in the high block.
CAM_X_HI, CAM_Y_HI, CAM_Z_HI, SCR_X_HI, SCR_Y_HI = 0xA0, 0x29E, 0x49C, 0x69A, 0x898
LO_NAMES = (
    (IN_X, "aPolyInX"),
    (IN_Y, "aPolyInY"),
    (CLIP_X, "aPolyClipX"),
    (CLIP_Y, "aPolyClipY"),
    (COUNT, "nMeshPolyVertexCount"),
    (POLY_VERTICES, "polygonVertices"),
    (CLIP_STATE, "abPolygonClipState"),
    (CAM_X, "aVertexCamX"),
    (CAM_Y, "aVertexCamY"),
    (CAM_Z, "aVertexCamZ"),
    (SCR_X, "aVertexScrX"),
    (SCR_Y, "aVertexScrY"),
)
HI_NAMES = (
    (0, "aPolyInXHi"),
    (0x28, "aPolyInYHi"),
    (0x50, "aPolyClipXHi"),
    (0x78, "aPolyClipYHi"),
    (CAM_X_HI, "aVertexCamXHi"),
    (CAM_Y_HI, "aVertexCamYHi"),
    (CAM_Z_HI, "aVertexCamZHi"),
    (SCR_X_HI, "aVertexScrXHi"),
    (SCR_Y_HI, "aVertexScrYHi"),
)

# r3dclip.call's routine numbers.
ROUTINES = (
    "r3d_polygon_scissor_2d",
    "r3d_interp_vertex_at_zclip",
    "r3d_polygon_clip_near_z",
    "r3d_apply_viewport_offset",
    "r3d_clip_interp_x_edge",
    "r3d_clip_interp_y_edge",
    "r3d_clip_polygon_to_viewport",
    "r3d_interp_vertex_at_zclip_long",
    "r3d_polygon_clip_against_zplane",
    "r3d_polygon_clip_edge_intersect",
    "r3d_clip_polygon_edge_y",
    "r3d_polygon_clip_rect_2d",
    "r3d_poly_clip_calc_isect",
    "r3d_polygon_clip_near_z_lod2",
    "r3d_apply_viewport_offset_long",
)
EDGE_ROUTINES = {4, 5, 9, 10}  # they return the next output offset in DI
INT_IDIV = {"bp": 0xF0, "cx": 0xF2}  # the interrupts that stand in for IDIV BP / IDIV CX

VIEW_RECT = (13, 306, 11, 131)  # the 3-D view's clip rectangle


def _s16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _trunc_div(a: int, d: int) -> int:
    q = abs(a) // abs(d)
    return q if (a < 0) == (d < 0) else -q


class ZeroDivisor(Exception):
    pass


class State:
    """The two blocks, and the clip rectangle, view-plane distance and viewport centre."""

    def __init__(self, rng: random.Random):
        self.lo = bytearray(rng.randbytes(LO_SIZE))
        self.hi = bytearray(rng.randbytes(HI_SIZE))
        self.rect = VIEW_RECT
        self.view_plane = 0x80
        self.center = (160, 71)

    def word(self, off: int, value: int) -> None:
        struct.pack_into("<H", self.lo, off, value & 0xFFFF)

    def long(self, off: int, off_hi: int, value: int) -> None:
        struct.pack_into("<H", self.lo, off, value & 0xFFFF)
        struct.pack_into("<H", self.hi, off_hi, (value >> 16) & 0xFFFF)

    def count(self, n: int) -> None:
        self.word(COUNT, n)

    def poly16(self, points) -> None:
        for i, (x, y) in enumerate(points):
            self.word(IN_X + 2 * i, x)
            self.word(IN_Y + 2 * i, y)
        self.count(len(points))

    def poly32(self, points) -> None:
        for i, (x, y) in enumerate(points):
            self.long(IN_X + 2 * i, IN_X + 2 * i, x)
            self.long(IN_Y + 2 * i, IN_Y + 2 * i, y)
        self.count(len(points))

    def settings(self) -> bytes:
        return struct.pack(
            "<7H", *(v & 0xFFFF for v in (*self.rect, self.view_plane, *self.center))
        )


def _first_difference(want: bytes, got: bytes, names) -> str:
    at = next(i for i in range(len(want)) if want[i] != got[i])
    base, name = max((b, n) for b, n in names if b <= at)
    return (
        f"{name}[byte {at - base}] (block offset {at:#x}): "
        f"original {want[at]:#04x}, native {got[at]:#04x}"
    )


class Pair:
    """The original, its IDIVs dividing by the handler's rule, and the translation."""

    def __init__(self, m, native):
        self.m, self.n = m, native
        self.lo_at = m.sym("g_aPolyInX")
        self.hi_at = m.sym("g_aPolyInXHi")
        self.seg = self.lo_at[0]
        ctx_seg, ctx_off = m.sym("g_graphics_context")
        self.clip_at = (ctx_seg, ctx_off + 4)  # clip.xmin, xmax, ymin, ymax
        self.divide_overflows = 0
        for name in ROUTINES:
            self._patch_idivs(name)
        for reg, number in INT_IDIV.items():
            m.interrupts[number] = lambda machine, reg=reg: self._idiv(machine, reg)

    def _patch_idivs(self, name: str) -> None:
        seg, off = self.m.sym(name)
        rel_seg = self.m.image.symbol(name)[0]
        end = min(o for s, o in self.m.image.symbols.values() if s == rel_seg and o > off)
        code = self.m.read((seg, off), end - off)
        for insn in Cs(CS_ARCH_X86, CS_MODE_16).disasm(code, off):
            if insn.mnemonic == "idiv":
                assert insn.size == 2 and insn.op_str in INT_IDIV, f"{name}: {insn.op_str}"
                self.m.write((seg, insn.address), bytes([0xCD, INT_IDIV[insn.op_str]]))

    def _idiv(self, machine, reg: str) -> None:
        dxax = _s16(machine.reg("dx")) << 16 | machine.reg("ax")
        divisor = _s16(machine.reg(reg))
        if divisor == 0:
            machine._fail("IDIV by zero")
            return
        q = _trunc_div(dxax, divisor)
        if not -0x8000 <= q <= 0x7FFF:
            self.divide_overflows += 1
            while not -0x8000 <= q <= 0x7FFF:
                dxax >>= 1
                q = _trunc_div(dxax, divisor)
        machine.set_reg("ax", q)
        machine.set_reg("dx", dxax - q * divisor)

    def run(self, routine: int, state: State, cl=0, bx=0, si=0, di=0, bp=0) -> bool:
        """Both sides on the same state; False when the original divided by zero."""
        m = self.m
        m.write(self.lo_at, state.lo)
        m.write(self.hi_at, state.hi)
        m.write(self.clip_at, struct.pack("<4H", *(v & 0xFFFF for v in state.rect)))
        m.set_u16("g_nViewPlaneDist", state.view_plane)
        m.set_u16("g_nViewportCenterX", state.center[0])
        m.set_u16("g_nViewportCenterY", state.center[1])
        try:
            regs = m.call_near(ROUTINES[routine], ds=self.seg, cx=cl, bx=bx, si=si, di=di, bp=bp)
        except MachineError as e:
            if "IDIV by zero" in str(e):
                return False
            raise
        self.n.poke("r3dclip.lo", bytes(state.lo))
        self.n.poke("r3dclip.hi", bytes(state.hi))
        self.n.call("r3dclip.set", state.settings())
        reply = self.n.call(
            "r3dclip.call", bytes([routine]) + struct.pack("<5H", cl, bx, si, di, bp)
        )
        what = f"{ROUTINES[routine]} (CL={cl:02X} BX={bx:X} SI={si:X} DI={di:X} BP={bp:X})"
        want_lo, got_lo = m.read(self.lo_at, LO_SIZE), self.n.peek("r3dclip.lo")
        if want_lo != got_lo:
            raise AssertionError(f"{what}: {_first_difference(want_lo, got_lo, LO_NAMES)}")
        want_hi, got_hi = m.read(self.hi_at, HI_SIZE), self.n.peek("r3dclip.hi")
        if want_hi != got_hi:
            raise AssertionError(f"{what}: {_first_difference(want_hi, got_hi, HI_NAMES)}")
        if routine in EDGE_ROUTINES:
            assert struct.unpack("<H", reply)[0] == regs.di, f"{what}: DI after"
        return True


@pytest.fixture
def pair(krondor, fresh_native) -> Pair:
    return Pair(krondor, fresh_native)


# ---- inputs -----------------------------------------------------------------------------


def _rect(rng: random.Random) -> tuple[int, int, int, int]:
    """The 3-D view's rectangle most often, else any: inverted, or past 0 at either end."""
    r = rng.random()
    if r < 0.5:
        return VIEW_RECT
    if r < 0.85:
        x0, y0 = rng.randrange(-20, 300), rng.randrange(-20, 190)
        return x0, x0 + rng.randrange(0, 320), y0, y0 + rng.randrange(0, 200)
    return tuple(rng.randrange(-400, 400) for _ in range(4))


def _near(rng: random.Random, low: int, high: int, span: int) -> int:
    """A coordinate on a bound, one past it, or within span of the range."""
    r = rng.random()
    if r < 0.15:
        return rng.choice((low, high))
    if r < 0.25:
        return rng.choice((low - 1, high + 1))
    return rng.randrange(min(low, high) - span, max(low, high) + span + 1)


def _points(rng: random.Random, n: int, rect, span: int, bits: int):
    """n vertices of any shape around the rectangle; span 0 picks full-range ones."""
    limit = 1 << (bits - 1)
    out = []
    for _ in range(n):
        if span == 0:
            x, y = rng.randrange(-limit, limit), rng.randrange(-limit, limit)
        else:
            x = _near(rng, rect[0], rect[1], span)
            y = _near(rng, rect[2], rect[3], span)
        out.append((x, y))
    return out


def _convex(rng: random.Random, n: int, rect, radius: int):
    """n vertices of a convex polygon (an ellipse's) somewhere over the rectangle."""
    x0, x1 = sorted(rect[:2])
    y0, y1 = sorted(rect[2:])
    cx = rng.randrange(x0 - radius, x1 + radius + 1)
    cy = rng.randrange(y0 - radius, y1 + radius + 1)
    rx, ry = rng.randrange(1, radius + 1), rng.randrange(1, radius + 1)
    start = rng.random() * 2 * math.pi
    angles = sorted(rng.random() * 2 * math.pi for _ in range(n))
    if rng.random() < 0.5:
        angles.reverse()
    return [
        (cx + round(rx * math.cos(start + a)), cy + round(ry * math.sin(start + a))) for a in angles
    ]


def _polygon_cases(rng: random.Random, bits: int):
    """Polygons for the rectangle clippers whose passes stay within the 20-entry arrays:
    any shape up to 5 vertices, convex ones up to 14, and the explicit edges."""
    rect = VIEW_RECT
    x0, x1, y0, y1 = rect
    far = 1 << (bits - 2)
    yield rect, [(x0 + 1, y0 + 1)] * 0  # no vertices
    yield rect, [(x0 - 5, y0 + 5)]  # one vertex: nothing to do
    yield rect, [((x0 + x1) // 2 + 7 * i, (y0 + y1) // 2 + 3 * (i % 5)) for i in range(20)]
    # Ten edges across both sides: 20 vertices after x, all kept in y.
    yield (
        rect,
        [
            ((x0 - 50, y0 + 3 + 9 * i) if i % 2 == 0 else (x1 + 50, y0 + 3 + 9 * i))
            for i in range(10)
        ],
    )
    yield rect, [(x0 - 10, y0 + 5), (x0 - 20, y1 - 5), (x0 - 1, y0 + 50)]  # all left of xmin
    yield rect, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]  # on the rectangle
    yield rect, [(x0 - 1, y0 - 1), (x1 + 1, y0 - 1), (x1 + 1, y1 + 1), (x0 - 1, y1 + 1)]
    yield rect, [(x0 - 100, y0 + 10), (x1 + 100, y0 + 10)]  # a 2-gon across
    yield rect, [(x0 - far, y0), (x1 + far, y1), (x1 + far, y0 - far)]  # far apart
    for _ in range(150):  # far apart: the 16-bit differences wrap and IDIV overflows
        sign = rng.choice((-1, 1))
        yield (
            VIEW_RECT,
            [
                (sign * (-1) ** k * rng.randrange(12000, 1 << (bits - 1)), rng.randrange(-far, far))
                for k in range(rng.randrange(2, 6))
            ],
        )
    for _ in range(500):
        rect = _rect(rng)
        r = rng.random()
        if r < 0.55:
            span = rng.choice((5, 40, 300, 3000, 0))
            yield rect, _points(rng, rng.randrange(2, 6), rect, span, bits)
        else:
            radius = rng.choice((20, 150, 600, 4000 if bits == 16 else 200000))
            yield rect, _convex(rng, rng.randrange(3, 15), rect, radius)


def _clip_rect_case(pair: Pair, rng: random.Random, routine: int, bits: int) -> int:
    done = 0
    for rect, points in _polygon_cases(rng, bits):
        state = State(rng)
        state.rect = rect
        if bits == 16:
            state.poly16([(x & 0xFFFF, y & 0xFFFF) for x, y in points])
        else:
            state.poly32(points)
        if not points:
            state.count(rng.choice((0, -1, -300)))
        done += pair.run(routine, state)
    return done


def _ring(
    rng: random.Random, state: State, n: int, bits: int, behind: float, distinct: bool = False
) -> None:
    """A ring of n vertex offsets (some repeated, unless distinct) with clip states (3:
    behind the near plane) and camera coordinates on the matching side of the plane most
    often; screen slots at random."""
    state.view_plane = rng.choice((0x80, 0x100, 1, 1 << rng.randrange(0, 15)))
    pool = rng.sample(range(0, 510, 2), 20)
    offsets = [rng.choice(pool[: max(1, n - rng.choice((0, 0, 1, 2)))]) for _ in range(n)]
    if distinct or rng.random() < 0.6:
        offsets = pool[:n]
    for i, off in enumerate(offsets):
        state.word(POLY_VERTICES + 2 * i, off)
    limit = 1 << (bits - 1)
    for off in set(offsets):
        is_behind = rng.random() < behind
        state.lo[CLIP_STATE + off] = 3 if is_behind else rng.choice((0, 1, 2, 4, 0x83, 0xFF))
        r = rng.random()
        if r < 0.75:
            y = state.view_plane + (-rng.randrange(1, 600) if is_behind else rng.randrange(0, 3000))
        else:
            y = rng.randrange(-limit, limit)
        span = rng.choice((200, 5000, limit))
        x, z = rng.randrange(-span, span), rng.randrange(-span, span)
        if bits == 16:
            state.word(CAM_X + off, x)
            state.word(CAM_Y + off, y)
            state.word(CAM_Z + off, z)
        else:
            state.long(CAM_X + off, CAM_X_HI + off, x)
            state.long(CAM_Y + off, CAM_Y_HI + off, y)
            state.long(CAM_Z + off, CAM_Z_HI + off, z)
    state.count(n)


def _near_clip_case(pair: Pair, rng: random.Random, routine: int, bits: int) -> int:
    done = 0
    for i in range(600):
        state = State(rng)
        if i < 10:
            n, behind = (
                (1, 2, 3, 20, 20, 13, 13, 1, 2, 3)[i],
                (0, 0.5, 0.5, 0, 0, 0.5, 1, 1, 1, 0)[i],
            )
        else:
            n, behind = rng.randrange(1, 14), rng.choice((0.2, 0.5, 0.8))
        _ring(rng, state, n, bits, behind)
        done += pair.run(routine, state)
    return done


# ---- the near plane -------------------------------------------------------------------


def test_interp_vertex_at_zclip(pair):
    rng = random.Random(1)
    done = 0
    for i in range(1500):
        state = State(rng)
        _ring(rng, state, 2, 16, 0.5, distinct=True)
        clipped, other = struct.unpack_from("<2H", state.lo, POLY_VERTICES)
        if i % 3 == 0:
            clipped, other = rng.randrange(0, 510, 2), rng.randrange(0, 510, 2)
        done += pair.run(1, state, bx=clipped, si=other)
    assert done > 1300 and pair.divide_overflows > 20


def test_polygon_clip_near_z(pair):
    assert _near_clip_case(pair, random.Random(2), 2, 16) > 500


def test_interp_vertex_at_zclip_long(pair):
    rng = random.Random(3)
    done = 0
    for _ in range(1500):
        state = State(rng)
        _ring(rng, state, 2, 32, 0.5, distinct=True)
        clipped, other = struct.unpack_from("<2H", state.lo, POLY_VERTICES)
        done += pair.run(7, state, si=other, bp=clipped)
    assert done > 1300 and pair.divide_overflows > 20


def test_polygon_clip_against_zplane(pair):
    assert _near_clip_case(pair, random.Random(4), 8, 32) > 500


def test_poly_clip_calc_isect(pair):
    rng = random.Random(11)
    done = 0
    for _ in range(1500):
        state = State(rng)
        _ring(rng, state, 2, 32, 0.5, distinct=True)
        clipped, other = struct.unpack_from("<2H", state.lo, POLY_VERTICES)
        done += pair.run(12, state, si=other, bp=clipped)
    assert done == 1500


def test_polygon_clip_near_z_lod2(pair):
    assert _near_clip_case(pair, random.Random(12), 13, 32) == 600


# ---- the viewport's centre ------------------------------------------------------------


@pytest.mark.parametrize("routine", [3, 14])
def test_apply_viewport_offset(pair, routine):
    rng = random.Random(5 + routine)
    for i in range(300):
        state = State(rng)
        state.count(i % 21 if i < 42 else rng.randrange(0, 21))
        state.center = (
            rng.choice((160, rng.randrange(-0x8000, 0x8000))),
            rng.choice((71, rng.randrange(-0x8000, 0x8000))),
        )
        assert pair.run(routine, state)


# ---- the clip rectangle ---------------------------------------------------------------


def test_polygon_scissor_2d(pair):
    assert _clip_rect_case(pair, random.Random(7), 0, 16) == 659
    assert pair.divide_overflows > 100


@pytest.mark.parametrize("routine", [4, 5, 9, 10])
def test_clip_edges(pair, routine):
    # Every side byte (the low bound's bit wins, none emits nothing), the inside vertex
    # and the outside one as origin, and 32-bit coordinates: their low-word differences
    # overflow the IDIV of 4 and 5; 9 and 10 scale through r3d_ldiv_helper.
    rng = random.Random(8 + routine)
    done = 0
    src = (IN_X, IN_Y) if routine in (4, 9) else (CLIP_X, CLIP_Y)
    for i in range(1500):
        state = State(rng)
        state.rect = _rect(rng)
        span = rng.choice((300, 70000, 1 << 31))
        for k in range(20):
            for base in src:
                state.long(base + 2 * k, base + 2 * k, rng.randrange(-span, span))
        cl = i & 0xFF if i < 256 else rng.choice((1, 2, 3, 4, 8, 12, 0, rng.randrange(256)))
        other, origin, out = (rng.randrange(0, 40, 2) for _ in range(3))
        done += pair.run(routine, state, cl=cl, si=other, di=out, bp=origin)
    if routine in (4, 5):
        assert done > 1300 and pair.divide_overflows > 20
    else:
        assert done == 1500


def test_clip_polygon_to_viewport(pair):
    assert _clip_rect_case(pair, random.Random(10), 6, 32) > 600
    assert pair.divide_overflows > 30


def test_polygon_clip_rect_2d(pair):
    assert _clip_rect_case(pair, random.Random(13), 11, 32) == 659
