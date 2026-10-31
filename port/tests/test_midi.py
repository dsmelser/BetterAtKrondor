"""AUDIO/DRIVER/MIDI.ASM (the sequencer) and SNDDRV.ASM against port/asm/midi.c, snddrv.c.

Both sides drive fake drivers that log every call and answer alike: the synth driver
(slot 0 and 1 as each test sets them, slot 7 masks CL to 7 bits, slots 11-13 return
slot * 111h + CL) and, behind SNDDRV.ASM, a digital driver (command 4 answers from a
script, the others 4400h + command). Tracks play generated scripts: streams of every
event kind, running status, loop points, meta events, long waits. After each operation
the driver calls (the registers each slot reads), the sequencer's state block (pointers
mapped between the two address spaces), every track record and every script must match.

Each track and its script share one block, at the same offsets on both sides: the
digital stream's start reads two bytes at the track's segment with the stream's offset.
"""

from __future__ import annotations

import random
import struct

import pytest
from unicorn import UC_HOOK_CODE

TRACK_SIZE = 0x17A
SCRIPT_AT = 0x200
BLOCK_SIZE = 0x1800
CS_FIRST, CS_END = 0x1C2, 0x3C7
QUEUE_OFF = 0x3C7  # _midi_queue_command in the ASM's offsets
POINTER_FIELDS = (range(0x08, 0x0C), range(0x16A, 0x16E))

# Which registers each synth slot reads (and the original passes).
SLOT_REGS = {1: "bank", 4: "al ch cl", 5: "al ch cl", 6: "al ch cl", 7: "al ch cl",
             8: "al cl", 9: "al cl", 10: "al ch cl", 11: "cl", 12: "cl", 13: "cl"}

# midi.op numbers (t_midi.c)
INSTALL, BANK, INIT, RELEASE, HOLD, FADE, INTENSITY, PRIORITY, CUE, BEND, CTL, GLOBAL, \
    VOLUME, ENABLE, RESET, MUTES, SEEK, MARKS, CLOCK, STATE, TICK, PATCH = range(22)

THUNKS = {
    INSTALL: "_midi_driver_install_dispatch", BANK: "_midi_cmd1_save_cx_303b4_thunk",
    INIT: "_midi_track_init_thunk", RELEASE: "_midi_active_release_thunk",
    HOLD: "_midi_event_callback_thunk_0a8b", FADE: "_midi_fade_queue_handle",
    INTENSITY: "_midi_track_apply_intensity", PRIORITY: "_midi_chord_event_thunk_0b6e",
    CUE: "_midi_track_set_byte_15a", BEND: "_midi_track_set_word_bc_far",
    CTL: "_midi_apply_ctrl_change_thunk", GLOBAL: "_midi_set_note_velocity_thunk",
    VOLUME: "_midi_cmd6_far_wrapper", ENABLE: "_midi_cmd4_far_wrapper",
    RESET: "_midi_init_thunk_0678", MUTES: "_midi_pitch_bend_step_far_thunk",
    SEEK: "_midi_seek_volume_preserve_thunk", MARKS: "_midi_stub_return_cx_via_nop_alt",
    CLOCK: "_midi_stub_return_cx_via_nop", STATE: "_midi_track_read_byte_158_far",
    TICK: "_midi_task_scheduler_tick", PATCH: "_snddrv_patch_farptr",
}
NO_POINTER = {GLOBAL, VOLUME, ENABLE, RESET, TICK}


def dude_header(version: int, name: bytes) -> bytes:
    tag = b"dude" if version == 1 else b"difftag!"
    return b"\xE9\x00\x00\x00" + struct.pack("<I", 0x87654321) + bytes([version, 0]) + tag \
        + bytes([len(name)]) + name


class Block:
    def __init__(self, useg: int, naddr: int, size: int):
        self.useg, self.naddr, self.size = useg, naddr, size


