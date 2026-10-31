"""R3D.ASM, the 3-D renderer core, against port/asm/r3d.c, routine by routine.

The routines take near pointers into DS. Both sides get the same memory, the original's
on a segment of its own that DS points at, with the arguments placed in it, overlapping
too: the original reads its inputs again after each store.
"""

from __future__ import annotations

import random
import struct

MEM = 256
ANGLES = [
    0,
    1,
    0xF,
    0x10,
    0x11,
    0x2000,
    0x3FFF,
    0x4000,
    0x4001,
    0x7FFF,
    0x8000,
    0x8001,
    0xA000,
    0xBFFF,
    0xC000,
    0xFFF0,
    0xFFFF,
]
MAT3T = [0, 1, 2, 3, 4, 0x7F, 0x80, 0x81, 0xFE, 0xFF]


def original(m, routine, mem: bytes, *offsets):
    m.free_all()
    seg = m.alloc(MEM)
    m.write((seg, 0), mem)
    regs = m.call_far(routine, *offsets, ds=seg)
    return m.read((seg, 0), MEM), regs.ax


def translated(native, command, mem: bytes, *offsets):
    reply = native.call(command, bytes(offsets) + mem)
    return reply[:MEM], struct.unpack_from("<H", reply, MEM)[0]


def check(m, native, routine, command, mem, *offsets):
    want = original(m, routine, mem, *offsets)
    got = translated(native, command, mem, *offsets)
    assert got == want, f"{routine} at {offsets}"


def words(*values) -> bytes:
    return struct.pack(f"<{len(values)}H", *(v & 0xFFFF for v in values))


def test_lighting_ramp(krondor, native):
    assert native.peek("g_abLightingLut") == krondor.read("g_abLightingLut", 256)


def test_rotation_matrix(krondor, native):
    # Every kind (identity, a turn about Z, the full path from the second or the first
    # angle on) over angles at the table's folds, random ones, and angles that overlap the
    # matrix being built.
    rng = random.Random(1)
    cases = (
        [(0, 0, 0)] + [(0, 0, z) for z in ANGLES] + [(0, y, z) for y in ANGLES for z in ANGLES[::3]]
    )
    cases += [(x, y, z) for x in ANGLES for y in ANGLES[::4] for z in ANGLES[::5]]
    cases += [tuple(rng.randrange(0x10000) for _ in range(3)) for _ in range(600)]
    for i, angles in enumerate(cases):
        mem = bytearray(rng.randbytes(MEM))
        if i % 3 == 0:
            check(
                krondor,
                native,
                "r3d_build_rotation_matrix",
                "r3d.build",
                bytes(mem[:0x40]) + words(*angles) + bytes(mem[0x46:]),
                0x40,
                0x80,
            )
        else:
            matrix = 0x80
            angles_at = matrix + 2 * rng.randrange(-2, 10)
            mem[angles_at : angles_at + 6] = words(*angles)
            check(
                krondor,
                native,
                "r3d_build_rotation_matrix",
                "r3d.build",
                bytes(mem),
                angles_at,
                matrix,
            )


def test_vector_transform(krondor, native):
    # Every type byte (the test is signed: 80h and up is the identity), random matrices and
    # vectors over the whole range, the result on the vector, next to it or on the matrix.
    rng = random.Random(2)
    for i in range(1500):
        mem = bytearray(rng.randbytes(MEM))
        matrix, vec = 0x40, 0x80
        if rng.random() < 0.5:
            q14 = [rng.randrange(-0x4000, 0x4001) for _ in range(9)]
            mem[matrix : matrix + 18] = words(*q14)
        mem[matrix + 18] = rng.choice(MAT3T) if i % 4 else rng.randrange(256)
        out = rng.choice(
            (
                0xC0,
                vec,
                vec + 2,
                vec - 2,
                vec + 4,
                matrix + 2 * rng.randrange(-2, 10),
                rng.randrange(0, MEM - 6, 2),
            )
        )
        if i % 7 == 0:
            vec = matrix + 2 * rng.randrange(-2, 10)
        check(
            krondor, native, "r3d_mat3_xform_vec3_wrap", "r3d.xform", bytes(mem), vec, matrix, out
        )


def light_state(m) -> bytes:
    seg, off = m.sym("r3d_camera_setup_view")  # the light vector is the 6 bytes before it
    return m.read("g_matXIn", 6) + m.read("g_mat3x3ScratchRot", 19) + m.read((seg, off - 6), 6)


def test_light_direction(krondor, native):
    # ts_init's own pair, then the folds and random pairs.
    rng = random.Random(3)
    pairs = [(0x2000, -0x6000)] + [(e, a) for e in ANGLES for a in ANGLES[::2]]
    pairs += [(rng.randrange(0x10000), rng.randrange(0x10000)) for _ in range(300)]
    for elevation, azimuth in pairs:
        krondor.call_far("r3d_light_set_direction", elevation & 0xFFFF, azimuth & 0xFFFF)
        got = native.call("r3d.light", words(elevation, azimuth))
        assert got == light_state(krondor), (elevation, azimuth)


def test_imul_full32(krondor, native):
    # Frameless: it reads its arguments through DS, which is SS here as in the game.
    rng = random.Random(4)
    edges = [0, 1, -1, 0x7FFF, -0x8000, 0x4000, -0x4000, 0x00FF, 0x0100]
    pairs = [(a, b) for a in edges for b in edges]
    pairs += [(rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)) for _ in range(2000)]
    want = [krondor.call_far("r3d_imul_full32", a & 0xFFFF, b & 0xFFFF).dx_ax for a, b in pairs]
    payload = b"".join(struct.pack("<hh", a, b) for a, b in pairs)
    got = list(struct.unpack(f"<{len(pairs)}I", native.call("r3d.imul", payload)))
    assert got == want
