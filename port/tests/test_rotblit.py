"""ROTBLIT.ASM (a sprite drawn rotated and scaled: the TTM scripts' opcode A5A7h) against
port/asm/rotblit.c.

The renderer's slot 36, which draws each span, is a stand-in on both sides that logs it: the
original's an INT F1h + RETF served here, the port's in bak_difftest. A span is its page, row
offset, x, count and row base, the sprite it names, where its source table starts in the X
source table, and the table's words it covers. After each sprite the corners' block (DGROUP
2F4Ch-2F63h: the corners and which is which), the source tables, the VGA's registers and what
read_4words_from_2tables gives are compared too. The original's scratch frame lies at the
same place for every call, as the port's frame array persists, so a frame word read before
it is written (past a table's end) is the last call's on both sides.

Sprites come in the sizes the scripts use and others, at any angle (the axes and near them
too), at scales from tiny to large and either sign (now and then the upright edge far past the
screen, the quad still narrow enough for the tables), about their centres (as TTM.C passes the
half sizes) or from a corner, under clip rectangles within the screen's width and a little past
its height (the row offset table is read past its end as the original reads it), on any page,
with the planar adapters' graphics-controller reset or without. The six IDIVs that can overflow
are swapped for interrupts that divide here by the divide-error handler's rule, as test_drawline
does; with the game's own half sizes none overflows, so test_divide_overflows gives half sizes
that make the centre's IDIVs overflow.
"""

from __future__ import annotations

import random
import struct

import pytest

from harness.machine import Machine
from harness.vga import PlanarVga

RECORD_AT = 0xF200  # the sprite's ImageRecord, in DGROUP's free space below the stack
OUTS_AT = 0xF210  # read_4words_from_2tables' four words
STUB_AT = 0x0000  # the slot 36 stand-in, in a segment of its own: INT F1h; RETF
CTX_CLIP, CTX_DST, CTX_ADAPTER, CTX_MODE = 0x03, 0x18, 0x1D, 0x21
CTX_WIDTH, CTX_LUT = 0x6EA, 0x6F2
PAGES = (0xA000, 0xA400, 0xA800, 0xAC00)
STATE_SIZE = 24
TABLES_SIZE = 2 * (640 + 400)
# jmp_via_2f48's IDIVs (offset from the routine, the instruction, its divisor), each swapped for
# INT n (and a NOP where the instruction is 3 bytes): the sprite's height and width under the
# half sizes, the right edge's slope and the ratio of the sine and cosine on each path.
IDIVS = (
    (0x5A, bytes([0xF7, 0x7F, 0x08]), "[bx+8]"),
    (0x8C, bytes([0xF7, 0x7F, 0x06]), "[bx+6]"),
    (0x2ED, bytes([0xF7, 0xF9]), "cx"),
    (0x317, bytes([0xF7, 0xFB]), "bx"),
    (0x465, bytes([0xF7, 0xF9]), "cx"),
    (0x48F, bytes([0xF7, 0xFB]), "bx"),
)


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def _trunc_div(a: int, d: int) -> int:
    q = abs(a) // abs(d)
    return q if (a < 0) == (d < 0) else -q