class Sequencer:
    """The original in Unicorn and the translation, kept in step."""

    def __init__(self, m, native, info=(0x0101, 0x0C20), bank=(0x0961, 0x0801), replies=()):
        self.m, self.n = m, native
        seg, off = m.sym("_midi_queue_command")
        self.cs_seg, self.cs_base = seg, off - QUEUE_OFF
        self.blocks: list[Block] = []
        self.info, self.bank_answer = info, bank
        self.replies = list(replies)
        self.ulog: list[tuple] = []
        native.call("midi.setup", struct.pack("<5H", *info, *bank, len(self.replies))
                    + struct.pack(f"<{len(self.replies)}H", *self.replies))
        self.synth = self.alloc(64, dude_header(1, b"difftest"))
        self.digital = self.alloc(64, dude_header(2, b"difftest"))
        m.write((self.synth.useg, 0), b"\xCB")  # RETF
        m.write((self.digital.useg, 0), b"\xCB")
        for block, hook in ((self.synth, self._synth_hook), (self.digital, self._digital_hook)):
            lin = m.linear(block.useg, 0)
            m.uc.hook_add(UC_HOOK_CODE, hook, None, lin, lin)
        m.port_out[0x20] = lambda m, port, size, value: None  # EOI

    # --- memory -----------------------------------------------------------------------

    def alloc(self, size: int, data: bytes = b"") -> Block:
        useg = self.m.alloc(size)
        (naddr,) = struct.unpack("<I", self.n.call("midi.alloc", struct.pack("<I", size)))
        block = Block(useg, naddr, size)
        self.blocks.append(block)
        if data:
            self.write(block, 0, data)
        return block

    def write(self, block: Block, off: int, data: bytes) -> None:
        self.m.write((block.useg, off), data)
        self.n.call("midi.write", struct.pack("<I", block.naddr + off) + data)

    def far(self, block: Block, off: int = 0) -> tuple[int, int]:
        return off, block.useg

    def u2n(self, off: int, seg: int) -> int:
        if off == 0 and seg == 0:
            return 0
        lin = (seg << 4) + off
        for b in self.blocks:
            if b.useg << 4 <= lin < (b.useg << 4) + b.size:
                return b.naddr + lin - (b.useg << 4)
        return -1

    # --- fake drivers (the original's side) ---------------------------------------------

    def _synth_hook(self, uc, address, size, user):
        m = self.m
        slot, ax, cx = m.reg("bp"), m.reg("ax"), m.reg("cx")
        self.ulog.append(("S", slot, ax, cx, self.u2n(ax, m.reg("es")) if slot == 1 else 0))
        if slot == 0:
            ax, cx = self.info
        elif slot == 1:
            ax, cx = self.bank_answer
        elif slot == 7:
            cx &= 0xFF7F
        elif slot in (11, 12, 13):
            ax = (slot * 0x111 + (cx & 0xFF)) & 0xFFFF
        m.set_reg("ax", ax)
        m.set_reg("cx", cx)

    def _digital_hook(self, uc, address, size, user):
        m = self.m
        cmd, si, ss = m.reg("ax"), m.reg("si"), m.reg("ss")
        block = b""
        if cmd == 0:
            block = m.read((ss, si), 4)
        elif cmd == 3:
            raw = m.read((ss, si), 10)
            off, seg = struct.unpack_from("<HH", raw, 4)
            block = raw[:4] + struct.pack("<I", self.u2n(off, seg) & 0xFFFFFFFF) + raw[8:]
        elif cmd == 4:
            block = m.read((ss, si), 2)
        self.ulog.append(("D", cmd, block))
        if cmd == 4:
            reply = self.replies.pop(0) if self.replies else 0
        else:
            reply = 0x4400 | cmd
        m.set_reg("ax", reply)

    # --- calls ----------------------------------------------------------------------------

    def op(self, op: int, block: Block | None = None, *args: int) -> int:
        words = list(args) + [0] * (3 - len(args))
        if op in NO_POINTER:
            r = self.m.call_far(THUNKS[op], *args)
        else:
            r = self.m.call_far(THUNKS[op], *self.far(block), *args)
        naddr = block.naddr if block is not None else 0
        (got,) = struct.unpack("<H", self.n.call(
            "midi.op", struct.pack("<BI3H", op, naddr, *(w & 0xFFFF for w in words))))
        return r.ax, got

    def queue(self, kind: int, handle: tuple[int, int, int], slots: tuple[int, ...]) -> None:
        """handle: (arg2, arg3) for the original and the u32 for the native side."""
        uarg2, uarg3, nbits = handle
        self.m.call_far("_midi_queue_command", kind, uarg2, uarg3, *slots)
        self.n.call("midi.queue", struct.pack("<B8H", kind, nbits & 0xFFFF, nbits >> 16,
                                               *(s & 0xFFFF for s in slots)))

    def native_log(self) -> list[tuple]:
        raw, out, i = self.n.call("midi.log"), [], 0
        while i < len(raw):
            if raw[i:i + 1] == b"S":
                slot, ax, cx, bank = struct.unpack_from("<HHHI", raw, i + 1)
                out.append(("S", slot, ax, cx, bank))
                i += 11
            else:
                (cmd,) = struct.unpack_from("<H", raw, i + 1)
                n = {0: 4, 3: 10, 4: 2}.get(cmd, 0)
                out.append(("D", cmd, raw[i + 7:i + 7 + n]))
                i += 7 + n
        return out

    @staticmethod
    def project(entry: tuple) -> tuple:
        if entry[0] == "D":
            return entry
        _, slot, ax, cx, bank = entry
        regs = SLOT_REGS.get(slot, "")
        return (slot,
                ax & 0xFF if "al" in regs else None,
                cx >> 8 if "ch" in regs else None,
                cx & 0xFF if "cl" in regs else None,
                bank if "bank" in regs else None)

    def check(self, what: str) -> None:
        got, self.ulog = [self.project(e) for e in self.ulog], []
        want = [self.project(e) for e in self.native_log()]
        assert want == got, f"{what}: driver calls"
        u = self.m.read((self.cs_seg, self.cs_base + CS_FIRST), CS_END - CS_FIRST)
        n = self.n.peek("g_midiData")
        for name, at, count in (("registry", 0x1C2, 16), ("streams", 0x202, 16),
                                ("voiceTrack", 0x242, 32)):
            for i in range(count):
                off, seg = struct.unpack_from("<HH", u, at - CS_FIRST + 4 * i)
                (nat,) = struct.unpack_from("<I", n, at - CS_FIRST + 4 * i)
                assert self.u2n(off, seg) == nat, f"{what}: {name}[{i}]"
        plain = slice(0x2C2 - CS_FIRST, 0x3A1 - CS_FIRST)
        assert n[plain] == u[plain], f"{what}: voice tables"
        globals_ = slice(0x3B3 - CS_FIRST, None)
        assert n[globals_] == u[globals_], f"{what}: globals"
        for b in self.blocks[2:]:
            ub = bytearray(self.m.read((b.useg, 0), b.size))
            nb = bytearray(self.n.call("midi.read", struct.pack("<II", b.naddr, b.size)))
            for r in POINTER_FIELDS:
                ub[r.start:r.stop] = nb[r.start:r.stop] = bytes(len(r))
            if nb != ub:
                diff = next(i for i in range(b.size) if nb[i] != ub[i])
                raise AssertionError(f"{what}: block {self.blocks.index(b)} differs at "
                                     f"{diff:#x}: native {nb[diff]:#04x}, original {ub[diff]:#04x}")

    # --- setup ----------------------------------------------------------------------------

    def install(self, bank: Block) -> None:
        ax_u, ax_n = self.op(INSTALL, self.synth)
        assert ax_n == ax_u & 0xFF
        self.check("install")
        ax_u, ax_n = self.op(BANK, bank)
        assert ax_n == ax_u
        self.check("bank")

    def track(self, script: bytes, priority: int = 1, master: int = 0x7F, loop: int = 0) -> Block:
        """A stopped track, as the game's sound-effect slots are, and its script."""
        b = self.alloc(BLOCK_SIZE)
        rec = bytearray(TRACK_SIZE)
        rec[0x158] = 0xFF
        rec[0x15C], rec[0x15D], rec[0x15E] = priority, loop, master
        self.m.write((b.useg, 0), bytes(rec))
        self.m.write((b.useg, 0x08), struct.pack("<HH", 0x16A, b.useg))
        self.m.write((b.useg, 0x16A), struct.pack("<HH", SCRIPT_AT, b.useg))
        self.m.write((b.useg, SCRIPT_AT), script)
        rec[0x08:0x0C] = struct.pack("<I", b.naddr + 0x16A)
        rec[0x16A:0x16E] = struct.pack("<I", b.naddr + SCRIPT_AT)
        self.n.call("midi.write", struct.pack("<I", b.naddr) + bytes(rec))
        self.n.call("midi.write", struct.pack("<I", b.naddr + SCRIPT_AT) + script)
        return b


