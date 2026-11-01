"""R3D.ASM's mesh producers against port/asm/r3d.c, on generated meshes.

A mesh part (faces of every kind and flag, vertex lists sharing a pool, normals that make
floor tiles) goes through the LOD 0, LOD 1-2 and LOD 3 producers from the same renderer
state on both sides: the polygon arrays and vertex caches as test_r3dclip.py's two blocks,
the state block as test_r3d.py's, the vertex cache kept or not. The game's polygon fillers
are hooked: every polygon submitted, with the context's corners, fill flag and colours at
that moment, must match, and so must everything the producers leave. The IDIVs of the
16-bit projections and of the clippers divide as the divide-error handler does; a zero
divisor, on which the original hangs, discards the case.
"""

from __future__ import annotations

import random
import struct

from capstone import CS_ARCH_X86, CS_MODE_16, Cs
from test_r3d import STATE_PTRS, STATE_REST, STATE_SIZE, VIEW_DISTS, rotation_bytes, words
from test_r3dclip import CAM_Y, CLIP_STATE, COUNT, HI_SIZE, LO_SIZE, POLY_VERTICES, VIEW_RECT
from test_r3dclip import Pair as ClipPair

from harness.machine import MachineError

PRODUCERS = {0: "r3d_mesh_part_polygons", 1: "r3d_mesh_polygons", 3: "r3d_mesh_part_polygons_lod3"}
INT_FLAT, INT_TEXTURED, INT_IDIV_SI = 0xF1, 0xF3, 0xF4  # the clippers' IDIVs take F0h, F2h
REMAP_AT = 0xF000  # the colour remap table in DGROUP, under the test's stack
LOG_ENTRY = 88

# Offsets in the state block.
ST_POOL, ST_COUNT, ST_SHIFT, ST_RESIDUAL = 0x04, 0x06, 0x08, 0x0A
ST_LOD, ST_VIEWCLIP, ST_ACTORROT, ST_CAM = 0x0E, 0x11, 0x27, 0x59
ST_CAMORIGIN, ST_LIGHT, ST_DIST, ST_SCREENSHIFT, ST_CENTER = 0x65, 0x99, 0xA5, 0xA7, 0xA9


class MeshPair(ClipPair):
    """test_r3dclip's pair, its producers' IDIVs patched too and the fillers logged."""

    def __init__(self, m, native):
        super().__init__(m, native)
        for name in PRODUCERS.values():
            self._patch_si(name)
        m.interrupts[INT_IDIV_SI] = lambda machine: self._idiv(machine, "si")
        offs = struct.unpack("<4H", native.call("r3d.ctxoffs"))
        self.flag_off, self.px_off, self.py_off, self.count_off = offs
        self.ctx = m.sym("g_graphics_context")
        for name, number in (
            ("sky_polygon_draw_dispatch", INT_FLAT),
            ("worldrender_textured_quad_lod", INT_TEXTURED),
        ):
            m.write(m.sym(name), bytes((0xCD, number, 0xCB)))  # INT n; RETF
        m.interrupts[INT_FLAT] = lambda machine: self._log(1)
        m.interrupts[INT_TEXTURED] = lambda machine: self._log(2)
        self.log: list[bytes] = []
        # R3D's own words, each before its producer's entry
        part, mesh, lod3 = (m.sym(PRODUCERS[k]) for k in (0, 1, 3))
        self.words_at = (
            (part[0], part[1] - 3, 3),  # g_nMeshPartPolygonsRemaining, g_bNearClipNeeded
            (mesh[0], mesh[1] - 3, 3),  # g_nMeshPolyRemaining, g_bMeshPolyHasZClipVertex
            (lod3[0], lod3[1] - 2, 2),  # g_nPolyListRemaining
        )
        light_seg, light_off = m.sym("r3d_camera_setup_view")
        self.light_at = (light_seg, light_off - 6)

    def _patch_si(self, name: str) -> None:
        seg, off = self.m.sym(name)
        rel_seg = self.m.image.symbol(name)[0]
        end = min(o for s, o in self.m.image.symbols.values() if s == rel_seg and o > off)
        code = self.m.read((seg, off), end - off)
        for insn in Cs(CS_ARCH_X86, CS_MODE_16).disasm(code, off):
            if insn.mnemonic == "idiv":
                assert insn.op_str == "si", f"{name}: idiv {insn.op_str}"
                self.m.write((seg, insn.address), bytes((0xCD, INT_IDIV_SI)))

    def _log(self, which: int) -> None:
        m = self.m
        first, xs, ys = struct.unpack("<3H", m.read((m.reg("ss"), m.reg("sp") + 4), 6))
        assert (xs, ys) == (self.ctx[1] + self.px_off, self.ctx[1] + self.py_off)
        c = m.read(self.ctx, 0x6F2)
        self.log.append(
            bytes((which,))
            + words(first)
            + c[self.count_off : self.count_off + 2]
            + c[self.px_off : self.px_off + 40]
            + c[self.py_off : self.py_off + 40]
            + c[0x0C:0x0F]  # fill enabled, fill, outline
        )

    def read_words(self) -> bytes:
        return b"".join(self.m.read((s, o), n) for s, o, n in self.words_at)


