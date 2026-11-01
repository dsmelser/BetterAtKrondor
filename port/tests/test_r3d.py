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


def original(m, routine, mem: bytes, *offsets, stack_too: bool = False):
    """The routine on mem at DS:0; stack_too puts SS there as well (the game has SS = DS,
    and some routines write their results through BP)."""
    m.free_all()
    seg = m.alloc(MEM)
    m.write((seg, 0), mem)
    regs = m.call_far(routine, *offsets, ds=seg, ss=seg if stack_too else None)
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


def matrix_bytes(rng, kind: int | None = None) -> bytes:
    """A matrix: Q14 values or any 16-bit ones (whose sums overflow), and a type byte."""
    if rng.random() < 0.6:
        values = [rng.randrange(-0x4000, 0x4001) for _ in range(9)]
    else:
        values = [rng.choice((-0x8000, 0x7FFF, rng.randrange(0x10000))) for _ in range(9)]
    if kind is None:
        kind = rng.choice(MAT3T) if rng.random() < 0.8 else rng.randrange(256)
    return words(*values) + bytes([kind])


def test_matrix_multiply(krondor, native):
    # Every pair of types (0 and 1 have paths of their own, any other is a full matrix),
    # the product apart, on either operand, overlapping one, and both operands the same.
    rng = random.Random(5)
    kinds = [0, 1, 2, 3, 0x80, 0xFF]
    cases = [(ka, kb) for ka in kinds for kb in kinds for _ in range(25)]
    cases += [(None, None)] * 1200
    for i, (ka, kb) in enumerate(cases):
        mem = bytearray(rng.randbytes(MEM))
        a, b = 0x20, 0x60
        mem[a : a + 19] = matrix_bytes(rng, ka)
        mem[b : b + 19] = matrix_bytes(rng, kb)
        if i % 9 == 0:
            b = a
        out = rng.choice(
            (
                0xA0,
                a,
                b,
                a + 2 * rng.randrange(-9, 10),
                b + 2 * rng.randrange(-9, 10),
                rng.randrange(0, MEM - 20, 2),
            )
        )
        check(krondor, native, "r3d_matrix3x3_multiply", "r3d.mul", bytes(mem), a, b, out)


def test_matrix_transpose(krondor, native):
    rng = random.Random(6)
    for _ in range(200):
        mem = rng.randbytes(MEM)
        check(
            krondor,
            native,
            "r3d_matrix3x3_transpose",
            "r3d.transpose",
            mem,
            rng.randrange(0, MEM - 20, 2),
        )


def test_matrix_to_euler(krondor, native):
    # m[5] over the arcsine table's range (past it the original reads its code), most
    # often near +-1.0, where the first angle reaches +-90 degrees and the other two fold
    # into one; the angles apart from the matrix or over it (out[1] is stored before the
    # reads for out[2]).
    rng = random.Random(7)
    sines = [0, 1, -1, 0x3FC0, 0x3FDF, 0x3FE0, 0x3FFF, 0x4000, 0x401F, -0x3FE0, -0x4000, -0x401F]
    for i in range(1500):
        mem = bytearray(rng.randbytes(MEM))
        matrix = 0x40
        mem[matrix : matrix + 19] = matrix_bytes(rng)
        if i < 300:
            sine = sines[i % len(sines)]
        elif rng.random() < 0.3:
            sine = rng.randrange(0x3F00, 0x4020) * rng.choice((1, -1))
        else:
            sine = rng.randrange(-0x401F, 0x4020)
        mem[matrix + 10 : matrix + 12] = words(sine)
        for k in (0, 1, 2, 3, 4, 8):  # the elements atan2 reads: the extremes too
            if rng.random() < 0.1:
                mem[matrix + 2 * k : matrix + 2 * k + 2] = words(rng.choice((0, -0x8000, 0x7FFF)))
        out = 0xA0 if i % 3 else matrix + 2 * rng.randrange(-2, 9)
        check(krondor, native, "r3d_matrix_to_euler", "r3d.euler", bytes(mem), matrix, out)


VIEW_AT, CAMERA_AT = 0xFE00, 0xFE40  # in DGROUP, under the test's stack