# --- scripts ------------------------------------------------------------------------------

def stream_events(rng: random.Random, channel: int, count: int, loop_point: bool) -> bytes:
    out = bytearray()
    last = None
    for i in range(count):
        if loop_point and i == 1:
            out += bytes([0xCF, 0x7F, rng.choice((0, 1, 2, 4))])  # its wait byte is read again
            last = None
            continue
        c = rng.choice((channel & 0x0F, channel & 0x0F, rng.randrange(15)))
        kind = rng.choices(("on", "off", "cc", "prog", "bend", "poly", "press", "sysex", "meta"),
                           (10, 8, 8, 2, 3, 1, 1, 1, 3))[0]
        if kind == "on":
            ev = [0x90 | c, rng.randrange(0x80), rng.choice((0, 0x40, 0x7F, rng.randrange(0x80)))]
        elif kind == "off":
            ev = [0x80 | c, rng.randrange(0x80), rng.randrange(0x80)]
        elif kind == "cc":
            ctl = rng.choice((0x07, 0x0A, 0x01, 0x40, 0x4B, 0x4E, 0x5B, 0x07))
            val = rng.randrange(0x10) if ctl == 0x4B else rng.choice((0, 1, rng.randrange(0x80)))
            ev = [0xB0 | c, ctl, val]
        elif kind == "prog":
            ev = [0xC0 | c, rng.randrange(0x80)]
        elif kind == "bend":
            ev = [0xE0 | c, rng.randrange(0x80), rng.randrange(0x80)]
        elif kind == "poly":
            ev = [0xA0 | c, rng.randrange(0x80), rng.randrange(0x80)]
        elif kind == "press":
            ev = [0xD0 | c, rng.randrange(0x80)]
        elif kind == "sysex":
            ev = [0xF0] + [rng.randrange(0x80) for _ in range(rng.randrange(4))] + [0xF7]
        else:
            ev = rng.choice((
                [0xCF, rng.choice((1, 2, 0x10, 0x55))],
                [0xBF, 0x50, rng.choice((0x7F, 0, 5, 10))],
                [0xBF, 0x60, rng.randrange(0x80)],
                [0xBF, 0x52, rng.choice((0, 1, 2))],
                [0xEF, 1, 2], [0xDF, 3], [0x8F, 4, 5],
                [0xFF, 1, 2, 0xF7],
            ))
        if ev[0] == last and ev[0] < 0xF0 and (ev[0] & 0x0F) != 0x0F:
            ev = ev[1:]  # running status
        last = ev[0] if ev[0] < 0xF0 and (ev[0] & 0x0F) != 0x0F else None
        out += bytes(ev)
        out.append(rng.choices((0, 1, 2, 3, 6, 0xF8, rng.randrange(1, 0x30)), (6, 4, 3, 2, 1, 1, 2))[0])
    out.append(0xFC)
    return bytes(out)


