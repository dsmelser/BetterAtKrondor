"""CDAUDIO.ASM (1.02 CD audio over MSCDEX) against port/asm/cdaudio.c.

Both sides talk to the same MSCDEX model (port/platform/cdrom.c, and its twin below):
absent, or a drive with a table of audio track starts. Sequences of detect, track
selection, play and stop must leave the state block (and its request header) the same.
The IOCTL control block's address in the header is a DOS far pointer on one side and a
host pointer on the other, so it is compared only after a play has overwritten it.
"""

from __future__ import annotations

import random
import struct

import pytest

STATE = 70
ADDRESS = slice(0x27, 0x2B)


class Mscdex:
    def __init__(self, m, drive: int, first: int, starts: list[tuple[int, int, int]]):
        self.drive, self.first, self.starts = drive, first, starts
        m.interrupts[0x2F] = self.int2f
        m.interrupts[0x21] = self.int21

    def int21(self, m):
        if m.reg("ax") >> 8 != 0x19:
            m._fail(f"unmodelled INT 21h AX={m.reg('ax'):04X}h")
        m.set_reg("ax", (m.reg("ax") & 0xFF00) | 2)  # current drive C:

    def int2f(self, m):
        if self.drive < 0:
            return  # MSCDEX absent: registers unchanged
        ax, cx = m.reg("ax"), m.reg("cx")
        if ax == 0x150B:
            m.set_reg("bx", 0xADAD)
            m.set_reg("ax", 0x5AD8 if cx == self.drive else 0)
        elif ax == 0x1510:
            self.request(m, (m.reg("es"), m.reg("bx")), cx)
        else:
            m._fail(f"unmodelled INT 2Fh AX={ax:04X}h")

    def request(self, m, hdr, drive):
        def status(v):
            m.write((hdr[0], hdr[1] + 3), struct.pack("<H", v))
        if drive != self.drive:
            return status(0x8101)
        cmd = m.u8((hdr[0], hdr[1] + 2))
        if cmd == 0x03:
            off, seg = struct.unpack("<HH", m.read((hdr[0], hdr[1] + 0x0E), 4))
            code, track = m.read((seg, off), 2)
            last = self.first + len(self.starts) - 1
            if code == 0x0B and self.first <= track <= last:
                mn, sc, fr = self.starts[track - self.first]
                m.write((seg, off + 2), bytes([fr, sc, mn, 0, 1]))
                status(0x0100)
            else:
                status(0x8103)
        elif cmd == 0x84:
            status(0x0300)
        elif cmd == 0x85:
            status(0x0100)
        else:
            status(0x8103)


def attach(native, drive, first, starts) -> None:
    payload = struct.pack("<bBB", drive, first, first + len(starts) - 1)
    payload += b"".join(bytes(s) for s in starts)
    native.call("cd.attach", payload)


@pytest.mark.parametrize("drive", [-1, 3, 4])
def test_cd_sequences(krondor, native, drive):
    m = krondor
    rng = random.Random(drive)
    starts = [(rng.randrange(80), rng.randrange(60), rng.randrange(75)) for _ in range(20)]
    Mscdex(m, drive, 2, starts)
    attach(native, drive, 2, starts)
    native.poke("g_cdaudio_state", m.read("_g_cdaudio_state", STATE))
    last_ioctl = False
    for i in range(400):
        op = rng.choice(("detect", "track", "track", "play", "stop"))
        if op == "detect":
            letter = rng.choice((0, ord("D"), ord("d"), ord("E"), ord("Z"), 0x20))
            got = m.call_far("v102_cddrive_detect", letter).ax
            assert struct.unpack("<H", native.call("cd.detect", bytes([letter])))[0] == got
        elif op == "track":
            track = rng.choice((0, 1, 2, 5, 21, 22, 0x102, rng.randrange(2, 22)))
            bias, offset = rng.choice((-75, 0, 150)), rng.choice((0, 0, rng.randrange(1 << 20)))
            m.call_far("v102_cdaudio_set_track_msf", track, bias & 0xFFFF, (bias >> 16) & 0xFFFF,
                       offset & 0xFFFF, offset >> 16)
            native.call("cd.set_track", struct.pack("<hii", track, bias, offset))
            last_ioctl = last_ioctl or m.read("_g_cdaudio_state", STATE)[0x1B] == 3
        elif op == "play":
            m.call_far("v102_cdaudio_play")
            native.call("cd.play")
            last_ioctl = False
        else:
            m.call_far("v102_cdaudio_stop")
            native.call("cd.stop")
        got, want = bytearray(m.read("_g_cdaudio_state", STATE)), bytearray(native.peek("g_cdaudio_state"))
        if last_ioctl:
            got[ADDRESS] = want[ADDRESS] = bytes(4)
        assert want == got, f"step {i} ({op})"
