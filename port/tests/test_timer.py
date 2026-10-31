"""TIMER.ASM (IRQ 0 and the callback registry) against port/asm/timer.c.

Registry callbacks are 16 probes on each side that log their number, so the order in
which a run of ticks calls them is compared, along with the registry, the countdowns
and how often the tick was passed on to the BIOS handler (also a probe).
"""

from __future__ import annotations

import random
import struct

import pytest

TICKS = 120
SCHED = 64  # TimerSched[16]: countdown, reload


def probe_code(log_seg: int, k: int) -> bytes:
    """push ds; push bx; mov bx, log; mov ds, bx; mov bx, [0]; mov [bx+2], k;
    inc word [0]; pop bx; pop ds; retf"""
    return bytes([0x1E, 0x53, 0xBB, log_seg & 0xFF, log_seg >> 8, 0x8E, 0xDB, 0x8B, 0x1E, 0, 0,
                  0xC6, 0x47, 0x02, k, 0xFF, 0x06, 0, 0, 0x5B, 0x1F, 0xCB])


BIOS_PROBE = bytes([0x2E, 0xFE, 0x06, 0x10, 0x00, 0xCF])  # inc byte cs:[10h]; iret


class Original:
    def __init__(self, m):
        self.m = m
        self.log = m.alloc(0x1000)
        self.probes = m.alloc(16 * 32)
        for k in range(16):
            m.write((self.probes, 32 * k), probe_code(self.log, k))
        self.bios = m.alloc(0x20)
        m.write((self.bios, 0), BIOS_PROBE)
        seg, off = m.sym("timer_install")
        m.write((seg, off - 4), struct.pack("<HH", 0, self.bios))  # the saved-vector island
        m.port_out[0x20] = lambda m, port, size, value: None
        self.vectors: dict[int, tuple[int, int]] = {}
        m.interrupts[0x21] = self.dos
        m.port_out[0x40] = m.port_out[0x43] = m.port_out[0x21] = lambda m, p, s, v: None
        m.port_in[0x21] = lambda m, port, size: 0xB8

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

    def probe_far(self, k: int) -> tuple[int, int]:
        return 32 * k, self.probes

    def set_callbacks(self, probes: list[int]) -> None:
        self.m.write("_g_aTimerCallback",
                     b"".join(struct.pack("<HH", *self.probe_far(k)) for k in probes))

    def ticks(self, n: int) -> tuple[int, bytes]:
        self.m.write((self.log, 0), bytes(0x1000))
        self.m.set_u8((self.bios, 0x10), 0)
        for _ in range(n):
            self.m.call_interrupt("timer_irq0_dispatch")
        logged = self.m.u16((self.log, 0))
        return self.m.u8((self.bios, 0x10)), self.m.read((self.log, 2), logged)


STATE = {"g_timer_installed": 1, "g_nFrameTickCountdownValue": 2, "g_nTimerPitDivisor": 2,
         "g_nTimerBiosChainReload": 2, "g_nTimerBiosChainCountdown": 2,
         "g_registry_slot_mask": 2, "g_aTimerSched": SCHED}


def original_name(name: str) -> str:
    return "_g_nFrameTickCountdown" if name == "g_nFrameTickCountdownValue" else "_" + name


def assert_state(m, native, what: str) -> None:
    for name, size in STATE.items():
        assert native.peek(name) == m.read(original_name(name), size), f"{name} {what}"


def set_state(m, native, name: str, data: bytes) -> None:
    m.write(original_name(name), data)
    native.poke(name, data)


@pytest.fixture
def timer(krondor, native):
    o = Original(krondor)
    for name, size in STATE.items():
        set_state(krondor, native, name, bytes(size))
    return krondor, o


def test_registry(timer, native):
    m, o = timer
    rng = random.Random(22)
    set_state(m, native, "g_timer_installed", b"\x01")
    for i in range(300):
        if rng.random() < 0.6:
            k, interval = rng.randrange(16), rng.randrange(0x10000)
            seg, off = o.probe_far(k)[1], o.probe_far(k)[0]
            got = m.call_far("registry_slot_alloc", off, seg, interval).ax
            want = native.call("timer.alloc", struct.pack("<BH", k, interval))
        else:
            slot = rng.choice((0, 1, 5, 16, 17, 0x100, 0x101, -1, rng.randrange(1, 17)))
            got = m.call_far("registry_slot_free", slot & 0xFFFF).ax
            want = native.call("timer.free", struct.pack("<h", slot))
        assert struct.unpack("<H", want)[0] == got, f"step {i}"
        assert_state(m, native, f"after step {i}")
    set_state(m, native, "g_timer_installed", b"\x00")
    assert m.call_far("registry_slot_alloc", 0, o.probes, 5).ax == 0
    assert native.call("timer.alloc", struct.pack("<BH", 0, 5)) == b"\0\0"


def test_ticks(timer, native):
    m, o = timer
    rng = random.Random(23)
    native.call("timer.hook_bios")  # installs: copy what that set to the original's side
    for name in ("g_timer_installed", "g_nTimerPitDivisor"):
        m.write(original_name(name), native.peek(name))
    for trial in range(30):
        probes = [rng.randrange(16) for _ in range(16)]
        o.set_callbacks(probes)
        native.call("timer.set_callbacks", bytes(probes))
        mask = rng.choice((0, 1, 0x8000, 0xFFFF, rng.randrange(0x10000)))
        sched = b"".join(struct.pack("<HH", rng.choice((0, 1, 2, rng.randrange(1, 40))),
                                     rng.choice((1, 3, rng.randrange(0, 40))))
                         for _ in range(16))
        reload = rng.randrange(1, 20)
        for name, value in (("g_registry_slot_mask", struct.pack("<H", mask)),
                            ("g_aTimerSched", sched),
                            ("g_nFrameTickCountdownValue",
                             struct.pack("<h", rng.choice((0, 1, 50, -1, -32768)))),
                            ("g_nTimerBiosChainReload", struct.pack("<H", reload)),
                            ("g_nTimerBiosChainCountdown",
                             struct.pack("<H", rng.randrange(1, reload + 1)))):
            set_state(m, native, name, value)
        bios, log = o.ticks(TICKS)
        reply = native.call("timer.ticks", struct.pack("<H", TICKS))
        assert struct.unpack_from("<H", reply)[0] == bios, f"BIOS chains, trial {trial}"
        assert reply[2:] == log, f"callback order, trial {trial}"
        assert_state(m, native, f"after trial {trial}")


@pytest.mark.parametrize("frequency", [13, 1, 255, 256, 0, 0x8000])
def test_install(timer, native, frequency):
    m, o = timer
    m.call_far("timer_install", frequency)
    native.call("timer.install", struct.pack("<H", frequency))
    assert_state(m, native, f"after timer_install({frequency})")
    got = m.call_far("timer_restore_default").ax
    assert struct.unpack("<H", native.call("timer.restore"))[0] == got
    assert_state(m, native, "after timer_restore_default")