def make_script(rng: random.Random, device: int, digital: bool = False,
                parsed: bool | None = None) -> bytes:
    """Streams: a 13-byte header [channel and flags, voice cost/priority, -, -, program, -,
    -, -, volume, -, -, pan, first wait] then events; a fixed-voice stream (10h) has its
    events from offset 3; a digital one starts FEh."""
    streams = []
    for s in range(rng.randrange(1, 9)):
        channel = rng.choice((1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 15, rng.randrange(16)))
        flags = rng.choices((0, 0x20, 0x40, 0x10), (6, 3, 1, 1))[0]
        loop = rng.random() < 0.4
        events = stream_events(rng, channel, rng.randrange(2, 30), loop)
        if flags == 0x10:
            streams.append(bytes([channel | flags, 0, 0]) + events)
        else:
            header = bytes([channel | flags, rng.choice((0x01, 0x02, 0x11, 0x23, 0x04, 0x31)),
                            0, 0, rng.randrange(0x80), 0, 0, 0, rng.choice((0x7F, 0x40, 0x64)),
                            0, 0, rng.randrange(0x80), rng.choice((0, 1, 3, 0xF8))])
            streams.append(header + events)
    if digital:
        streams.append(bytes([0xFE, rng.choice((0xFE, 0x01))]) + bytes(rng.randrange(256) for _ in range(14)))
    if parsed is None:
        parsed = rng.random() < 0.5
    body = bytearray()
    offsets = []
    start = 0x24 if parsed else 0x80
    for st in streams:
        offsets.append(start + len(body))
        body += st
    if parsed:
        head = b"".join(struct.pack("<H", o) for o in offsets) + bytes(2 * (16 - len(offsets)))
        head += bytes([rng.choice((0xFF, 1, 3, 7))]) + b"\xFC\xFD\xFE"
    else:
        head = bytearray()
        if rng.random() < 0.5:
            head += bytes([0xF0, rng.choice((1, 4, 9))]) + bytes(6)
        other = rng.choice((0x01, 0x07, 0x0A))
        head += bytes([other, 0, 0, 0x30, 0x00, 0, 0, 0xFF])  # another device's row
        head += bytes([device])
        for o in offsets:
            head += bytes([0, 0]) + struct.pack("<H", o) + bytes(2)
        head += b"\xFF\xFF"
        assert len(head) <= start
        head += bytes(start - len(head))
    return bytes(head) + bytes(body)