def _mesh(rng: random.Random, producer: int):
    """A shape record, a part header, its faces and their vertex lists, and the pool."""
    n = rng.randrange(4, 40)
    big = producer == 1
    span = rng.choice((0x40, 0x200, 0x800 if big else 0x300))
    pool = [[rng.randrange(-span, span) for _ in range(3)] for _ in range(n)]
    floor = rng.randrange(n)  # a floor tile's normal: (0, 0, +-z)
    pool[floor] = [0, 0, rng.choice((-1, 1)) * rng.randrange(1, span)]
    faces, lists = [], []
    for _ in range(rng.randrange(1, 12)):
        kind = rng.choice((0, 1, 1, 1, 2, 3))
        flags = kind | rng.choice((0, 0x80, 0x80, 0xA0, 0x90, 0xB0, rng.randrange(256) & 0xFC))
        k = rng.randrange(3, min(8, n + 1))
        verts = (
            rng.sample(range(n), k) if rng.random() < 0.8 else [rng.randrange(n) for _ in range(k)]
        )
        nrm = floor if rng.random() < 0.3 else rng.randrange(n)
        faces.append([flags, *rng.randbytes(4), nrm])
        lists.append(bytes(verts) + b"\xff")
    header_off = 26
    faces_off = header_off + 4
    lists_off = faces_off + 8 * len(faces)
    blob = bytearray(rng.randbytes(26))
    # a face count the DEC/JGE loop ends on at once now and then: none, -1, -8000h
    count = len(faces) if rng.random() < 0.95 else rng.choice((0, 0xFFFF, 0x8000))
    blob += words(count, faces_off)
    at = lists_off
    for face, vl in zip(faces, lists):
        blob += bytes(face) + words(at)
        at += len(vl)
    for vl in lists:
        blob += vl
    pool_off = len(blob)
    for v in pool:
        blob += words(*v)
    return bytes(blob), header_off, pool_off, pool


