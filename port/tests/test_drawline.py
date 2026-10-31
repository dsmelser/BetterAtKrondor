"""DRAWLINE.ASM (the line clipper) and DRAWRECT.ASM (the filled rectangle) against
port/asm/drawline.c and port/asm/drawrect.c.

The renderer's line (2) and span (27) slots are replaced by probes on both sides that
log what reaches them: the page and the clipped endpoints, left end first; the span
table. The original's probes are a few instructions that append to a log in their own
segment.

Clipping divides with IDIV, which overflows for ends far apart and is then resolved by
the divide-error handler. Unicorn turns a second divide fault it did not deliver itself
into a double fault, so for those cases the clipper's IDIVs are swapped for an interrupt
that divides here by the handler's rule (test_int00 checks the rule against the handler).
"""

from __future__ import annotations

import random
import struct

# Each probe appends a 12-byte record to the log at cs:0080h (cs:007Eh holds its length):
# the slot, then ES BX CX DX SI (slot 2) or ES SI (slot 27).
LINE_PROBE = bytes([
    0x57,                                      # push di
    0x2E, 0x8B, 0x3E, 0x7E, 0x00,              # mov di, cs:[7Eh]
    0x2E, 0xC7, 0x85, 0x80, 0x00, 0x02, 0x00,  # mov word ptr cs:[di+80h], 2
    0x2E, 0x8C, 0x85, 0x82, 0x00,              # mov cs:[di+82h], es
    0x2E, 0x89, 0x9D, 0x84, 0x00,              # mov cs:[di+84h], bx
    0x2E, 0x89, 0x8D, 0x86, 0x00,              # mov cs:[di+86h], cx
    0x2E, 0x89, 0x95, 0x88, 0x00,              # mov cs:[di+88h], dx
    0x2E, 0x89, 0xB5, 0x8A, 0x00,              # mov cs:[di+8Ah], si
    0x2E, 0x83, 0x06, 0x7E, 0x00, 0x0C,        # add word ptr cs:[7Eh], 12
    0x5F,                                      # pop di
    0xCB,                                      # retf
])
SPAN_PROBE = bytes([
    0x57,                                      # push di
    0x2E, 0x8B, 0x3E, 0x7E, 0x00,              # mov di, cs:[7Eh]
    0x2E, 0xC7, 0x85, 0x80, 0x00, 0x1B, 0x00,  # mov word ptr cs:[di+80h], 27
    0x2E, 0x8C, 0x85, 0x82, 0x00,              # mov cs:[di+82h], es
    0x2E, 0x89, 0xB5, 0x84, 0x00,              # mov cs:[di+84h], si
    0x2E, 0x83, 0x06, 0x7E, 0x00, 0x0C,        # add word ptr cs:[7Eh], 12
    0x5F,                                      # pop di
    0xCB,                                      # retf
])
SPAN_PROBE_AT, LOG_LEN, LOG_AT = 0x30, 0x7E, 0x80

IDIV_BP, INT_F0 = bytes([0xF7, 0xFD]), bytes([0xCD, 0xF0])
IDIV_SITES = (0x44, 0x73, 0x9F, 0xCB)  # in draw_line: the y-min, x-min, y-max, x-max clips

CTX_CLIP = 0x03       # bClip_enabled, then clip.xmin, xmax, ymin, ymax
CTX_FILL = 0x0C       # bGfx_fill_enabled, bGfx_fill_color, bGfx_outline_color
CTX_DST = 0x18        # wGfxBlitDstPage
CTX_SPAN_SEG = 0xAB2  # wSpanTableBufSeg
PAGES = (0xA000, 0xA400, 0xA800)


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def _trunc_div(a: int, d: int) -> int:
    q = abs(a) // abs(d)
    return q if (a < 0) == (d < 0) else -q