def make_bank() -> bytes:
    return bytes(range(256)) * 2


# --- tests ---------------------------------------------------------------------------------

@pytest.fixture
def seq(krondor, fresh_native):
    s = Sequencer(krondor, fresh_native)
    s.install(s.alloc(512, make_bank()))
    return s


def test_install(krondor, fresh_native):
    s = Sequencer(krondor, fresh_native, info=(0x1203, 0x0910), bank=(0xFFFF, 0x0F00))
    s.install(s.alloc(512, make_bank()))


def random_session(s: Sequencer, rng: random.Random, tracks: list[Block], steps: int) -> None:
    for step in range(steps):
        t = rng.choice(tracks)
        op = rng.choices(
            (TICK, INIT, RELEASE, HOLD, FADE, INTENSITY, PRIORITY, CUE, BEND, CTL, GLOBAL,
             VOLUME, ENABLE, MUTES, MARKS, CLOCK, STATE, RESET),
            (60, 6, 2, 2, 3, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 0.1))[0]
        if op == TICK:
            args = ()
        elif op == INIT:
            args = (rng.choice((0, 1)),)
        elif op == HOLD:
            args = (rng.choice((0, 1)),)
            if rng.random() < 0.3:
                ur, nr = s.m.call_far(THUNKS[HOLD], 0, 0, *args), s.n.call(
                    "midi.op", struct.pack("<BI3H", HOLD, 0, args[0], 0, 0))
                s.check(f"step {step}: hold all")
                continue
        elif op == FADE:
            args = (rng.choice((0, 0x20, 0x7F, 0x80, 0x90, rng.randrange(256))),
                    rng.choice((0, 1, 3)), rng.choice((1, 4, 0x10, 0x7F)))
        elif op in (INTENSITY, PRIORITY, CUE, GLOBAL, VOLUME, ENABLE):
            args = (rng.choice((0, 1, 2, 5, 0x0A, 0x0B, 0x0F, 0x10, 0x40, 0x7F, 0xFF, rng.randrange(256))),)
        elif op == BEND:
            args = (rng.randrange(0x10000), rng.randrange(15))
        elif op == CTL:
            args = (rng.choice((0x07, 0x0A, 0x01, 0x40, 0x4E, 0x7F, 0x5B)),
                    rng.choice((0, 1, rng.randrange(0x80))), rng.randrange(15))
        elif op == MUTES:
            args = (rng.choice((0, 1)),)
        else:
            args = ()
        ur, nr = s.op(op, None if op in NO_POINTER else t, *args)
        if op in (MARKS, CLOCK, STATE):
            assert nr == (ur & 0xFF if op == STATE else ur), f"step {step}: op {op} result"
        s.check(f"step {step}: op {op} {args}")


@pytest.mark.parametrize("seed", range(6))
def test_random_sessions(krondor, fresh_native, seed):
    rng = random.Random(seed)
    info = (0x0101, rng.choice((0x0C20, 0x0C10, 0x0C08)))
    bank = rng.choice(((0x0961, 0x0801), (0x2446, 0x0800), (0x0F71, 0x0F01)))
    s = Sequencer(krondor, fresh_native, info=info, bank=bank)
    s.install(s.alloc(512, make_bank()))
    tracks = [s.track(make_script(rng, 0x0C), priority=rng.randrange(4), loop=rng.choice((0, 1)))
              for _ in range(10)]
    random_session(s, rng, tracks, 2500)


