"""MOUSE.ASM against port/asm/mouse.c.

The original's INT 33h calls are recorded as (function, CX, DX) and compared with the
emulated driver's calls; functions 0-2 carry no arguments, and for 0Ch only whether a
handler is installed or removed counts. The event callback runs with a probe as the
game's handler on a planar VGA, so the register save/restore and the latch parking at
A000:FFFF are compared too.
"""

from __future__ import annotations

import random
import struct

import pytest

from harness.machine import Machine
from harness.vga import PlanarVga

CTX_ADAPTER = 0x1D
PROBE = bytes([0x2E, 0xC6, 0x06, 0x10, 0x00, 0x01, 0xCB])  # mov byte cs:[10h], 1; retf
GLOBALS = {"g_mouse_x_mickeys": 2, "g_mouse_y_mickeys": 2, "g_mouse_installed": 1,
           "g_mouse_buttons": 1, "g_bVgaGcReg5ModeA": 1, "g_bVgaGcReg5ModeB": 1,
           "g_wSavedVgaGcReg0AndIdx": 2, "g_wSavedVgaGcReg1And4": 2,
           "g_wSavedVgaGcReg5And8": 2, "g_wSavedVgaScReg2AndIdx": 2, "g_bSavedVgaGcReg3": 1}


class Int33:
    def __init__(self, m: Machine):
        self.calls: list[tuple[int, int, int]] = []
        m.interrupts[0x33] = self.service

    def service(self, m: Machine) -> None:
        ax, cx, dx, es = m.reg("ax"), m.reg("cx"), m.reg("dx"), m.reg("es")
        if ax in (0, 1, 2):
            self.calls.append((ax, 0, 0))
        elif ax == 0x0C:
            self.calls.append((ax, cx, 1 if (es or dx) else 0))
        else:
            self.calls.append((ax, cx, dx))
        if ax == 0:
            m.set_reg("ax", 0xFFFF)  # driver present

    def take(self) -> list[tuple[int, int, int]]:
        calls, self.calls = self.calls, []
        return calls


def native_calls(blob: bytes) -> list[tuple[int, int, int]]:
    return [struct.unpack_from("<3H", blob, i) for i in range(0, len(blob), 6)]


def sync_globals(m: Machine, native) -> None:
    for name in GLOBALS:
        native.poke(name, m.read("_" + name, GLOBALS[name]))


def assert_globals(m: Machine, native, what: str) -> None:
    for name, size in GLOBALS.items():
        assert native.peek(name) == m.read("_" + name, size), f"{name} {what}"


def set_adapter(m: Machine, native, adapter: int) -> None:
    seg, off = m.sym("_g_graphics_context")
    m.set_u8((seg, off + CTX_ADAPTER), adapter)
    ctx = bytearray(native.peek("g_gfxContextBlock"))
    ctx[CTX_ADAPTER] = adapter
    native.poke("g_gfxContextBlock", bytes(ctx))


@pytest.fixture
def mouse(krondor, native):
    int33 = Int33(krondor)
    sync_globals(krondor, native)
    return krondor, int33


@pytest.mark.parametrize("adapter", [8, 5])
def test_install(mouse, native, adapter):
    m, int33 = mouse
    set_adapter(m, native, adapter)
    for attempt in range(2):  # the second install is a no-op
        r = m.call_far("mouse_install").ax
        reply = native.call("mouse.install")
        assert struct.unpack_from("<H", reply)[0] == r, f"result, attempt {attempt}"
        assert native_calls(reply[2:]) == int33.take(), f"driver calls, attempt {attempt}"
        assert_globals(m, native, f"after install attempt {attempt}")


def test_clip_rect(mouse, native):
    m, int33 = mouse
    rng = random.Random(17)
    for _ in range(100):
        args = [rng.randrange(-400, 700) for _ in range(4)]
        m.call_far("mouse_set_cursor_clip_rect", *(a & 0xFFFF for a in args))
        want = native_calls(native.call("mouse.clip", struct.pack("<4h", *args)))
        assert want == int33.take(), args