class Sides:
    def __init__(self, image, native):
        self.m = m = Machine(image, vga=True)
        self.vga = PlanarVga(m)
        self.n = native
        self.dseg = m.data_seg
        self.ctx = m.sym("_g_graphics_context")
        self.block = bytearray(native.peek("g_gfxContextBlock"))
        self.state_at = m.sym("_g_anRotBlitVertX")
        self.x_at = m.sym("_g_aStretchXSrcTable")
        assert m.sym("_g_aStretchYSrcTable")[1] == self.x_at[1] + 1280, "the tables lie apart"
        assert m.sym("_g_anRotBlitVertY")[1] == self.state_at[1] + 8
        assert m.sym("_g_wRotBlitIdxOppCW")[1] == self.state_at[1] + 22
        stub = m.alloc(16)
        m.write((stub, STUB_AT), bytes([0xCD, 0xF1, 0xCB]))
        m.write(
            (m.sym("_g_renderer_vtable")[0], m.sym("_g_renderer_vtable")[1] + 0x90),
            struct.pack("<HH", STUB_AT, stub),
        )
        m.interrupts[0xF1] = self._span
        seg, off = m.sym("_jmp_via_2f48")
        for n, (at, code, divisor) in enumerate(IDIVS):
            assert m.read((seg, off + at), len(code)) == code, f"no IDIV {divisor} at +{at:#x}"
            m.write((seg, off + at), bytes([0xCD, 0xF3 + n]) + b"\x90" * (len(code) - 2))
            m.interrupts[0xF3 + n] = self._divider(divisor)
        self.log = bytearray()
        self.overflows = 0

    def _span(self, m) -> None:
        sprite, row_base, table = struct.unpack("<3H", m.read((m.reg("ss"), m.reg("sp") + 4), 6))
        count = _s16(m.reg("cx"))
        self.log += struct.pack(
            "<7H",
            m.reg("es"),
            m.reg("di"),
            m.reg("dx"),
            m.reg("cx"),
            row_base,
            int(sprite == RECORD_AT),
            (table - self.x_at[1]) // 2 & 0xFFFF,
        )
        self.log += m.read((m.reg("ss"), table), 2 * max(count, 0))

    def _divider(self, divisor: str):
        def divide(m) -> None:
            if divisor.startswith("["):
                v = m.u16((m.reg("ds"), m.reg("bx") + int(divisor[4])))
            else:
                v = m.reg(divisor)
            d = _s16(v)
            dxax = _s16(m.reg("dx")) << 16 | m.reg("ax")
            assert d != 0, "IDIV by zero"
            q = _trunc_div(dxax, d)
            if not -0x8000 <= q <= 0x7FFF:
                self.overflows += 1
                while not -0x8000 <= q <= 0x7FFF:
                    dxax >>= 1
                    q = _trunc_div(dxax, d)
            m.set_reg("ax", q)
            m.set_reg("dx", dxax - q * d)

        return divide

    def set_ctx(self, off: int, data: bytes) -> None:
        self.m.write((self.ctx[0], self.ctx[1] + off), data)
        self.block[off : off + len(data)] = data
        self.n.poke("g_gfxContextBlock", bytes(self.block))

    def draw(self, width: int, height: int, *args: int) -> str:
        self.m.write((self.dseg, RECORD_AT), struct.pack("<HHHhh", 0x1234, 0, 0, width, height))
        self.log = bytearray()
        self.m.call_far("_jmp_via_2f48", RECORD_AT, *(a & 0xFFFF for a in args))
        got = self.n.call(
            "rotblit.draw",
            struct.pack("<9H", width & 0xFFFF, height & 0xFFFF, *(a & 0xFFFF for a in args)),
        )
        what = f"{width}x{height} at {args}"
        if bytes(self.log) != got:
            raise AssertionError(
                f"{what}: the spans differ\n{_spans(bytes(self.log))[:6]}\n{_spans(got)[:6]}"
            )
        assert self.n.peek("g_anRotBlitState") == self.m.read(self.state_at, STATE_SIZE), (
            f"{what}: the corners"
        )
        assert self.n.peek("g_aStretchSrcTables") == self.m.read(self.x_at, TABLES_SIZE), (
            f"{what}: the source tables"
        )
        assert self.n.call("heg.regs") == self.vga.regs(), f"{what}: the VGA's registers"
        return what


def _spans(log: bytes) -> list[tuple]:
    out, at = [], 0
    while at < len(log):
        head = struct.unpack_from("<7H", log, at)
        count = _s16(head[3])
        at += 14
        out.append(head + struct.unpack_from(f"<{max(count, 0)}H", log, at))
        at += 2 * max(count, 0)
    return out


def _sprite(rng: random.Random) -> tuple[int, int]:
    if rng.random() < 0.4:  # the sizes of the scripts' sprites (TVRN2.TTM's tankard: 55x31)
        return rng.choice(((55, 31), (31, 55), (16, 16), (40, 8), (8, 40), (64, 48)))
    return rng.randrange(1, 120), rng.randrange(1, 120)


def _angle(rng: random.Random) -> int:
    r = rng.random()
    if r < 0.06:  # the axes exactly, where two corners lie level and the tie-breaks decide
        return rng.choice((0, 0x4000, 0x8000, 0xC000))
    if r < 0.25:  # near the axes, where the sine or cosine is small
        return (rng.choice((0, 0x4000, 0x8000, 0xC000)) + rng.randrange(-300, 301)) & 0xFFFF
    if r < 0.35:  # TVRN2.TTM's angle
        return 0xD020
    return rng.randrange(0x10000)


def _scale(rng: random.Random, size: int) -> int:
    r = rng.random()
    if r < 0.4:
        s = size
    elif r < 0.7:
        s = rng.randrange(1, 260)
    elif r < 0.85:
        s = rng.randrange(1, 6)
    else:
        s = rng.randrange(1, 400)
    return -s if rng.random() < 0.1 else s


def _clip(sides: Sides, rng: random.Random) -> None:
    xmin, ymin = rng.randrange(-20, 200), rng.randrange(-20, 150)
    xmax, ymax = rng.randrange(xmin, 340), rng.randrange(ymin, 230)
    sides.set_ctx(CTX_CLIP, bytes([1]) + struct.pack("<4h", xmin, xmax, ymin, ymax))
    sides.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))
    sides.set_ctx(CTX_ADAPTER, bytes([rng.choice((2, 8, 0x0C, 0x0D))]))
    sides.set_ctx(CTX_MODE, bytes([rng.choice((0x10, 0xFF))]))


