"""VIDDET.ASM (adapter detection, video shutdown) against port/asm/viddet.c.

The original runs against a modelled VGA machine with a colour monitor: the BIOS answers
INT 10h as a VGA BIOS does, the colour CRTC's registers read back, and there is no
monochrome CRTC. The translation has those answers built in.
"""

from __future__ import annotations

import struct

import pytest

CTX_RENDER_AUTODETECT = 0x232  # bVideoAutoDetectEnabled within GraphicsContextRender


@pytest.fixture
def vga_machine(krondor):
    m = krondor
    crtc = bytearray(32)
    index = [0]

    def int10(m):
        ax = m.reg("ax")
        if ax == 0x1A00:  # display combination: active colour VGA
            m.set_reg("ax", 0x001A)
            m.set_reg("bx", 0x0008)
        elif ax >> 8 == 0x12:  # EGA information: colour, 256 KB
            m.set_reg("bx", 0x0003)
            m.set_reg("cx", 0x0009)
        elif ax != 0x0003:  # mode 3 (text) is accepted and ignored
            m._fail(f"unmodelled INT 10h AX={ax:04X}h")

    def crtc_out(m, port, size, value):
        if port == 0x3D4:
            index[0] = value & 0x1F
        elif port == 0x3D5:
            crtc[index[0]] = value & 0xFF

    m.interrupts[0x10] = int10
    m.port_out[0x3D4] = m.port_out[0x3D5] = crtc_out
    m.port_in[0x3D5] = lambda m, port, size: crtc[index[0]]
    m.port_out[0x3B4] = m.port_out[0x3B5] = lambda m, port, size, value: None
    m.port_in[0x3B5] = m.port_in[0x3BA] = lambda m, port, size: 0xFF  # no MDA
    m.write((0x40, 0x87), b"\x60")  # EGA/VGA active, not monochrome
    return m


@pytest.mark.parametrize("autodetect", [0x0000, 0x0001, 0x0100, 0x0101])
def test_detect_adapter(vga_machine, native, autodetect):
    m = vga_machine
    seg, off = m.sym("_g_graphics_context_render")
    for requested in range(256):
        m.set_u8("_g_bRequestedVideoMode", requested)
        m.set_u16((seg, off + CTX_RENDER_AUTODETECT), autodetect)
        got = m.call_near("video_detect_adapter").ax
        want = struct.unpack("<H", native.call("viddet.detect",
                                               struct.pack("<BH", requested, autodetect)))[0]
        assert want == got, f"requested {requested:#x}, auto-detect word {autodetect:#x}"


def test_shutdown(vga_machine, native):
    m = vga_machine
    for saved in (0x00, 0x01, 0x02, 0x03, 0xFF):
        m.set_u8("_g_bSavedBiosVideoMode", saved)
        ax = m.call_far("video_shutdown").ax
        reply = native.call("viddet.shutdown", bytes([saved]))
        assert struct.unpack("<HB", reply) == (ax, m.u8("_g_bSavedBiosVideoMode")), hex(saved)