def _case(pair: MeshPair, rng: random.Random, producer: int) -> bool:
    m, native = pair.m, pair.n
    blob, header, pool_off, pool = _mesh(rng, producer)
    n = len(pool)
    dist = rng.choice(VIEW_DISTS)
    shift = {0x80: 7, 0x100: 8, 0x200: 9}[dist]

    state = bytearray(rng.randbytes(STATE_SIZE))
    state[ST_POOL : ST_POOL + 4] = words(pool_off, n)
    mesh_shift = rng.choice((0, 0, 1, 2, 3))
    state[ST_SHIFT : ST_SHIFT + 4] = words(mesh_shift, rng.choice((0, 0, -1, -2, -mesh_shift)))
    if producer == 3:
        state[ST_SHIFT : ST_SHIFT + 2] = words(rng.choice((0, 1, 2, -1, -2, 0x80, 0x7F)))
    state[ST_LOD] = rng.choice((1, 2)) if producer == 1 else producer
    state[ST_VIEWCLIP] = rng.choice((0, 1))
    state[ST_ACTORROT : ST_ACTORROT + 19] = rotation_bytes(rng)
    depth = rng.choice(
        (rng.randrange(-200, dist), rng.randrange(dist, 4 * dist), rng.randrange(0, 0x3000))
    )
    far = 0x40000 if producer == 1 else 0x1000
    cam = (rng.randrange(-far, far), depth, rng.randrange(-far, far))
    state[ST_CAM : ST_CAM + 12] = struct.pack("<3i", cam[0], cam[1], cam[2])  # X, Z, Y
    if rng.random() < 0.3:
        # the identity, and a vertex's depth on a boundary: the view plane, 0, either side
        state[ST_ACTORROT : ST_ACTORROT + 19] = words(
            0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000
        ) + bytes(1)
        y = rng.choice(pool)[1]
        if producer == 1:
            y <<= mesh_shift
        target = rng.choice((dist, dist - 1, dist + 1, 0, -1, -2))
        state[ST_CAM + 4 : ST_CAM + 8] = struct.pack("<i", target - y)
    state[ST_CAMORIGIN : ST_CAMORIGIN + 6] = words(
        *(rng.randrange(-0x400, 0x400) for _ in range(3))
    )
    state[ST_LIGHT : ST_LIGHT + 6] = words(*(rng.randrange(-0x200, 0x200) for _ in range(3)))
    state[ST_DIST : ST_DIST + 2] = words(dist)
    state[ST_SCREENSHIFT : ST_SCREENSHIFT + 2] = bytes((shift, 0))
    state[ST_CENTER : ST_CENTER + 4] = words(160, 71)
    hit = rng.random() < 0.3
    scratch = rng.randbytes(24)
    matout32 = rng.randbytes(12)
    counters = rng.randbytes(8)
    day = rng.choice((0, 1))
    mode = rng.choice((1, 2, 2))
    textured = rng.choice((0, 1))
    remap = rng.randbytes(256)
    has_remap = rng.random() < 0.3
    lo, hi = rng.randbytes(LO_SIZE), rng.randbytes(HI_SIZE)
    left = rng.randbytes(3 + 2 + 80)  # the context's fill flag, colours, corners from before

    # the original
    m.free_all()
    seg = m.alloc(len(blob))
    m.write((seg, 0), blob)
    original = bytearray(state)
    original[STATE_PTRS : STATE_PTRS + 4] = words(0, seg)
    if hit:
        original[0:4] = words(pool_off, seg)
    m.write("g_abR3dState", bytes(original))
    m.write(pair.lo_at, lo)
    m.write(pair.hi_at, hi)
    m.write("g_matXIn", scratch[:18])
    m.write(pair.light_at, scratch[18:])
    m.write("g_matXOut32", matout32)
    k = 0
    for s, o, size in pair.words_at:
        m.write((s, o), counters[k : k + size])
        k += size
    m.write((pair.ctx[0], pair.ctx[1] + pair.flag_off), bytes((day,)))
    m.write((pair.ctx[0], pair.ctx[1] + 0x0C), left[:3])
    m.write((pair.ctx[0], pair.ctx[1] + pair.count_off), left[3:5])
    m.write((pair.ctx[0], pair.ctx[1] + pair.px_off), left[5:45])
    m.write((pair.ctx[0], pair.ctx[1] + pair.py_off), left[45:])
    m.write(pair.clip_at, struct.pack("<4H", *VIEW_RECT))
    m.write("g_game_mode", words(mode))
    m.write("g_bTexturedPolyEnabled", words(textured))
    m.write((m.data_seg, REMAP_AT), remap)
    m.write("g_pColorRemapTable", words(REMAP_AT if has_remap else 0))
    m.write("g_pFaceMaterialTable", words(0))
    pair.log = []
    try:
        m.call_far(PRODUCERS[producer], header, seg)
    except MachineError as e:
        if "IDIV by zero" in str(e):
            return False
        raise
    want = bytearray(m.read("g_abR3dState", STATE_SIZE))
    want[STATE_PTRS:STATE_REST] = bytes(STATE_REST - STATE_PTRS)
    want[2:4] = bytes(2)
    want += m.read("g_matXIn", 18) + m.read(pair.light_at, 6) + m.read("g_matXOut32", 12)
    want += pair.read_words()
    c = m.read(pair.ctx, 0x6F2)
    want += c[0x0C:0x0F] + c[pair.count_off : pair.count_off + 2]
    want += c[pair.px_off : pair.px_off + 40] + c[pair.py_off : pair.py_off + 40]
    want += words(len(pair.log)) + b"".join(pair.log)

    # the translation
    native.poke("r3dclip.lo", lo)
    native.poke("r3dclip.hi", hi)
    payload = (
        bytes((producer, hit))
        + bytes(state)
        + scratch
        + matout32
        + counters
        + bytes((day,))
        + words(mode, textured, *VIEW_RECT)
        + bytes((has_remap,))
        + remap
        + left
        + words(header, len(blob))
        + blob
    )
    got = native.call("r3dmesh.run", payload)
    what = f"{PRODUCERS[producer]} (lod {state[ST_LOD]}, {len(pair.log)} polygons)"
    if got != bytes(want):
        at = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), None)
        raise AssertionError(
            f"{what}: reply differs at byte {at} (lengths {len(got)}, {len(want)})"
        )
    assert native.peek("r3dclip.lo") == m.read(pair.lo_at, LO_SIZE), f"{what}: low block"
    assert native.peek("r3dclip.hi") == m.read(pair.hi_at, HI_SIZE), f"{what}: high block"
    return True


