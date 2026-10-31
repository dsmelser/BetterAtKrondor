"""KEYBOARD.ASM against port/asm/keyboard.c.

Random key sequences go through the original's INT 9 handler (reading port 60h) and
the translation; after every byte the BIOS shift state and keyboard buffer, the key
states, the channel table and the last key event must match, and so must what
kbhit_read takes from the buffer. Ctrl-Esc and Ctrl-Alt-Del, which end the game, are
left out. The Tandy remapping paths run with bBiosCompatible set.
"""

from __future__ import annotations

import random
import struct

import pytest

CTX_TANDY = 0x1C  # GraphicsContext.bBiosCompatible
KEYS = [0x1E, 0x30, 0x20, 0x2C, 0x02, 0x0B, 0x0C, 0x1A, 0x27, 0x29, 0x2B, 0x33, 0x35, 0x39,
        0x0E, 0x0F, 0x1C, 0x2A, 0x36, 0x1D, 0x38, 0x3A, 0x45, 0x46, 0x3B, 0x44, 0x57, 0x58,
        0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x37, 0x54,
        0x55, 0x56, 0x59, 0x5A, 0x60, 0x7F]
GLOBALS = {"g_keyboard_installed": 1, "g_bCapsLockEnabled": 1, "g_wIsrLastKeyEventDead": 2,
           "g_input_mode_lut": 2, "g_key_state": 121, "g_bEnhancedKeyboard": 1}
TABLES = {"g_abAdapterRowStrideShift": 16, "g_bUnused2cfa": 250, "g_key_state": 121,
          "g_ext_scancode_from": 11, "g_ext_scancode_to": 11}


class Keyboard:
    """The original's side: its handler with port 60h, a BIOS data area and DOS vectors."""

    def __init__(self, m, native):
        self.m = m
        self.byte = 0
        m.port_in[0x60] = lambda m, port, size: self.byte
        self.port61 = 0
        m.port_in[0x61] = lambda m, port, size: self.port61
        m.port_out[0x61] = self._out61
        m.port_out[0x20] = lambda m, port, size, value: None
        self.vectors: dict[int, tuple[int, int]] = {}
        m.interrupts[0x21] = self.dos
        m.interrupts[0x15] = self.int15
        m.write((0x40, 0), native.call("kbd.bda"))  # both start from the native BIOS data area

    def _out61(self, m, port, size, value):
        self.port61 = value

    def dos(self, m) -> None:
        ah, al = m.reg("ax") >> 8, m.reg("ax") & 0xFF
        if ah == 0x35:
            seg, off = self.vectors.get(al, (0, 0))
            m.set_reg("es", seg)
            m.set_reg("bx", off)
        elif ah == 0x25:
            self.vectors[al] = (m.reg("ds"), m.reg("dx"))
        else:
            m._fail(f"unmodelled INT 21h AH={ah:02X}h")

    def int15(self, m) -> None:  # the Tandy keyboard probe is unsupported: CF set
        m.set_reg("flags", m.reg("flags") | 1)

    def isr(self, byte: int) -> None:
        self.byte = byte
        self.m.call_interrupt("isr_keyboard")


def sync(m, native, names) -> None:
    for name in names:
        native.poke(name, m.read("_" + name, GLOBALS.get(name) or TABLES[name]))


def assert_same(m, native, what: str) -> None:
    assert native.call("kbd.bda")[:0x40] == m.read((0x40, 0), 0x40), f"BIOS data area {what}"
    for name, size in GLOBALS.items():
        assert native.peek(name) == m.read("_" + name, size), f"{name} {what}"


def set_tandy(m, native, tandy: int, enhanced: int) -> None:
    seg, off = m.sym("_g_graphics_context")
    m.set_u8((seg, off + CTX_TANDY), tandy)
    ctx = bytearray(native.peek("g_gfxContextBlock"))
    ctx[CTX_TANDY] = tandy
    native.poke("g_gfxContextBlock", bytes(ctx))
    m.set_u8("_g_bEnhancedKeyboard", enhanced)


def test_initial_tables(krondor, fresh_native):
    for name, size in TABLES.items():
        assert fresh_native.peek(name) == krondor.read("_" + name, size), name


@pytest.mark.parametrize("tandy,enhanced,caps", [(0, 0, 0), (0, 0, 1), (1, 0, 0), (1, 1, 0),
                                                 (1, 1, 1)])
