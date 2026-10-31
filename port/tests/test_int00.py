"""INT00.ASM (the divide-error handler) against port/asm/int00.c.

The original handler is installed in the interrupt table and a real DIV or IDIV faults
into it until the retried division fits; the translation's bak_div8/bak_idiv8 (and
bak_idiv16 for the word form) must give the same quotient and remainder. Divisions the
handler can never resolve (a zero divisor, an unsigned dividend the arithmetic shift
cannot shrink) hang the original and stop the translation, so they are left out.
"""

from __future__ import annotations

import random
import struct


def resolves(ax: int, divisor: int, signed: bool) -> bool:
    for _ in range(20):
        if divisor == 0:
            return False
        if signed:
            a = ax - 0x10000 if ax & 0x8000 else ax
            d = divisor - 0x100 if divisor & 0x80 else divisor
            if -128 <= int(a / d) <= 127:
                return True
        elif ax // divisor <= 0xFF:
            return True
        shifted = ((ax - 0x10000 if ax & 0x8000 else ax) >> 1) & 0xFFFF
        if shifted == ax:
            return False
        ax = shifted
    return False


def overflows(ax: int, divisor: int, signed: bool) -> bool:
    if signed:
        a = ax - 0x10000 if ax & 0x8000 else ax
        d = divisor - 0x100 if divisor & 0x80 else divisor
        return not -128 <= int(a / d) <= 127
    return ax // divisor > 0xFF


def original_divide(m, code: int, ax: int, divisor: int, signed: bool) -> int:
    """Run the division the way the CPU and the installed handler do: while it would
    fault, enter the handler with the frame a fault pushes (pointing at the DIV), which
    hands back the halved dividend in AX; then the DIV runs. (Unicorn escalates repeated
    faults it was not left to deliver itself, so the harness drives the retries.)"""
    div_ip = 4 if signed else 0
    handler = m.vector(0)
    while overflows(ax, divisor, signed):
        # The frame points at a copy of the instruction that never runs: Unicorn may not
        # stop at an address whose code it has already translated.
        r = m.call_interrupt(handler, ax=ax, bx=divisor, cs_frame=code, ip_frame=div_ip + 8)
        ax = r.ax
    return m.call_far((code, div_ip), ax=ax, bx=divisor).ax


def test_divide_recovery(krondor, native):
    m = krondor
    m.call_far("install_int00_handler")
    native.call("int00.install")
    assert native.peek("g_int00_installed") == m.read("_g_int00_installed", 1)
    code = m.alloc(16)
    # div bl; retf; idiv bl; retf, then the same two instructions as fault-frame targets
    m.write((code, 0), bytes([0xF6, 0xF3, 0xCB, 0x90, 0xF6, 0xFB, 0xCB, 0x90,
                              0xF6, 0xF3, 0xCB, 0x90, 0xF6, 0xFB, 0xCB, 0x90]))
    rng = random.Random(26)
    cases = [(0x1234, 0x01, False), (0xFE01, 0x3F, False), (0x7FFF, 0x02, False), (0x8000, 0x01, True),
             (0x7FFF, 0xFF, True), (0x0100, 0x01, False)]
    cases += [(rng.randrange(0x10000), rng.randrange(1, 0x100), rng.random() < 0.5)
              for _ in range(3000)]
    checked = 0
    for ax, divisor, signed in cases:
        if not resolves(ax, divisor, signed):
            continue
        got = original_divide(m, code, ax, divisor, signed)
        reply = native.call("int00.div", struct.pack("<BHB", signed, ax, divisor))
        assert reply == bytes([got & 0xFF, got >> 8]), (hex(ax), hex(divisor), signed)
        checked += 1
    assert checked > 2000


def test_install_restore(krondor, native):
    """The flag on both sides; the saved vector on the first install (0:0 here). A second
    install saves the original's own handler (restoring never wrote the vector back),
    a DOS address with no native counterpart."""
    m = krondor
    m.call_far("install_int00_handler")
    native.call("int00.install")
    for name, size in (("g_int00_installed", 1), ("g_wInt00SaveSeg", 2), ("g_wInt00SaveOff", 2)):
        assert native.peek(name) == m.read("_" + name, size), name
    for _ in range(2):
        m.call_far("restore_int00_handler")
        native.call("int00.restore")
        assert native.peek("g_int00_installed") == m.read("_g_int00_installed", 1)
        m.call_far("install_int00_handler")
        native.call("int00.install")
        assert native.peek("g_int00_installed") == m.read("_g_int00_installed", 1)
    assert m.vector(0) == (m.u16("_g_wInt00SaveSeg"), m.u16("_g_wInt00SaveOff"))


def _trunc_div(a: int, d: int) -> int:
    q = abs(a) // abs(d)
    return q if (a < 0) == (d < 0) else -q


def _s32(v: int) -> int:
    return v - (1 << 32) if v & 0x80000000 else v


def test_word_divide_recovery(krondor, native):
    """IDIV r16, as the line clipper divides: the handler halves DX:AX arithmetically
    until the quotient fits; bak_idiv16 must end with the same quotient and remainder."""
    m = krondor
    m.call_far("install_int00_handler")
    handler = m.vector(0)
    code = m.alloc(16)
    # idiv bx; retf, then the same instruction as the fault-frame target
    m.write((code, 0), bytes([0xF7, 0xFB, 0xCB, 0x90, 0xF7, 0xFB, 0xCB, 0x90]))
    rng = random.Random(27)
    cases = [(0x7FFFFFFF, 1), (-0x80000000, 1), (0x12345678, -0x8000), (-0x7654321, -2),
             (0x8000, 1), (-0x8001, 1), (-0x8000, 1), (0x7FFF, -1)]
    for _ in range(600):
        magnitude = rng.choice((16, 24, 32))
        dxax = rng.randrange(-(1 << (magnitude - 1)), 1 << (magnitude - 1))
        cases.append((dxax, rng.choice((1, -1)) * rng.randrange(1, 0x8000)))
    for dxax, divisor in cases:
        value = dxax
        while not -0x8000 <= _trunc_div(value, divisor) <= 0x7FFF:
            r = m.call_interrupt(handler, ax=value & 0xFFFF, dx=(value >> 16) & 0xFFFF,
                                 bx=divisor & 0xFFFF, cs_frame=code, ip_frame=4)
            value = _s32(r.dx << 16 | r.ax)
        r = m.call_far((code, 0), ax=value & 0xFFFF, dx=(value >> 16) & 0xFFFF,
                       bx=divisor & 0xFFFF)
        reply = native.call("int00.idiv16", struct.pack("<iH", dxax, divisor & 0xFFFF))
        assert reply == struct.pack("<HH", r.ax, r.dx), (hex(dxax), divisor)