def _run(krondor, fresh_native, producer: int, seed: int, cases: int) -> None:
    pair = MeshPair(krondor, fresh_native)
    rng = random.Random(seed)
    done = drawn = 0
    while done < cases:
        if _case(pair, rng, producer):
            done += 1
            drawn += len(pair.log)
    assert drawn > cases  # the cases draw something


def test_mesh_lod0(krondor, fresh_native):
    _run(krondor, fresh_native, 0, 21, 400)


def test_mesh_lod12(krondor, fresh_native):
    _run(krondor, fresh_native, 1, 22, 400)


def test_mesh_lod3(krondor, fresh_native):
    _run(krondor, fresh_native, 3, 23, 400)


def test_emit_screen_or_world(krondor, fresh_native):
    # A floor tile's corners after the near clip: behind ones (state 3) give their camera x
    # and z, kept when their depth is 0 or more; no corner kept drops the polygon.
    pair = MeshPair(krondor, fresh_native)
    m, native = pair.m, pair.n
    rng = random.Random(24)
    for i in range(1500):
        lo, hi = bytearray(rng.randbytes(LO_SIZE)), rng.randbytes(HI_SIZE)
        n = rng.choice((0, 1, 2, 3, 4, rng.randrange(21)))
        struct.pack_into("<H", lo, COUNT, n)
        for k in range(n):
            v = rng.randrange(255)
            struct.pack_into("<H", lo, POLY_VERTICES + 2 * k, 2 * v)
            lo[CLIP_STATE + 2 * v] = rng.choice((3, 3, 2, rng.randrange(256)))
            depth = rng.choice((0, -1, 1, -0x8000, rng.randrange(-0x8000, 0x8000)))
            if i % 2:
                depth = rng.choice((0, -1, -2, -0x8000))  # behind the camera mostly
            struct.pack_into("<h", lo, CAM_Y + 2 * v, depth)
        m.write(pair.lo_at, bytes(lo))
        m.write(pair.hi_at, hi)
        m.call_near("r3d_poly_emit_screen_or_world", ds=pair.seg)
        native.poke("r3dclip.lo", bytes(lo))
        native.poke("r3dclip.hi", hi)
        native.call("r3dmesh.emit")
        assert native.peek("r3dclip.lo") == m.read(pair.lo_at, LO_SIZE), i
        assert native.peek("r3dclip.hi") == m.read(pair.hi_at, HI_SIZE), i
