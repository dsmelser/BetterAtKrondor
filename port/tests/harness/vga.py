"""A VGA for original routines that program the hardware directly.

PlanarVga models what the game's video code touches: four 64 KB planes behind the
A000 window with the sequencer map mask, the graphics controller's write modes 0-3,
read modes, read map select, logical operations, bit mask and latches; the CRTC,
attribute and miscellaneous registers as storage; the DAC with its index registers
and triple latching; and an Input Status 1 that alternates retrace and display so
polling loops end.

VgaChunk loads the VMCODE.OVL "VGA:" driver chunk (VGA.EXE of the reference build)
next to a host graphics-context block, ready to call vga_init and the slots.
"""

from __future__ import annotations

import struct

from .machine import LOAD_SEG, VGA_LINEAR, Machine
from .mzexe import MzImage

PLANE = 0x10000
CTX_SIZE = 0xAB6  # GfxContext (gfxctx.inc)
TEMPLATE_OFF = 0x00F8  # g_apfnVgaVtable in the chunk


class PlanarVga:
    def __init__(self, machine: Machine):
        self.machine = machine
        self.planes = [bytearray(PLANE) for _ in range(4)]
        self.latch = [0, 0, 0, 0]
        self.seq = bytearray(8)
        self.gc = bytearray(16)
        self.crtc = bytearray(32)
        self.attr = bytearray(32)
        self.seq_index = self.gc_index = self.crtc_index = self.attr_index = 0
        self.attr_flipflop = 0
        self.misc = 0
        self.pel_mask = 0xFF
        self.dac = [bytearray(3) for _ in range(256)]
        self.dac_write_index = self.dac_read_index = 0
        self.dac_component = 0
        self.dac_triple = bytearray(3)
        self.dac_state = 0
        self.status_reads = 0
        self.mode_13h()
        machine.uc.mmio_map(VGA_LINEAR, PLANE, self._read, None, self._write, None)
        for port in (0x3C0, 0x3C2, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9, 0x3CE, 0x3CF,
                     0x3D4, 0x3D5):
            machine.port_out[port] = self._out
        for port in (0x3C1, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9, 0x3CC, 0x3CE, 0x3CF, 0x3D4,
                     0x3D5, 0x3DA):
            machine.port_in[port] = self._in
        machine.interrupts[0x10] = self._int10

    def mode_13h(self) -> None:
        """Register state after INT 10h AX=0013h (the values the Mode-Y setup edits)."""
        self.seq[2] = 0x0F
        self.seq[4] = 0x0E
        self.gc[5] = 0x40
        self.gc[6] = 0x05
        self.gc[8] = 0xFF
        self.crtc[0x14] = 0x40
        self.crtc[0x17] = 0xA3
        self.crtc[0x0C] = self.crtc[0x0D] = 0

    def _int10(self, m: Machine) -> None:
        if m.reg("ax") == 0x0013:
            self.mode_13h()
            return
        m._fail(f"unmodelled INT 10h AX={m.reg('ax'):04X}h")

    # --- display memory --------------------------------------------------------------

    def _read(self, uc, offset, size, _user):
        value = 0
        for k in range(size):
            value |= self._read_byte((offset + k) & 0xFFFF) << (8 * k)
        return value

    def _read_byte(self, o: int) -> int:
        self.latch = [p[o] for p in self.planes]
        if self.gc[5] & 0x08:  # read mode 1: colour compare
            result = 0xFF
            for p in range(4):
                if self.gc[7] & (1 << p):
                    want = 0xFF if self.gc[2] & (1 << p) else 0
                    result &= ~(self.latch[p] ^ want) & 0xFF
            return result
        return self.latch[self.gc[4] & 3]

    def _write(self, uc, offset, size, value, _user):
        for k in range(size):
            self._write_byte((offset + k) & 0xFFFF, (value >> (8 * k)) & 0xFF)

    def _write_byte(self, o: int, b: int) -> None:
        mode = self.gc[5] & 3
        mask = self.seq[2] & 0x0F
        if mode == 1:
            for p in range(4):
                if mask & (1 << p):
                    self.planes[p][o] = self.latch[p]
            return
        rotate = self.gc[3] & 7
        op = (self.gc[3] >> 3) & 3
        bitmask = self.gc[8]
        if mode in (0, 3):
            b = ((b >> rotate) | (b << (8 - rotate))) & 0xFF
        if mode == 3:
            bitmask &= b
        for p in range(4):
            if not mask & (1 << p):
                continue
            if mode == 0:
                v = (0xFF if self.gc[0] & (1 << p) else 0) if self.gc[1] & (1 << p) else b
            elif mode == 2:
                v = 0xFF if b & (1 << p) else 0
            else:
                v = 0xFF if self.gc[0] & (1 << p) else 0
            if op == 1:
                v &= self.latch[p]
            elif op == 2:
                v |= self.latch[p]
            elif op == 3:
                v ^= self.latch[p]
            self.planes[p][o] = (v & bitmask) | (self.latch[p] & ~bitmask & 0xFF)

    def chunky(self) -> bytes:
        """Display memory in the port's chunky layout: planar (offset o, plane p) at o*4+p."""
        out = bytearray(4 * PLANE)
        for p in range(4):
            out[p::4] = self.planes[p]
        return bytes(out)

    def load_chunky(self, data: bytes) -> None:
        for p in range(4):
            self.planes[p][:] = data[p::4]

    def dac_bytes(self) -> bytes:
        return b"".join(bytes(e) for e in self.dac)

    def crtc_start(self) -> int:
        return self.crtc[0x0C] << 8 | self.crtc[0x0D]

    # --- ports -----------------------------------------------------------------------

    def _out(self, m: Machine, port: int, size: int, value: int) -> None:
        self._out8(port, value & 0xFF)
        if size == 2:
            self._out8(port + 1, (value >> 8) & 0xFF)

    def _out8(self, port: int, v: int) -> None:
        if port == 0x3C4:
            self.seq_index = v
        elif port == 0x3C5:
            self.seq[self.seq_index & 7] = v
        elif port == 0x3CE:
            self.gc_index = v
        elif port == 0x3CF:
            self.gc[self.gc_index & 15] = v
        elif port == 0x3D4:
            self.crtc_index = v
        elif port == 0x3D5:
            self.crtc[self.crtc_index & 0x1F] = v
        elif port == 0x3C0:
            if self.attr_flipflop == 0:
                self.attr_index = v
            else:
                self.attr[self.attr_index & 0x1F] = v
            self.attr_flipflop ^= 1
        elif port == 0x3C2:
            self.misc = v
        elif port == 0x3C6:
            self.pel_mask = v
        elif port == 0x3C7:
            self.dac_read_index, self.dac_component, self.dac_state = v, 0, 3
        elif port == 0x3C8:
            self.dac_write_index, self.dac_component, self.dac_state = v, 0, 0
        elif port == 0x3C9:
            # The DAC commits an entry once all three components have arrived.
            self.dac_triple[self.dac_component] = v & 0x3F
            self.dac_component += 1
            if self.dac_component == 3:
                self.dac[self.dac_write_index][:] = self.dac_triple
                self.dac_component = 0
                self.dac_write_index = (self.dac_write_index + 1) & 0xFF
        else:
            self.machine._fail(f"unmodelled VGA port {port:03X}h")

    def _in(self, m: Machine, port: int, size: int) -> int:
        if port == 0x3DA:
            # Alternate display and retrace so "wait for (not) retrace" loops end.
            self.status_reads += 1
            self.attr_flipflop = 0
            return 0x09 if self.status_reads & 1 else 0x00
        if port == 0x3C4:
            return self.seq_index
        if port == 0x3CE:
            return self.gc_index
        if port == 0x3D4:
            return self.crtc_index
        if port == 0x3C5:
            return self.seq[self.seq_index & 7]
        if port == 0x3CF:
            return self.gc[self.gc_index & 15]
        if port == 0x3D5:
            return self.crtc[self.crtc_index & 0x1F]
        if port == 0x3C1:
            return self.attr[self.attr_index & 0x1F]
        if port == 0x3C6:
            return self.pel_mask
        if port == 0x3C7:
            return self.dac_state
        if port == 0x3C8:
            return self.dac_write_index
        if port == 0x3C9:
            v = self.dac[self.dac_read_index][self.dac_component]
            self.dac_component += 1
            if self.dac_component == 3:
                self.dac_component = 0
                self.dac_read_index = (self.dac_read_index + 1) & 0xFF
            return v
        if port == 0x3CC:
            return self.misc
        m._fail(f"unmodelled IN from VGA port {port:03X}h")
        return 0xFF