def camera_state(m) -> bytes:
    ctx_seg, ctx_off = m.sym("g_graphics_context")
    return (
        m.read("g_camCurWorldPos", 12)
        + m.read("g_camViewYaw", 6)
        + m.read("g_nScreenShift", 2)
        + m.read("g_nViewPlaneDist", 2)
        + m.read("g_pFaceMaterialTable", 2)
        + m.read("g_pColorRemapTable", 2)
        + m.read((ctx_seg, ctx_off + 4), 8)  # clip.xmin, xmax, ymin, ymax
        + m.read("g_nViewportCenterX", 4)
        + m.read("g_bCamSetupResetFlag", 3)  # g_bSkipFarEdgeCull, g_bActorViewportClip
        + m.read("g_matXIn", 6)
        + m.read("g_mat3x3ScratchRot", 19)
        + m.read("g_mat3x3ViewRot", 19)
        + m.read("g_mat3x3ActorRot", 19)
    )


def test_camera_setup(krondor, native):
    # The game's views (no view rotation: the camera's angles give the matrix) and turned
    # ones (composed, the camera's angles recovered from the product), over random
    # cameras, viewports anywhere in the 16-bit range, and zooms whose shift passes 15 or
    # 31 or has a high byte (stored with it).
    rng = random.Random(8)
    zooms = [7, 8, 0, 1, 15, 16, 31, 32, 0x107, 0xFFFF]
    for i in range(1200):
        zoom = zooms[i % len(zooms)] if i < 200 else rng.randrange(0x10000)
        kind = i % 4
        rotation = [0, 0, 0]
        if kind == 1:
            rotation[2] = rng.randrange(1, 0x10000)
        elif kind == 2:
            rotation[1:] = [rng.randrange(1, 0x10000), rng.randrange(0x10000)]
        elif kind == 3:
            rotation = [rng.randrange(1, 0x10000), rng.randrange(0x10000), rng.randrange(0x10000)]
        if rng.random() < 0.5:
            viewport = [
                rng.randrange(320),
                rng.randrange(200),
                rng.randrange(1, 321),
                rng.randrange(1, 201),
            ]
        else:
            viewport = [rng.randrange(0x10000) for _ in range(4)]
        face, remap = rng.randrange(0x10000), rng.randrange(0x10000)
        position = [rng.randrange(-0x80000000, 0x80000000) for _ in range(3)]
        orientation = [rng.choice((0, rng.randrange(0x10000))) for _ in range(3)]
        actor = rng.randbytes(19)
        flags = rng.randbytes(3)  # before: cleared, cleared, kept

        view = words(zoom, CAMERA_AT, *rotation, *viewport, face, remap)
        camera = words(rng.randrange(0x10000)) + struct.pack("<3i", *position) + words(*orientation)
        krondor.write((krondor.data_seg, VIEW_AT), view)
        krondor.write((krondor.data_seg, CAMERA_AT), camera)
        krondor.write("g_mat3x3ActorRot", actor)
        krondor.write("g_bCamSetupResetFlag", flags)
        krondor.call_far("r3d_camera_setup_view", VIEW_AT)

        payload = (
            words(zoom, *rotation, *viewport, face, remap)
            + struct.pack("<3i", *position)
            + words(*orientation)
            + actor
            + flags
        )
        got = native.call("r3d.camera", payload)
        assert got == camera_state(krondor), (zoom, rotation, viewport, position, orientation)


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


def s32(v: int) -> int:
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


EDGES32 = [0, 1, -1, 0x7F, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x00FFFFFF, 0x01000000]
EDGES32 += [0x7FFFFFFF, -0x80000000, -0x10000, -0x8000, 0x12345678, -0x12345678]
VIEW_DISTS = [0x80, 0x100, 0x200]  # 1 << zoom


def test_ldiv(krondor, native):
    # The game's ratios (the view distance over a depth at or beyond it), then every path:
    # a depth under 64K (the divisor unnormalized, a numerator over 64K swapped), with a
    # zero top byte, ratios of 1 and above, signs, 80000000h, and values with a zero high
    # word or fraction.
    rng = random.Random(9)
    cases = []
    for _ in range(600):
        num = rng.choice(VIEW_DISTS)
        den = rng.choice(
            (num, num + 1, rng.randrange(num, 0x10000), rng.randrange(0x10000, 1 << 31))
        )
        cases.append(
            (
                rng.choice(EDGES32 + [rng.randrange(-(1 << 31), 1 << 31)] * 4),
                rng.randrange(-0x20000, 0x20000),
                den,
                num,
            )
        )
    for den in EDGES32:
        for num in EDGES32:
            cases.append((rng.choice(EDGES32), rng.randrange(-(1 << 31), 1 << 31), den, num))
    for _ in range(1500):
        den = rng.choice(
            (
                rng.randrange(-(1 << 31), 1 << 31),
                rng.randrange(0x10000),
                rng.randrange(0x10000, 0x1000000),
            )
        )
        num = rng.choice(
            (
                rng.randrange(-(1 << 31), 1 << 31),
                rng.randrange(0x10000),
                rng.randrange(abs(den) + 1) if den else 0,
            )
        )
        cases.append(
            (rng.randrange(-(1 << 31), 1 << 31), rng.choice((0, rng.randrange(0x10000))), den, num)
        )
    want = []
    for a, b, den, num in cases:
        stack = struct.pack("<4i", a, b, s32(den), s32(num))
        krondor.call_near("r3d_ldiv_helper", *struct.unpack("<8H", stack))
        want.append(krondor.read((krondor.data_seg, 0xFFF0 - 16), 8))
    payload = b"".join(struct.pack("<4i", a, b, s32(den), s32(num)) for a, b, den, num in cases)
    reply = native.call("r3d.ldiv", payload)
    got = [reply[k : k + 8] for k in range(0, len(reply), 8)]
    for case, g, w in zip(cases, got, want):
        assert g == w, [hex(v & 0xFFFFFFFF) for v in case]