def test_key_sequences(krondor, native, tandy, enhanced, caps):
    m = krondor
    kb = Keyboard(m, native)
    set_tandy(m, native, tandy, enhanced)
    m.set_u8("_g_bCapsLockEnabled", caps)
    sync(m, native, list(GLOBALS) + list(TABLES))
    rng = random.Random(f"{tandy}{enhanced}{caps}")
    keys = [k for k in KEYS if not (tandy and k == 0x56)]  # the Tandy remap makes 56h Del
    down = set()
    for i in range(1500):
        if rng.random() < 0.05:
            byte = rng.choice((0xE0, 0xE1, 0xFA, 0xFF, 0x00))
        else:
            key = rng.choice(keys)
            release = key in down and rng.random() < 0.6 or rng.random() < 0.1
            byte = key | (0x80 if release else 0)
            (down.discard if release else down.add)(key)
        kb.isr(byte)
        native.call("kbd.isr", bytes([byte]))
        assert_same(m, native, f"after byte {i} ({byte:#04x})")
        if rng.random() < 0.15:
            for _ in range(rng.randrange(1, 4)):
                got = m.call_far("kbhit_read").ax
                assert struct.unpack("<H", native.call("kbd.kbhit"))[0] == got, f"kbhit {i}"
            assert_same(m, native, f"after reading the buffer at {i}")


def test_queries(krondor, native):
    m, rng = krondor, random.Random(24)
    for i in range(50):
        state = rng.randbytes(121)
        lut = bytes([rng.randrange(10), rng.randrange(10)])
        m.write("_g_key_state", state)
        m.write("_g_input_mode_lut", lut)
        sync(m, native, ["g_key_state", "g_input_mode_lut"])
        for code in range(0x59):
            got = m.call_far("key_is_down", code).ax
            assert struct.unpack("<H", native.call("kbd.is_down", struct.pack("<H", code)))[0] == got
        for mode in (0, 1, 2, -1):
            got = m.call_far("key_state_get_via_2cf8_table", mode & 0xFFFF).ax
            want = native.call("kbd.state_get", struct.pack("<h", mode))
            assert struct.unpack("<H", want)[0] == got, (i, mode)


def test_axis_tick(krondor, native):
    m, rng = krondor, random.Random(25)
    for i in range(400):
        state = bytearray(m.read("_g_key_state", 121))
        state[0x59:0x79] = rng.randbytes(0x20)
        for at in (0x59, 0x5B, 0x5D, 0x5F):  # axes: small values both signs, sometimes big
            v = rng.choice((rng.randrange(-0x90, 0x90), rng.randrange(-0x8000, 0x8000)))
            state[at:at + 2] = struct.pack("<h", v)
        state[0x65:0x67] = struct.pack("<H", rng.choice((1, 3, 0x20, 0x90, rng.randrange(0x10000))))
        lut = bytes(rng.choice((0, 1, 2, 5, 8, 9, 0x81, 0xFF)) for _ in range(2))
        m.write("_g_key_state", bytes(state))
        m.write("_g_input_mode_lut", lut)
        sync(m, native, ["g_key_state", "g_input_mode_lut"])
        m.call_interrupt("input_axis_accumulate_isr")
        native.call("kbd.axis")
        assert native.peek("g_key_state") == m.read("_g_key_state", 121), f"tick {i}, lut {lut.hex()}"


@pytest.mark.parametrize("tandy,bda96", [(0, 0x10), (1, 0x10), (1, 0x00)])
def test_install_restore(krondor, native, tandy, bda96):
    m = krondor
    Keyboard(m, native)
    set_tandy(m, native, tandy, 0)
    bda = bytearray(native.call("kbd.bda"))
    bda[0x96] = bda96
    bda[0x17] = 0x7F
    native.call("kbd.bda", bytes(bda))
    m.write((0x40, 0), bytes(bda))
    m.set_u8("_g_keyboard_installed", 0)
    m.set_u8("_g_bCapsLockEnabled", 1)
    sync(m, native, list(GLOBALS) + list(TABLES))
    for arg in (0, 1):  # the second call finds the keyboard installed
        got = m.call_far("keyboard_install", arg).ax
        assert struct.unpack("<H", native.call("kbd.install", struct.pack("<H", arg)))[0] == got & 0xFF
        assert_same(m, native, f"after keyboard_install({arg})")
        for name in TABLES:
            assert native.peek(name) == m.read("_" + name, TABLES[name]), name
    for _ in range(2):
        got = m.call_far("keyboard_restore_handler").ax
        assert struct.unpack("<H", native.call("kbd.restore"))[0] == got & 0xFF
        assert_same(m, native, "after keyboard_restore_handler")
