"""A real-mode PC under Unicorn that runs routines of the original executables.

Memory map (1 MB, real mode):

    0000:0000  zeroed low memory (interrupt table, BIOS data area: unused)
    1000:0000  the executable's load module, relocated (LOAD_SEG)
    5000-9FFF  test heap: buffers the tests allocate (alloc)
    A000-AFFF  VGA window: RAM, or the planar VGA model when a test attaches one
    F000:0000  far-return sentinel: a call ends when execution reaches it

Calls push their arguments the way Borland's medium-model C does (right to left, word
by word, caller cleans up) and stop when the routine returns to the sentinel. Port I/O
and software interrupts go to handlers the test installs; anything unhandled fails the
call, so an original routine never touches hardware the test did not model.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Callable

from unicorn import (UC_ARCH_X86, UC_HOOK_INSN, UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_MODE_16,
                     Uc, UcError)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BP,
                               UC_X86_REG_BX, UC_X86_REG_CS, UC_X86_REG_CX, UC_X86_REG_DI,
                               UC_X86_REG_DS, UC_X86_REG_DX, UC_X86_REG_ES, UC_X86_REG_FLAGS,
                               UC_X86_REG_IP, UC_X86_REG_SI, UC_X86_REG_SP, UC_X86_REG_SS)

from .mzexe import MzImage

LOAD_SEG = 0x1000
HEAP_FIRST_SEG = 0x5000
HEAP_END_SEG = 0xA000
VGA_LINEAR = 0xA0000
RETURN_SEG = 0xF000
RETURN_OFF = 0x0000
NEAR_RETURN_OFF = 0xFFFF  # a near call returns here, inside the routine's own segment
MAX_INSTRUCTIONS = 200_000_000

_REGS = {
    "ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
    "si": UC_X86_REG_SI, "di": UC_X86_REG_DI, "bp": UC_X86_REG_BP, "sp": UC_X86_REG_SP,
    "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS, "cs": UC_X86_REG_CS,
    "ip": UC_X86_REG_IP, "flags": UC_X86_REG_FLAGS,
}

CARRY = 0x0001
ZERO = 0x0040
SIGN = 0x0080


class MachineError(Exception):
    pass


@dataclass
class Regs:
    ax: int
    bx: int
    cx: int
    dx: int
    si: int
    di: int
    bp: int
    sp: int
    ds: int
    es: int
    ss: int
    flags: int

    @property
    def dx_ax(self) -> int:
        """A 32-bit result returned in DX:AX."""
        return self.dx << 16 | self.ax

    @property
    def carry(self) -> bool:
        return bool(self.flags & CARRY)


PortIn = Callable[["Machine", int, int], int]  # (machine, port, size) -> value
PortOut = Callable[["Machine", int, int, int], None]  # (machine, port, size, value)
Interrupt = Callable[["Machine"], None]


class Machine:
    def __init__(self, image: MzImage | None, *, vga: bool = False):
        self.image = image
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, VGA_LINEAR)
        if not vga:
            self.uc.mem_map(VGA_LINEAR, 0x10000)
        self.uc.mem_map(VGA_LINEAR + 0x10000, 0x100000 - VGA_LINEAR - 0x10000)
        self.port_in: dict[int, PortIn] = {}
        self.port_out: dict[int, PortOut] = {}
        self.interrupts: dict[int, Interrupt] = {}
        self._heap_seg = HEAP_FIRST_SEG
        self.data_seg = 0
        if image is not None:
            self._load(image)
        self.uc.hook_add(UC_HOOK_INSN, self._on_in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._on_out, None, 1, 0, UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_INTR, self._on_interrupt)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_invalid)

    # --- loading and symbols -------------------------------------------------------

    def _load(self, image: MzImage) -> None:
        base = LOAD_SEG << 4
        self.uc.mem_write(base, image.image)
        for seg, off in image.relocations:
            addr = base + (seg << 4) + off
            (word,) = struct.unpack("<H", self.uc.mem_read(addr, 2))
            self.uc.mem_write(addr, struct.pack("<H", (word + LOAD_SEG) & 0xFFFF))
        if "DATASEG@" in image.symbols:
            self.data_seg = LOAD_SEG + image.symbols["DATASEG@"][0]

    def sym(self, name: str) -> tuple[int, int]:
        """Absolute (segment, offset) of a public of the loaded executable."""
        if self.image is None:
            raise MachineError("no executable loaded")
        seg, off = self.image.symbol(name)
        return LOAD_SEG + seg, off

    def resolve(self, target: str | tuple[int, int]) -> tuple[int, int]:
        return self.sym(target) if isinstance(target, str) else target

    # --- memory --------------------------------------------------------------------

    @staticmethod
    def linear(seg: int, off: int) -> int:
        return (seg << 4) + off

    def read(self, target: str | tuple[int, int], size: int) -> bytes:
        seg, off = self.resolve(target)
        return bytes(self.uc.mem_read(self.linear(seg, off), size))

    def write(self, target: str | tuple[int, int], data: bytes) -> None:
        seg, off = self.resolve(target)
        self.uc.mem_write(self.linear(seg, off), bytes(data))

    def u8(self, target: str | tuple[int, int]) -> int:
        return self.read(target, 1)[0]

    def u16(self, target: str | tuple[int, int]) -> int:
        return struct.unpack("<H", self.read(target, 2))[0]

    def u32(self, target: str | tuple[int, int]) -> int:
        return struct.unpack("<I", self.read(target, 4))[0]

    def set_u8(self, target: str | tuple[int, int], v: int) -> None:
        self.write(target, bytes([v & 0xFF]))

    def set_u16(self, target: str | tuple[int, int], v: int) -> None:
        self.write(target, struct.pack("<H", v & 0xFFFF))

    def set_u32(self, target: str | tuple[int, int], v: int) -> None:
        self.write(target, struct.pack("<I", v & 0xFFFFFFFF))

    def free_all(self) -> None:
        """Release every test buffer: the heap starts over."""
        self._heap_seg = HEAP_FIRST_SEG

    def alloc(self, size: int, fill: int = 0) -> int:
        """A paragraph-aligned test buffer; returns its segment (the buffer is seg:0000)."""
        paragraphs = (size + 15) // 16 or 1
        seg = self._heap_seg
        if seg + paragraphs > HEAP_END_SEG:
            raise MachineError(f"test heap exhausted ({size} bytes requested)")
        self._heap_seg += paragraphs
        self.uc.mem_write(self.linear(seg, 0), bytes([fill]) * size)
        return seg

    # --- calls ---------------------------------------------------------------------

    def _push(self, ss: int, sp: int, word: int) -> int:
        sp = (sp - 2) & 0xFFFF
        self.uc.mem_write(self.linear(ss, sp), struct.pack("<H", word & 0xFFFF))
        return sp

    def _run(self, seg: int, off: int, until: int, sp_after: int, ss: int, regs: dict) -> Regs:
        self._error: Exception | None = None
        for name in ("ax", "bx", "cx", "dx", "si", "di", "bp"):
            self.uc.reg_write(_REGS[name], regs.pop(name, 0) & 0xFFFF)
        self.uc.reg_write(UC_X86_REG_DS, regs.pop("ds", self.data_seg))
        self.uc.reg_write(UC_X86_REG_ES, regs.pop("es", self.data_seg))
        self.uc.reg_write(UC_X86_REG_FLAGS, regs.pop("flags", 0x0002))
        if regs:
            raise MachineError(f"unknown register arguments: {sorted(regs)}")
        self.uc.reg_write(UC_X86_REG_SS, ss)
        self.uc.reg_write(UC_X86_REG_SP, sp_after)
        self.uc.reg_write(UC_X86_REG_CS, seg)
        try:
            self.uc.emu_start(self.linear(seg, off), until, count=MAX_INSTRUCTIONS)
        except UcError as e:
            raise MachineError(f"emulation failed at {self._where()}: {e}") from e
        if self._error is not None:
            raise self._error
        cs = self.uc.reg_read(UC_X86_REG_CS)
        ip = self.uc.reg_read(UC_X86_REG_IP)
        if self.linear(cs, ip) != until:
            raise MachineError(f"routine did not return (stopped at {cs:04X}:{ip:04X})")
        return self.regs()

    def call_far(self, target: str | tuple[int, int], *args: int, ss: int | None = None,
                 sp: int = 0xFFF0, **regs: int) -> Regs:
        """Far-call a routine: stack arguments as words (push a 32-bit value as its low
        word then its high word), register arguments by name (ax=..., es=...)."""
        seg, off = self.resolve(target)
        ss = self.data_seg if ss is None else ss
        for word in reversed(args):
            sp = self._push(ss, sp, word)
        args_sp = sp
        sp = self._push(ss, sp, RETURN_SEG)
        sp = self._push(ss, sp, RETURN_OFF)
        out = self._run(seg, off, self.linear(RETURN_SEG, RETURN_OFF), sp, ss, dict(regs))
        if out.sp != args_sp:
            raise MachineError(f"unbalanced stack after the call (SP {out.sp:04X}, "
                               f"expected {args_sp:04X})")
        return out

    def call_near(self, target: str | tuple[int, int], *args: int, ss: int | None = None,
                  sp: int = 0xFFF0, **regs: int) -> Regs:
        """Near-call a routine in its own code segment (it returns to NEAR_RETURN_OFF)."""
        seg, off = self.resolve(target)
        ss = self.data_seg if ss is None else ss
        for word in reversed(args):
            sp = self._push(ss, sp, word)
        args_sp = sp
        sp = self._push(ss, sp, NEAR_RETURN_OFF)
        out = self._run(seg, off, self.linear(seg, NEAR_RETURN_OFF), sp, ss, dict(regs))
        if out.sp != args_sp:
            raise MachineError(f"unbalanced stack after the call (SP {out.sp:04X}, "
                               f"expected {args_sp:04X})")
        return out

    def call_interrupt(self, target: str | tuple[int, int], ss: int | None = None,
                       sp: int = 0xFFF0, **regs: int) -> Regs:
        """Enter an interrupt handler as the CPU would (FLAGS, CS, IP pushed); it ends
        with IRET, or by chaining to another handler that does."""
        seg, off = self.resolve(target)
        ss = self.data_seg if ss is None else ss
        top = sp
        # The return address in the frame: the sentinel, or (cs_frame/ip_frame) where a
        # fault happened, which the handler may read; either way the call ends there.
        ret_seg, ret_off = regs.pop("cs_frame", RETURN_SEG), regs.pop("ip_frame", RETURN_OFF)
        sp = self._push(ss, sp, regs.get("flags", 0x0202))
        sp = self._push(ss, sp, ret_seg)
        sp = self._push(ss, sp, ret_off)
        out = self._run(seg, off, self.linear(ret_seg, ret_off), sp, ss, dict(regs))
        if out.sp != top:
            raise MachineError(f"unbalanced stack after the interrupt (SP {out.sp:04X}, "
                               f"expected {top:04X})")
        return out

    def vector(self, n: int) -> tuple[int, int]:
        """(segment, offset) of interrupt vector n in the interrupt table at 0000:0000."""
        off, seg = struct.unpack("<HH", self.uc.mem_read(4 * n, 4))
        return seg, off

    def set_vector(self, n: int, seg: int, off: int) -> None:
        self.uc.mem_write(4 * n, struct.pack("<HH", off, seg))

    def deliver_through_vector(self, n: int) -> None:
        """From an interrupt hook: enter the handler interrupt table entry n names, as the
        CPU does (the hook sees IP at the faulting or following instruction)."""
        ss, sp = self.reg("ss"), self.reg("sp")
        for word in (self.reg("flags"), self.reg("cs"), self.reg("ip")):
            sp = self._push(ss, sp, word)
        self.set_reg("sp", sp)
        seg, off = self.vector(n)
        self.set_reg("cs", seg)
        self.set_reg("ip", off)

    def regs(self) -> Regs:
        r = {name: self.uc.reg_read(_REGS[name]) for name in
             ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ds", "es", "ss", "flags")}
        return Regs(**r)

    # --- hooks ---------------------------------------------------------------------

    def _where(self) -> str:
        return f"{self.uc.reg_read(UC_X86_REG_CS):04X}:{self.uc.reg_read(UC_X86_REG_IP):04X}"

    def _fail(self, message: str) -> None:
        if self._error is None:
            self._error = MachineError(f"{message} (at {self._where()})")
        self.uc.emu_stop()

    def _on_in(self, uc, port, size, _user):
        handler = self.port_in.get(port)
        if handler is None:
            self._fail(f"unmodelled IN from port {port:03X}h")
            return 0
        return handler(self, port, size)

    def _on_out(self, uc, port, size, value, _user):
        handler = self.port_out.get(port)
        if handler is None:
            self._fail(f"unmodelled OUT {value:X}h to port {port:03X}h")
            return
        handler(self, port, size, value)

    def _on_interrupt(self, uc, intno, _user):
        handler = self.interrupts.get(intno)
        if handler is None:
            self._fail(f"unmodelled INT {intno:02X}h (AX={uc.reg_read(UC_X86_REG_AX):04X})")
            return
        handler(self)

    def _on_invalid(self, uc, access, address, size, value, _user):
        self._fail(f"invalid memory access at linear {address:05X}")
        return False

    def reg(self, name: str) -> int:
        return self.uc.reg_read(_REGS[name])

    def set_reg(self, name: str, value: int) -> None:
        self.uc.reg_write(_REGS[name], value & 0xFFFF)