class VgaChunk:
    """The VGA driver chunk with a planar VGA and a host graphics-context block.

    The chunk is loaded at LOAD_SEG (ORG 0). vga_init gets the context block's segment as
    the caller's DS and 0 as the context offset, so the driver's scratch DS is that block.
    """

    def __init__(self, image: MzImage):
        self.m = Machine(image, vga=True)
        self.vga = PlanarVga(self.m)
        # Past the 64 KB window nothing answers in a VGA-only PC: reads float to FFh.
        self.m.uc.mem_unmap(VGA_LINEAR + PLANE, PLANE)
        self.m.uc.mmio_map(VGA_LINEAR + PLANE, PLANE, lambda uc, off, size, ud: (1 << 8 * size) - 1,
                           None, lambda uc, off, size, value, ud: None, None)
        # BIOS data area: the CRTC base port a colour adapter's BIOS records at 0040:0063.
        self.m.write((0x40, 0x63), struct.pack("<H", 0x3D4))
        self.seg = LOAD_SEG
        self.ctx_seg = self.m.alloc(CTX_SIZE)
        self.callbacks_seg = self.m.alloc(76)

    def write_ctx(self, data: bytes) -> None:
        assert len(data) == CTX_SIZE
        self.m.write((self.ctx_seg, 0), data)

    def ctx(self) -> bytes:
        return self.m.read((self.ctx_seg, 0), CTX_SIZE)

    def init(self) -> int:
        r = self.m.call_far((self.seg, 0), 0, 0, self.callbacks_seg, ds=self.ctx_seg)
        assert (r.dx, r.si) == (self.seg, TEMPLATE_OFF)
        return r.ax & 0xFF

    def slot(self, index: int) -> tuple[int, int]:
        (off,) = struct.unpack("<H", self.m.read((self.seg, TEMPLATE_OFF + 4 * index), 2))
        return self.seg, off

    def call(self, index: int, *args: int, **regs: int):
        regs.setdefault("ds", self.ctx_seg)
        return self.m.call_far(self.slot(index), *args, **regs)