def test_projection(krondor, native):
    # The perspective divide and the projected scale (the view distance over nDenom, the
    # result over the arguments too) and the screen translation, over the game's view
    # distances and any.
    rng = random.Random(10)
    for i in range(600):
        dist = rng.choice(VIEW_DISTS) if i % 3 else rng.randrange(0x10000)
        mem = bytearray(rng.randbytes(MEM))
        args = 0x40
        nums = [rng.choice(EDGES32 + [rng.randrange(-(1 << 31), 1 << 31)] * 3) for _ in range(3)]
        if i % 2:
            nums[1] = rng.randrange(dist, 0x100000)  # nDenom: a depth beyond the view plane
        mem[args : args + 12] = struct.pack("<3i", *(s32(v) for v in nums))
        out = rng.choice((0xA0, args, args + 4, args - 4, args + 8))
        krondor.write("g_nViewPlaneDist", words(dist))
        want = original(
            krondor, "r3d_vec2_perspective_divide", bytes(mem), args, out, stack_too=True
        )
        got = native.call("r3d.persp", words(dist) + bytes((args, out)) + bytes(mem))
        assert (got[:MEM], struct.unpack_from("<H", got, MEM)[0]) == want, (dist, nums)

        krondor.free_all()
        seg = krondor.alloc(MEM)
        krondor.write((seg, 0), bytes(mem))
        scale = krondor.call_far("r3d_actor_proj_scale_compute", args, ds=seg).dx_ax
        got = native.call("r3d.projscale", words(dist) + bytes((args,)) + bytes(mem))
        assert struct.unpack("<I", got)[0] == scale, (dist, nums)

        cx, cy = (
            rng.choice((160, 159, rng.randrange(0x10000))),
            rng.choice((60, rng.randrange(0x10000))),
        )
        krondor.write("g_nViewportCenterX", words(cx, cy))
        pt = rng.randrange(0, MEM - 8, 2)
        krondor.free_all()
        seg = krondor.alloc(MEM)
        krondor.write((seg, 0), bytes(mem))
        ax = krondor.call_far("r3d_vec2_long_translate_yflip", pt, ds=seg).ax
        got = native.call("r3d.yflip", words(cx, cy) + bytes((pt,)) + bytes(mem))
        assert got == krondor.read((seg, 0), MEM) + words(ax), (cx, cy, pt)


STATE_SIZE, STATE_PTRS, STATE_REST = 0xAD, 0x85, 0x8B
ST_SHIFT, ST_HALF, ST_RELX = 0x08, 0x8B, 0x4D
ST_CAMX, ST_CLAMPZ, ST_CAMPOS = 0x59, 0x9F, 0x71
ST_VIEWROT, ST_ACTORROT, ST_DIST = 0x14, 0x27, 0xA5


def rotation_bytes(rng) -> bytes:
    """A matrix the builder makes (every type), or any values with any type byte."""
    if rng.random() < 0.7:
        m = Machine_free_matrix(rng)
        return m
    return matrix_bytes(rng)


def Machine_free_matrix(rng) -> bytes:
    # A rotation from random angles, built on the Python side the way the tests know: the
    # identity, a turn about Z, or a full one, as Q14 values within one.
    kind = rng.choice((0, 1, 3, 3))
    if kind == 0:
        return words(0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000) + bytes([0])
    values = [rng.randrange(-0x4000, 0x4001) for _ in range(9)]
    if kind == 1:
        values[2] = values[5] = values[6] = values[7] = 0
        values[8] = 0x4000
    return words(*values) + bytes([kind])