class Sides:
    """The original with its probes and a span table, and the native side; context
    fields are set on both."""

    def __init__(self, m, native):
        self.m, self.native = m, native
        self.probes = m.alloc(LOG_AT + 12 * 8)
        m.write((self.probes, 0), LINE_PROBE)
        m.write((self.probes, SPAN_PROBE_AT), SPAN_PROBE)
        vt_seg, vt_off = m.sym("_g_renderer_vtable")
        m.write((vt_seg, vt_off + 4 * 2), struct.pack("<HH", 0, self.probes))
        m.write((vt_seg, vt_off + 4 * 27), struct.pack("<HH", SPAN_PROBE_AT, self.probes))
        self.ctx = m.sym("_g_graphics_context")
        self.span_seg = m.alloc(0x1000)
        m.write((self.ctx[0], self.ctx[1] + CTX_SPAN_SEG), struct.pack("<H", self.span_seg))
        self.divide_overflows = 0

    def divide_by_handler_rule(self) -> None:
        """Swap draw_line's IDIV BP instructions for INT F0h (see the module notes)."""
        seg, off = self.m.sym("draw_line")
        for at in IDIV_SITES:
            assert self.m.read((seg, off + at), 2) == IDIV_BP, f"no IDIV BP at +{at:#x}"
            self.m.write((seg, off + at), INT_F0)
        self.m.interrupts[0xF0] = self._idiv_bp

    def _idiv_bp(self, machine) -> None:
        """IDIV BP, an overflow resolved as the divide-error handler does: DX:AX halved
        (SAR DX,1 / RCR AX,1) until the quotient fits."""
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

    def set_ctx(self, off: int, data: bytes) -> None:
        self.m.write((self.ctx[0], self.ctx[1] + off), data)
        block = bytearray(self.native.peek("g_gfxContextBlock"))
        block[off:off + len(data)] = data
        self.native.poke("g_gfxContextBlock", bytes(block))

    def set_state(self, rng: random.Random) -> str:
        enabled = rng.choice((0, 1, 1, 1, 0x80))
        xmin, ymin = rng.randrange(-50, 300), rng.randrange(-50, 190)
        xmax, ymax = xmin + rng.randrange(-5, 330), ymin + rng.randrange(-5, 210)
        clip = bytes([enabled]) + struct.pack("<4h", xmin, xmax, ymin, ymax)
        fill = bytes([rng.choice((0, 1, 1, 0x40)), rng.randrange(3), rng.randrange(3)])
        self.set_ctx(CTX_CLIP, clip)
        self.set_ctx(CTX_FILL, fill)
        self.set_ctx(CTX_DST, struct.pack("<H", rng.choice(PAGES)))
        return f"clip {clip.hex()} fill {fill.hex()}"

    def log(self) -> bytes:
        """The original's probe log in the native command's format (and a fresh log)."""
        m, out = self.m, b""
        for k in range(0, m.u16((self.probes, LOG_LEN)), 12):
            slot, es, *regs = struct.unpack("<6H", m.read((self.probes, LOG_AT + k), 12))
            if slot == 2:
                out += struct.pack("<6H", slot, es, *regs)
            else:
                assert (es, regs[0]) == (self.span_seg, 0), "the spans are at ES:0"
                rows = m.u16((es, 2))
                out += struct.pack("<H", slot) + m.read((es, 0), 4 + 4 * rows)
        m.set_u16((self.probes, LOG_LEN), 0)
        return out


def _compare_line(s: Sides, x1: int, y1: int, x2: int, y2: int, what: str) -> None:
    s.m.call_far("draw_line", x1 & 0xFFFF, y1 & 0xFFFF, x2 & 0xFFFF, y2 & 0xFFFF)
    got = s.native.call("drawline.run", struct.pack("<4h", x1, y1, x2, y2))
    assert got == s.log(), f"line ({x1}, {y1})-({x2}, {y2}) {what}"


def test_lines(krondor, native):
    s = Sides(krondor, native)
    rng = random.Random(16)
    for i in range(1500):
        state = s.set_state(rng)
        x1, y1, x2, y2 = (rng.randrange(-400, 800) for _ in range(4))
        if rng.random() < 0.2:  # horizontal or vertical
            x2, y2 = (x1, y2) if rng.random() < 0.5 else (x2, y1)
        _compare_line(s, x1, y1, x2, y2, f"{i}: {state}")


def test_lines_divide_overflow(krondor, native):
    """Ends far apart: clipping overflows IDIV, and the handler's halving decides where
    the clipped end lands. Clip windows spanning the coordinate range let some of those
    lines through (against the screen nearly all are dropped either way)."""
    s = Sides(krondor, native)
    s.divide_by_handler_rule()
    rng = random.Random(18)
    s.set_state(rng)
    for i in range(600):
        xmin, ymin = rng.randrange(-0x8000, 0x7000), rng.randrange(-0x8000, 0x7000)
        xmax = min(0x7FFF, xmin + rng.randrange(0x10000))
        ymax = min(0x7FFF, ymin + rng.randrange(0x10000))
        clip = b"" + struct.pack("<4h", xmin, xmax, ymin, ymax)
        s.set_ctx(CTX_CLIP, clip)
        x1, y1, x2, y2 = (rng.randrange(-0x8000, 0x8000) for _ in range(4))
        _compare_line(s, x1, y1, x2, y2, f"{i}: clip {clip.hex()}")
    assert s.divide_overflows > 20


def test_rects(krondor, native):
    s = Sides(krondor, native)
    rng = random.Random(17)
    for i in range(1000):
        state = s.set_state(rng)
        x, y = rng.randrange(-100, 400), rng.randrange(-100, 300)
        w, h = rng.randrange(-5, 200), rng.randrange(-5, 150)
        krondor.call_far("draw_rect_filled", x & 0xFFFF, y & 0xFFFF, w & 0xFFFF, h & 0xFFFF)
        got = native.call("drawrect.run", struct.pack("<4h", x, y, w, h))
        assert got == s.log(), f"rect {i} ({x}, {y}, {w}x{h}) {state}"