@pytest.mark.parametrize("installed", [0, 1])
def test_restore_and_queries(mouse, native, installed):
    m, int33 = mouse
    rng = random.Random(18 + installed)
    out_seg, out_off = m.sym("_g_abMouseCbStack")  # spare DGROUP bytes for the out-pointers
    for i in range(60):
        m.set_u8("_g_mouse_installed", installed)
        m.set_u16("_g_mouse_x_mickeys", rng.randrange(0x10000))
        m.set_u16("_g_mouse_y_mickeys", rng.randrange(0x10000))
        m.set_u8("_g_mouse_buttons", rng.randrange(256))
        sync_globals(m, native)

        x0, y0 = rng.randrange(0x10000), rng.randrange(0x10000)
        m.set_u16((out_seg, out_off), x0)
        m.set_u16((out_seg, out_off + 2), y0)
        r = m.call_far("mouse_get_position", out_off, out_off + 2).ax
        got = (r, m.u16((out_seg, out_off)), m.u16((out_seg, out_off + 2)))
        assert struct.unpack("<3H", native.call("mouse.get_position",
                                                struct.pack("<HH", x0, y0))) == got, i

        for button in (0, 1, 2, -1):
            r = m.call_far("mouse_button_pressed", button & 0xFFFF).ax
            want = struct.unpack("<H", native.call("mouse.button", struct.pack("<h", button)))
            assert want[0] == r, (i, button)

        x, y = rng.randrange(-200, 600), rng.randrange(-200, 600)
        r = m.call_far("mouse_set_position", x & 0xFFFF, y & 0xFFFF).ax
        reply = native.call("mouse.set_position", struct.pack("<hh", x, y))
        assert struct.unpack_from("<H", reply)[0] == r
        assert native_calls(reply[2:]) == int33.take()
        assert_globals(m, native, f"after set_position {i}")

    r = m.call_far("mouse_restore_handler").ax
    reply = native.call("mouse.restore")
    assert struct.unpack_from("<H", reply)[0] == r
    assert native_calls(reply[2:]) == int33.take()
    assert_globals(m, native, "after restore")


def test_event_callback(krondor_image, native):
    m = Machine(krondor_image, vga=True)
    vga = PlanarVga(m)
    rng = random.Random(19)
    probe = m.alloc(0x20)
    m.write((probe, 0), PROBE)
    for i in range(40):
        vram = rng.randbytes(4 * 0x10000)
        vga.load_chunky(vram)
        native.poke("g_bakVram", vram)
        seq, gc = bytearray(rng.randbytes(8)), bytearray(rng.randbytes(16))
        gc[5] = rng.choice((0x40, 0x41, 0x00, 0x08))
        seq_index, gc_index = rng.randrange(8), rng.randrange(16)
        vga.seq[:], vga.gc[:] = seq, gc
        vga.seq_index, vga.gc_index = seq_index, gc_index
        native.call("mouse.vga", bytes([seq_index, gc_index]) + bytes(seq) + bytes(gc))
        latch_from = rng.randrange(0x10000)
        vga.latch = [p[latch_from] for p in vga.planes]
        native.call("mouse.latches", struct.pack("<HH", latch_from, 0xFFFF))
        mode_a, mode_b = rng.choice(((0x02, 0x01), (0x40, 0x41)))
        m.set_u8("_g_bVgaGcReg5ModeA", mode_a)
        m.set_u8("_g_bVgaGcReg5ModeB", mode_b)
        handler = rng.random() < 0.8
        m.write("_g_pMouseHandlerCb", struct.pack("<HH", 0, probe) if handler else bytes(4))
        sync_globals(m, native)
        native.call("mouse.set_probe", bytes([handler]))
        m.set_u8((probe, 0x10), 0)

        x, y, b = rng.randrange(0x10000), rng.randrange(0x10000), rng.randrange(0x10000)
        m.call_far("mouse_event_callback", cx=x, dx=y, bx=b)
        calls = struct.unpack("<H", native.call("mouse.callback", struct.pack("<3H", x, y, b)))
        assert calls[0] == m.u8((probe, 0x10)), f"handler calls, case {i}"
        assert_globals(m, native, f"after callback {i}")
        state = native.call("mouse.vga")
        assert state == bytes([vga.seq_index, vga.gc_index]) + bytes(vga.seq) + bytes(vga.gc), \
            f"VGA registers after callback {i}"
        assert native.peek("g_bakVram") == vga.chunky(), f"display memory after callback {i}"
        # The latches must be back: park them somewhere visible on both sides.
        spot = rng.randrange(0x10000)
        for p in range(4):
            vga.planes[p][spot] = vga.latch[p]
        native.call("mouse.latches", struct.pack("<HH", 0xFFFF, spot))
        assert native.peek("g_bakVram") == vga.chunky(), f"latches after callback {i}"