def actor_case(m, native, rng, step: int, tweak=None) -> None:
    state = bytearray(rng.randbytes(STATE_SIZE))
    scratch = bytearray(rng.randbytes(24))
    shift = rng.choice((0, 0, 1, 2, 3, rng.randrange(32), rng.randrange(0x10000)))
    state[ST_SHIFT : ST_SHIFT + 2] = words(shift)
    state[ST_DIST : ST_DIST + 2] = words(rng.choice(VIEW_DISTS + [rng.randrange(0x10000)]))
    state[ST_VIEWROT : ST_VIEWROT + 19] = rotation_bytes(rng)
    state[ST_ACTORROT : ST_ACTORROT + 19] = rotation_bytes(rng)
    if rng.random() < 0.6:  # the game's magnitudes: positions within a zone, near the camera
        for off, n in ((ST_CAMPOS, 3), (ST_RELX, 3), (ST_CAMX, 3)):
            state[off : off + 4 * n] = struct.pack(
                f"<{n}i", *(rng.randrange(-0x40000, 0x40000) for _ in range(n))
            )
        state[ST_HALF : ST_HALF + 4] = struct.pack("<i", rng.randrange(0, 0x8000))
        state[ST_CLAMPZ : ST_CLAMPZ + 4] = struct.pack("<i", rng.randrange(0x80, 0x40000))
    entry = bytearray(rng.randbytes(22))
    if rng.random() < 0.5:
        entry[2:14] = struct.pack("<3i", *(rng.randrange(-0x40000, 0x40000) for _ in range(3)))
    record = bytearray(rng.randbytes(26))
    record[0] = rng.choice((0, 0x20, 0x40, 0x60, rng.randrange(256)))
    if tweak is not None:
        tweak(state)

    m.free_all()
    seg = m.alloc(0x40)
    m.write((seg, 0), bytes(entry) + bytes(record))
    orig_state = bytearray(state)
    orig_state[STATE_PTRS : STATE_PTRS + 4] = words(22, seg)  # the record, after the entry
    m.write("g_abR3dState", bytes(orig_state))
    m.write("g_matXIn", bytes(scratch[:18]))
    cs_seg, cs_off = m.sym("r3d_camera_setup_view")
    m.write((cs_seg, cs_off - 6), bytes(scratch[18:]))
    routine = (
        "r3d_setup_actor_in_camera_space",
        "r3d_compose_actor_camera_matrix",
        "r3d_actor_compute_lod_z",
    )[step]
    if step == 0:
        m.call_far(routine, 0, seg)
    else:
        m.call_far(routine)
    want = bytearray(m.read("g_abR3dState", STATE_SIZE))
    want[STATE_PTRS:STATE_REST] = bytes(STATE_REST - STATE_PTRS)
    want += m.read("g_matXIn", 18) + m.read((cs_seg, cs_off - 6), 6)
    got = native.call(
        "r3d.actor", bytes([step]) + bytes(state) + bytes(scratch) + bytes(entry) + bytes(record)
    )
    if got != bytes(want):
        at = next(k for k in range(len(got)) if got[k] != want[k])
        raise AssertionError(f"{routine}: differs at {at:#x} (shift {shift})")


def test_actor_setup(krondor, native):
    # The actor into camera space: every type of view matrix, positions in a zone and
    # anywhere, depths on both sides of the view plane.
    rng = random.Random(11)
    for _ in range(800):
        actor_case(krondor, native, rng, 0)


def test_actor_compose(krondor, native):
    # The actor's matrix and its mesh's scale: mesh shifts of 0-3 as the game's, up to the
    # 16-bit range (the shifts saturate), extents whose sum is negative or past 3FACh.
    rng = random.Random(12)
    for _ in range(800):
        actor_case(krondor, native, rng, 1)

    # The extent plus the half extent just under, on and over 3FACh, before and after shifts.
    def edge(total: int, shift: int):
        def tweak(state: bytearray) -> None:
            extent = rng.randrange(total + 1)
            state[ST_SHIFT : ST_SHIFT + 2] = words(shift)
            state[ST_CAMX : ST_CAMX + 12] = struct.pack("<3i", -extent, extent // 2, 0)
            state[ST_HALF : ST_HALF + 4] = struct.pack("<i", total - extent)

        return tweak

    for shift in (0, 1, 3):
        for k in (0, 1, 4):
            for total in (0x3FAB, 0x3FAC, 0x3FAD):
                actor_case(krondor, native, rng, 1, edge((total << k) << shift, shift))


def test_actor_lod_depth(krondor, native):
    # Simple meshes (flag 20h) and boxed ones, matrix elements of 0, any box.
    rng = random.Random(13)
    for _ in range(800):
        actor_case(krondor, native, rng, 2)