def _near_axis(angle: int) -> bool:
    return any(
        min((angle - a) & 0xFFFF, (a - angle) & 0xFFFF) < 0x200 for a in (0, 0x4000, 0x8000, 0xC000)
    )


def _sides(image, native) -> Sides:
    sides = Sides(image, native)
    sides.set_ctx(CTX_WIDTH, struct.pack("<H", 320))
    sides.set_ctx(CTX_LUT, struct.pack("<200H", *(80 * k for k in range(200))))
    regs = bytes(sides.vga.regs())
    sides.vga.load_regs(regs)
    native.call("heg.regs", regs)
    return sides


def _compare_corners(sides: Sides) -> None:
    sides.m.call_far("_read_4words_from_2tables", *(OUTS_AT + 2 * k for k in range(4)))
    assert sides.n.call("rotblit.corners") == sides.m.read((sides.dseg, OUTS_AT), 8)


@pytest.mark.parametrize("seed", range(3))
def test_rotated_sprites(krondor_image, fresh_native, seed):
    rng = random.Random(500 + seed)
    sides = _sides(krondor_image, fresh_native)
    spans = 0
    for i in range(500):
        if i % 10 == 0:
            _clip(sides, rng)
        width, height = _sprite(rng)
        sx, sy = _scale(rng, width), _scale(rng, height)
        angle = _angle(rng)
        if _near_axis(angle) and rng.random() < 0.4:  # the upright edge far past the screen
            if (angle + 0x2000) & 0x4000:
                sx = rng.randrange(600, 4000)
            else:
                sy = rng.randrange(600, 4000)
        if rng.random() < 0.03:
            sx = 0
        r = rng.random()
        half = (
            (width >> 1, height >> 1)
            if r < 0.6
            else (0, 0)
            if r < 0.85
            else (rng.randrange(-200, 200), rng.randrange(-200, 200))
        )
        x, y = rng.randrange(-120, 440), rng.randrange(-120, 320)
        sides.draw(width, height, x, y, angle, sx, sy, *half)
        spans += len(_spans(bytes(sides.log)))
        if i % 7 == 0:
            _compare_corners(sides)
    assert spans > 3000, f"only {spans} spans drawn"


def test_divide_overflows(krondor_image, fresh_native):
    """Half sizes far past the sprite's (TTM.C passes its own, whose IDIVs cannot overflow):
    the IDIV that moves the origin to the centre overflows, and the divide-error handler's
    halving decides by how much. Near the angle 0 the half height moves the origin up, near
    4000h the half width does; from an origin on the screen the quad then lies between rows
    -32767 and -16000, clear of the 16-bit wrap, so its walk is the ordinary one."""
    rng = random.Random(600)
    sides = _sides(krondor_image, fresh_native)
    for i in range(300):
        if i % 10 == 0:
            _clip(sides, rng)
        width, height = rng.randrange(1, 21), rng.randrange(1, 21)
        sx, sy = rng.randrange(100, 300), rng.randrange(100, 300)
        if i % 2:
            angle, half = rng.randrange(-0x400, 0x400) & 0xFFFF, (0, rng.randrange(10000, 32768))
        else:
            angle, half = 0x4000 + rng.randrange(-0x400, 0x400), (rng.randrange(10000, 32768), 0)
        sides.draw(
            width, height, rng.randrange(0, 300), rng.randrange(0, 320), angle, sx, sy, *half
        )
        if i % 7 == 0:
            _compare_corners(sides)
    assert sides.overflows > 250, f"only {sides.overflows} IDIVs overflowed"


def test_wide_flat_sprites(krondor_image, fresh_native):
    """Sprites up to 320 pixels wide and a few high, stretched wide and turned a degree or a few
    off an axis: the ratio of the cosine and the sine times the sprite's width passes 16 bits
    and saturates at FFFFh, the quad staying narrow enough for the X source table."""
    rng = random.Random(700)
    sides = _sides(krondor_image, fresh_native)
    spans = 0
    for i in range(300):
        if i % 10 == 0:
            _clip(sides, rng)
        width, height = rng.randrange(150, 321), rng.randrange(2, 11)
        sx, sy = rng.randrange(300, 600), rng.randrange(4, 25)
        turn = rng.randrange(0xC0, 0x300) * rng.choice((-1, 1))
        angle = (rng.choice((0, 0x8000)) + turn) & 0xFFFF
        half = (width >> 1, height >> 1) if rng.random() < 0.5 else (0, 0)
        sides.draw(
            width, height, rng.randrange(-100, 300), rng.randrange(-20, 220), angle, sx, sy, *half
        )
        spans += len(_spans(bytes(sides.log)))
        if i % 7 == 0:
            _compare_corners(sides)
    assert spans > 1000, f"only {spans} spans drawn"
