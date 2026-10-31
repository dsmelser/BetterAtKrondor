"""MUSDISP.ASM and DISP35E0.ASM (the game's commands to the digital driver) against
port/asm/musdisp.c and disp35e0.c. SNDDRV.ASM is checked with the sequencer, which is
its only caller (test_midi.py's digital streams).

The driver at g_pMusicDriver is the fake of test_midi.py. Each stub must send its
command; the start command its two arguments, through SS:SI, and return the driver's
AX. The other stubs point SI at their caller's stack, which C has no counterpart for.
"""

from __future__ import annotations

import struct

from test_midi import Sequencer

STUBS = (  # snddrv.op number, the original, its command
    (1, "_audio_music_tick", 1),
    (2, "_audio_driver_disp_seg1000_far", 2),
    (3, "_dispatch_via_35e0_cmd0d_seg1000", 0x0D),
    (4, "_FUN_1000_13d7", 6),
    (5, "_FUN_1000_13e9", 9),
    (6, "_FUN_1000_13f0", 0x0A),
    (7, "_FUN_1000_13f7", 0x0B),
    (8, "_FUN_1000_1405", 0x0C),
)


def test_commands(krondor, fresh_native):
    s = Sequencer(krondor, fresh_native)
    m, n = s.m, s.n
    m.write("_g_pMusicDriver", struct.pack("<HH", 0, s.digital.useg))

    def native(op: int, a: int = 0, b: int = 0) -> int:
        reply = n.call("snddrv.op", struct.pack("<BI2H", op, s.digital.naddr, a, b))
        return struct.unpack("<H", reply)[0]

    for a, b in ((0, 1), (0x1234, 1), (0xFFFF, 0x8000)):
        r = m.call_far("_audio_driver_disp_seg1000_alt", a, b)
        assert native(0, a, b) == r.ax == 0x4400
        assert s.native_log() == s.ulog == [("D", 0, struct.pack("<HH", a, b))]
        s.ulog = []
    for op, name, cmd in STUBS:
        m.call_far(name)
        native(op)
        assert [e[:2] for e in s.native_log()] == [e[:2] for e in s.ulog] == [("D", cmd)], name
        s.ulog = []
