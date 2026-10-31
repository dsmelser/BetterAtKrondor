"""PIXEL.ASM (clipped getpixel/putpixel) against port/asm/pixel.c.

The renderer's pixel slots are replaced by probes on both sides: getpixel's probe
returns 1234h, putpixel's records that it was reached. What is compared is the clipping
decision made before the hand-over.
"""

from __future__ import annotations

import random
import struct

GET_PROBE = bytes([0xB8, 0x34, 0x12, 0xCB])  # mov ax, 1234h; retf
PUT_PROBE = bytes([0x2E, 0xC6, 0x06, 0x10, 0x00, 0x01, 0xCB])  # mov byte cs:[10h], 1; retf
SLOT_GET, SLOT_PUT = 21, 22
CLIP_AT = 0x03  # bClip_enabled, then clip.xmin/xmax/ymin/ymax


def test_clipping(krondor, native):
    m = krondor
    get_probe = m.alloc(0x20)
    m.write((get_probe, 0), GET_PROBE)
    put_probe = m.alloc(0x20)
    m.write((put_probe, 0), PUT_PROBE)
    vt_seg, vt_off = m.sym("_g_renderer_vtable")
    m.write((vt_seg, vt_off + 4 * SLOT_GET), struct.pack("<HH", 0, get_probe))
    m.write((vt_seg, vt_off + 4 * SLOT_PUT), struct.pack("<HH", 0, put_probe))
    ctx = bytearray(native.peek("g_gfxContextBlock"))
    rng = random.Random(12)
    for _ in range(400):
        enabled = rng.choice((0, 1, 1, 0x80))
        xmin, ymin = rng.randrange(-50, 300), rng.randrange(-50, 190)
        xmax, ymax = xmin + rng.randrange(-5, 100), ymin + rng.randrange(-5, 100)
        clip = bytes([enabled]) + struct.pack("<4h", xmin, xmax, ymin, ymax)
        ctx_seg, ctx_off = m.sym("_g_graphics_context")
        m.write((ctx_seg, ctx_off + CLIP_AT), clip)
        ctx[CLIP_AT:CLIP_AT + 9] = clip
        native.poke("g_gfxContextBlock", bytes(ctx))
        x, y = rng.randrange(-80, 400), rng.randrange(-80, 280)
        m.write((put_probe, 0x10), b"\x00")
        got = m.call_far("getpixel", x & 0xFFFF, y & 0xFFFF).ax
        m.call_far("putpixel", x & 0xFFFF, y & 0xFFFF, 7)
        reached = m.u8((put_probe, 0x10))
        want_get, want_put = struct.unpack("<HB", native.call("pixel.run",
                                                             struct.pack("<hh", x, y)))
        assert (want_get, want_put) == (got, reached), f"({x}, {y}) clip {clip.hex()}"
