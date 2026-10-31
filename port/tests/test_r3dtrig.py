"""R3DTRIG.ASM (table trigonometry for the 3-D renderer) against port/asm/r3dtrig.c.

Every entry of the three tables is reached through the routines: sin and cos at an angle
in each of the 4096 sixteenths of the turn, asin at a value in each 32nd of the range
(both signs), the raw arctangent at every index; atan2 over random vectors of three
magnitudes and the axes, diagonals and -8000h.
"""

from __future__ import annotations

import random
import struct


def _words(values) -> bytes:
    return struct.pack(f"<{len(values)}H", *(v & 0xFFFF for v in values))


def _compare(m, native, routine: str, command: str, cases: list[tuple[int, ...]]) -> None:
    want = [m.call_far(routine, *(a & 0xFFFF for a in case)).ax for case in cases]
    reply = native.call(command, _words([a for case in cases for a in case]))
    got = list(struct.unpack(f"<{len(reply) // 2}H", reply))
    for case, w, g in zip(cases, want, got, strict=True):
        assert g == w, f"{routine}{case}: original {w:#06x}, native {g:#06x}"


def test_sin_cos(krondor, native):
    rng = random.Random(30)
    angles = [(k * 16 + rng.randrange(16),) for k in range(4096)]
    _compare(krondor, native, "r3d_tbl_sin", "r3dtrig.sin", angles)
    _compare(krondor, native, "r3d_tbl_cos", "r3dtrig.cos", angles)


def test_asin(krondor, native):
    rng = random.Random(31)
    values = [0, 0x4000, -0x4000, 0x401F, -0x401F]
    values += [sign * (k * 32 + rng.randrange(32)) for k in range(0x201) for sign in (1, -1)]
    cases = [(v,) for v in values if -0x401F <= v <= 0x401F]
    _compare(krondor, native, "r3d_tbl_asin", "r3dtrig.asin", cases)


def test_atan2(krondor, native):
    rng = random.Random(32)
    cases = [(0, 0), (0, 5), (0, -5), (5, 0), (-5, 0), (7, 7), (-7, 7), (7, -7), (-7, -7),
             (-0x8000, 1), (1, -0x8000), (-0x8000, -0x8000), (0x7FFF, -0x8000),
             (-0x8000, 0x7FFF), (0x7FFF, 0x7FFE), (0x7FFE, 0x7FFF)]
    for _ in range(3000):
        size = rng.choice((8, 300, 0x8000))
        cases.append((rng.randrange(-size, size), rng.randrange(-size, size)))
    _compare(krondor, native, "r3d_tbl_atan2", "r3dtrig.atan2", cases)


def test_atan_lookup_raw(krondor, native):
    _compare(krondor, native, "r3d_tbl_atan_lookup_raw", "r3dtrig.atan_raw",
             [(i,) for i in range(0x200)])
