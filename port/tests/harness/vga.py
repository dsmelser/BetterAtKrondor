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

from unicorn import UC_HOOK_MEM_READ

from .machine import LOAD_SEG, VGA_LINEAR, Machine
from .mzexe import MzImage

PLANE = 0x10000
CTX_SIZE = 0xAB6  # GfxContext (gfxctx.inc)
TEMPLATE_OFF = 0x00F8  # g_apfnVgaVtable in the chunk

# The registers INT 10h AH=00h loads (IBM VGA BIOS), as port/platform/vgaregs.c has them.
MODE_REGS = {
    0x13: {
        "seq": [0x03, 0x01, 0x0F, 0x00, 0x0E],
        "crtc": [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF],
        "gc": [0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF],
        "attr": [*range(16), 0x41, 0x00, 0x0F, 0x00, 0x00],
        "misc": 0x63,
    },
    0x10: {
        "seq": [0x03, 0x01, 0x0F, 0x00, 0x06],
        "crtc": [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x40, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x83, 0x85, 0x5D, 0x28, 0x0F, 0x63, 0xBA, 0xE3, 0xFF],
        "gc": [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF],
        "attr": [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
                 0x3D, 0x3E, 0x3F, 0x01, 0x00, 0x0F, 0x00, 0x00],
        "misc": 0xA3,
    },
}


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
        self.set_mode(0x13)
        machine.uc.mmio_map(VGA_LINEAR, PLANE, self._read, None, self._write, None)
        machine.uc.hook_add(UC_HOOK_MEM_READ, self._on_cpu_read, begin=VGA_LINEAR,
                            end=VGA_LINEAR + PLANE - 1)
        for port in (0x3C0, 0x3C2, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9, 0x3CE, 0x3CF,
                     0x3D4, 0x3D5):
            machine.port_out[port] = self._out
        for port in (0x3C0, 0x3C1, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9, 0x3CC, 0x3CE, 0x3CF,
                     0x3D4, 0x3D5, 0x3DA):
            machine.port_in[port] = self._in
        machine.interrupts[0x10] = self._int10

    def set_mode(self, mode: int) -> None:
        """INT 10h AH=00h as the port has it: the mode's registers, the index registers at 0
        but the attribute index at 20h (display on), the flip-flop reset, display memory
        cleared, the DAC untouched."""
        regs = MODE_REGS[mode]
        for name in ("seq", "crtc", "gc", "attr"):
            table = getattr(self, name)
            table[:] = bytes(len(table))
            table[:len(regs[name])] = bytes(regs[name])
        self.misc = regs["misc"]
        self.pel_mask = 0xFF
        self.seq_index = self.gc_index = self.crtc_index = 0
        self.attr_index = 0x20  # the BIOS ends by turning the display on
        self.attr_flipflop = 0
        for p in self.planes:
            p[:] = bytes(PLANE)

    def _int10(self, m: Machine) -> None:
        ax = m.reg("ax")
        if ax in (0x0013, 0x0010):
            self.set_mode(ax)
            return
        # The attribute-controller calls go through the ports as the BIOS's do, ending with
        # index 20h (display on), which leaves the flip-flop expecting data.
        if ax == 0x1013:  # colour paging: BL = 0 sets the mode BH, BL = 1 selects page BH
            bl, bh = m.reg("bx") & 0xFF, m.reg("bx") >> 8
            self._in(m, 0x3DA, 1)
            self._out8(0x3C0, 0x10)
            mode = self._in(m, 0x3C1, 1)
            if bl == 0:
                self._out8(0x3C0, mode | 0x80 if bh else mode & 0x7F)
            else:
                self._out8(0x3C0, mode)
                self._out8(0x3C0, 0x14)
                self._out8(0x3C0, (bh if mode & 0x80 else bh << 2) & 0x0F)
            self._out8(0x3C0, 0x20)
            return
        if ax == 0x1001:  # the overscan colour, BH
            self._in(m, 0x3DA, 1)
            self._out8(0x3C0, 0x11)
            self._out8(0x3C0, m.reg("bx") >> 8)
            self._out8(0x3C0, 0x20)
            return
        m._fail(f"unmodelled INT 10h AX={ax:04X}h")

    def regs(self) -> bytes:
        """The register file in vga_regs_snapshot's layout (port/platform/vgaregs.h)."""
        return (bytes(self.seq) + bytes(self.crtc) + bytes(self.gc) + bytes(self.attr)
                + bytes((self.misc, self.pel_mask, self.seq_index, self.crtc_index,
                         self.gc_index, self.attr_index, self.attr_flipflop))
                + bytes(self.latch))

    def load_regs(self, data: bytes) -> None:
        self.seq[:] = data[0:8]
        self.crtc[:] = data[8:40]
        self.gc[:] = data[40:56]
        self.attr[:] = data[56:88]
        (self.misc, self.pel_mask, self.seq_index, self.crtc_index, self.gc_index,
         self.attr_index, self.attr_flipflop) = data[88:95]
        self.latch = list(data[95:99])

    # --- display memory --------------------------------------------------------------

    # A read loads the latches from the last byte the CPU reads (a word or dword is read a
    # byte at a time, upwards). Unicorn hands an unaligned word read to the MMIO callback as
    # two aligned words, so the latches come from the access the memory hook sees, and the
    # MMIO callback only computes the bytes read.

    def _on_cpu_read(self, uc, access, address, size, value, _user):
        last = (address - VGA_LINEAR + size - 1) & 0xFFFF
        self.latch = [p[last] for p in self.planes]

    def _read(self, uc, offset, size, _user):
        value = 0
        for k in range(size):
            value |= self._read_byte((offset + k) & 0xFFFF) << (8 * k)
        return value

    def _read_byte(self, o: int) -> int:
        if self.gc[5] & 0x08:  # read mode 1: colour compare
            result = 0xFF
            for p in range(4):
                if self.gc[7] & (1 << p):
                    want = 0xFF if self.gc[2] & (1 << p) else 0
                    result &= ~(self.planes[p][o] ^ want) & 0xFF
            return result
        return self.planes[self.gc[4] & 3][o]

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
        if port == 0x3C0:
            return self.attr_index
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
    Other VMCODE.OVL chunks load the same way; their slot template sits elsewhere.
    """

    template_off = TEMPLATE_OFF

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
        assert (r.dx, r.si) == (self.seg, self.template_off)
        return r.ax & 0xFF

    def slot(self, index: int) -> tuple[int, int]:
        (off,) = struct.unpack("<H", self.m.read((self.seg, self.template_off + 4 * index), 2))
        return self.seg, off

    def call(self, index: int, *args: int, **regs: int):
        regs.setdefault("ds", self.ctx_seg)
        return self.m.call_far(self.slot(index), *args, **regs)


class HegChunk(VgaChunk):
    """The "HEG:" chunk (HEG.EXE of the reference build), the books' 640x350 driver."""

    template_off = 0x013E  # g_apfnHegVtable