@pytest.mark.parametrize("seed", range(3))
def test_digital_streams(krondor, fresh_native, seed):
    """Tracks with a stream for the digital driver: with one (replies script the polls)
    and without, a state the game never reaches (every synth driver reports no digital
    capability), where a poll answers DGROUP's segment: the clock restarts and the stream
    ends, on both sides alike as long as neither of its bytes is 0."""
    rng = random.Random(100 + seed)
    replies = [rng.choice((0, 0, 0, 0x0100, 0x0001, 0x0101)) for _ in range(200)]
    s = Sequencer(krondor, fresh_native, info=(0x1101, 0x0C20), replies=replies)
    s.install(s.alloc(512, make_bank()))
    if seed != 0:
        s.m.set_u16("_g_bMusicDriverInstalled", 1)
        s.n.poke("g_bMusicDriverInstalled", struct.pack("<H", 1))
        s.op(PATCH, s.digital)
        s.check("patch")
    tracks = [s.track(make_script(rng, 0x0C, digital=rng.random() < 0.6), loop=rng.choice((0, 1)))
              for _ in range(6)]
    random_session(s, rng, tracks, 1200)


def test_digital_streams_skipped(krondor, fresh_native):
    """The MT-32 setup: no digital driver and no digital capability, so a stream for the
    digital driver is skipped and its track plays the rest."""
    rng = random.Random(200)
    s = Sequencer(krondor, fresh_native, info=(0x0101, 0x0C20))
    s.install(s.alloc(512, make_bank()))
    tracks = [s.track(make_script(rng, 0x0C, digital=True), loop=rng.choice((0, 1)))
              for _ in range(5)]
    random_session(s, rng, tracks, 600)


def test_full_registry(krondor, fresh_native):
    """Seventeen tracks for sixteen slots; the last slot's track released and given a new
    priority; controller 4Eh's count raised and lowered; a resume at no pause."""
    rng = random.Random(300)
    s = Sequencer(krondor, fresh_native, info=(0x0101, 0x0C40), bank=(0x0961, 0x0F00))
    s.install(s.alloc(512, make_bank()))
    tracks = [s.track(make_script(rng, 0x0C, parsed=True), priority=rng.randrange(1, 9))
              for _ in range(17)]
    for t in tracks:
        s.op(INIT, t, rng.choice((0, 1)))
        s.check("init")
    for _ in range(20):
        s.op(TICK)
    s.check("ticks")

    def registered(slot: int) -> Block:
        off, seg = struct.unpack("<HH", s.m.read((s.cs_seg, s.cs_base + 0x1C2 + 4 * slot), 4))
        return next(b for b in tracks if b.useg == seg)

    s.op(PRIORITY, registered(15), 0x30)
    s.check("priority of the last")
    s.op(RELEASE, registered(15))
    s.check("release of the last")
    for value in (1, 1, 0, 0, 0):
        s.op(CTL, registered(0), 0x4E, value, 2)
        s.check(f"controller 4Eh {value}")
    for kind, value in ((3, 1), (3, 1), (3, 0), (3, 1)):
        s.queue(kind, (value, 0, value), (value,) * 6)
        s.check("pause")
    for _ in range(20):
        s.op(TICK)
    s.check("end")


def test_queue_command(seq):
    """The command interface the game does not use, including the tests that run on
    after 0Fh, 11h and 12h."""
    s = seq
    rng = random.Random(7)
    tracks = [s.track(make_script(rng, 0x0C)) for _ in range(4)]
    results = s.alloc(16)

    def handle(b: Block) -> tuple[int, int, int]:
        return 0, b.useg, b.naddr

    def ptr_slots() -> tuple[int, ...]:
        n = results.naddr
        return (0, results.useg, 2, results.useg, 4, results.useg), (n & 0xFFFF, n >> 16, (n + 2) & 0xFFFF, (n + 2) >> 16, (n + 4) & 0xFFFF, (n + 4) >> 16)

    for t in tracks:
        s.queue(8, handle(t), (rng.choice((0, 1)), 0, 0, 0, 0, 0))
        s.check("queue 8")
    for _ in range(40):
        s.op(TICK)
    s.check("ticks")
    t = tracks[0]
    for kind, slots in ((3, (0, 0, 0, 0, 0, 0)), (3, (1, 0, 0, 0, 0, 0)), (6, (9,) * 6),
                        (7, (4,) * 6), (7, (0xFF,) * 6), (4, (1,) * 6), (0x0A, (1,) * 6),
                        (0x0A, (0,) * 6), (0x0B, (0x30, 2, 5, 0, 0, 0)), (0x0C, (3,) * 6),
                        (0x0D, (1,) * 6), (0x0E, (0x50,) * 6), (0x10, (0,) * 6),
                        (0x19, (0,) * 6), (0x13, (2, 0x40, 0x22, 0, 0, 0)),
                        (0x14, (2, 0x41, 0x33, 0, 0, 0)), (0x15, (3, 0x07, 0x50, 0, 0, 0)),
                        (0x15, (3, 0x4E, 1, 0, 0, 0)), (0x16, (4, 0x21, 0, 0, 0, 0)),
                        (0x17, (5, 0x2345, 0, 0, 0, 0)), (0x0F, (0x13, 0x41, 0x22, 0, 0, 0)),
                        (0x0F, (0x15, 0x07, 0x22, 0, 0, 0)), (0x0F, (2, 0, 0, 0, 0, 0)),
                        (0x09, (0,) * 6), (0x05, (0,) * 6), (0x02, (0,) * 6)):
            if kind in (3, 4, 6, 7):
                s.queue(kind, (slots[0], 0, slots[0]), slots)
            else:
                s.queue(kind, handle(t), slots)
            s.check(f"queue {kind:#x}")
            for _ in range(3):
                s.op(TICK)
            s.check(f"ticks after {kind:#x}")
    # 11h: then the tests run on T_MARKS's low byte (12h would write through the slots).
    marks = s.m.u16((t.useg, 0x152)) & 0xFF
    if marks != 0x12:
        s.queue(0x11, handle(t), (2, 0x40, 0x22, 0, 0, 0))
        s.check("queue 11h")
    # 12h: the clock's parts through three pointers, then the tests run on CL.
    clock = s.m.u16((t.useg, 0x154))
    if not 0x13 <= (clock % 3600 % 60) >> 1 <= 0x17:
        uslots, nslots = ptr_slots()
        s.m.call_far("_midi_queue_command", 0x12, 0, t.useg, *uslots)
        s.n.call("midi.queue", struct.pack("<B8H", 0x12, t.naddr & 0xFFFF, t.naddr >> 16, *nslots))
        s.check("queue 12h")
    # 0: install again, the three answers through pointers.
    uslots, nslots = ptr_slots()
    s.m.call_far("_midi_queue_command", 0, 0, s.synth.useg, *uslots)
    s.n.call("midi.queue", struct.pack("<B8H", 0, s.synth.naddr & 0xFFFF, s.synth.naddr >> 16, *nslots))
    s.check("queue 0")
    s.queue(1, (0, s.blocks[2].useg, s.blocks[2].naddr), (0,) * 6)
    s.check("queue 1")


def test_seek(seq):
    """Commands the game does not use: seek (a silent replay up to the track's clock),
    and the mute counter.

    A seek leaves the track's state at FFh (midi_track_init releases it first and, while
    seeking, does not reset the state). The scheduler then takes the registered track for
    one that stopped during its tick and plays it again, forever: the next tick never
    returns, in the original as in the translation. The test clears the state after
    each seek to go on."""
    s = seq
    rng = random.Random(11)
    tracks = [s.track(make_script(rng, 0x0C), loop=1) for _ in range(5)]
    for t in tracks:
        s.op(INIT, t, 1)
    for i in range(300):
        s.op(TICK)
        if i % 37 == 0:
            t = rng.choice(tracks)
            s.op(SEEK, t)
            s.check(f"seek {i}")
            assert s.m.u8((t.useg, 0x158)) == 0xFF
            s.write(t, 0x158, b"\x00")
            s.op(MUTES, rng.choice(tracks), rng.choice((0, 1)))
    s.check("end")
