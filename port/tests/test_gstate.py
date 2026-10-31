"""GAME/STATE/GSTATE.C's native serializer against the original's TEMP.GAM layout.

TEMP.GAM (and every save, a copy of it) keeps the 16-bit layout, where a near pointer
takes 2 bytes. GameState, Combatant and CombatantState hold near pointers, so natively
they are converted field by field. The offsets below are the original's, as its code
addresses them (the reference build's objects: `imul 0x5f` + 90E7h for the combatant
pool, `actor_record` at g_gameState + D0h, and so on) and as STARTUP.GAM lays them out.
Each named field of the native struct must hold the bytes from its offset, and packing
must give the record back with only the pointers cleared.
"""

from __future__ import annotations

import random
import struct

CHARACTERS, STRIDE = 0x77, 0x5F  # GameState.characters[6]: Combatant records
SPELLS, STATS, CHAR_SLOT, ACTOR_RECORD, INNER = 0x02, 0x08, 0x58, 0x59, 0x5D
NAMES = 0x3B            # characterNames[6][10]
PARTY_SIZE = 0x2B1      # partySize, then activeParty[3]
SHARED_INVENTORY, GROUND_PILE = 0x2B5, 0x2B9  # far pointers
PARTY_DIRTY = 0x2BD     # bPartyDirtyFlags
SKILL_TRAIN_RATE = 0x2C0
GAMESTATE, COMBATANT, STATE = 0xAD7, 0x5F, 0x16


def _cleared(data: bytes, spans: list[tuple[int, int]]) -> bytes:
    out = bytearray(data)
    for at, n in spans:
        out[at:at + n] = bytes(n)
    return bytes(out)


def _combatant_spans(base: int) -> list[tuple[int, int]]:
    return [(base, 2), (base + ACTOR_RECORD, 4), (base + INNER, 2)]


class Reader:
    def __init__(self, data: bytes):
        self.data, self.pos = data, 0

    def take(self, n: int) -> bytes:
        self.pos += n
        return self.data[self.pos - n:self.pos]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]


def _check_combatant(r: Reader, disk: bytes, base: int, what: str) -> None:
    assert r.take(6) == disk[base + SPELLS:base + SPELLS + 6], f"{what}: spellsKnown"
    assert r.take(0x50) == disk[base + STATS:base + STATS + 0x50], f"{what}: stats"
    assert r.take(1) == disk[base + CHAR_SLOT:base + CHAR_SLOT + 1], f"{what}: charSlot"
    assert (r.u32(), r.u32(), r.u32()) == (0, 0, 0), f"{what}: pointers not NULL"


def test_gamestate(native):
    rng = random.Random(1)
    for i in range(50):
        disk = rng.randbytes(GAMESTATE)
        r = Reader(native.call("gstate.gamestate", disk))
        assert r.take(2) == disk[0:2], f"case {i}: nChapter"
        assert r.take(4) == disk[2:6], f"case {i}: nParty_gold"
        assert r.take(60) == disk[NAMES:NAMES + 60], f"case {i}: characterNames"
        for k in range(6):
            _check_combatant(r, disk, CHARACTERS + k * STRIDE, f"case {i}: character {k}")
        assert r.take(4) == disk[PARTY_SIZE:PARTY_SIZE + 4], f"case {i}: partySize, activeParty"
        assert (r.u32(), r.u32()) == (0, 0), f"case {i}: inventory pointers not NULL"
        assert r.take(1) == disk[PARTY_DIRTY:PARTY_DIRTY + 1], f"case {i}: bPartyDirtyFlags"
        assert r.take(12) == disk[SKILL_TRAIN_RATE:SKILL_TRAIN_RATE + 12], f"case {i}: train rate"
        assert r.take(1) == disk[GAMESTATE - 1:], f"case {i}: event_bitmap_hi"
        spans = [(SHARED_INVENTORY, 4), (GROUND_PILE, 4)]
        for k in range(6):
            spans += _combatant_spans(CHARACTERS + k * STRIDE)
        assert r.take(GAMESTATE) == _cleared(disk, spans), f"case {i}: packed again"


def test_combatant(native):
    rng = random.Random(2)
    for i in range(100):
        disk = rng.randbytes(COMBATANT)
        r = Reader(native.call("gstate.combatant", disk))
        _check_combatant(r, disk, 0, f"case {i}")
        assert r.take(COMBATANT) == _cleared(disk, _combatant_spans(0)), f"case {i}: packed again"


def test_combatant_state(native):
    rng = random.Random(3)
    for i in range(100):
        disk = rng.randbytes(STATE)
        r = Reader(native.call("gstate.state", disk))
        assert r.u32() == 0, f"case {i}: target not NULL"
        assert r.take(2) == disk[2:4], f"case {i}: creatureType"
        assert r.take(1) == disk[8:9], f"case {i}: flags"
        assert r.take(2) == disk[0xA:0xC], f"case {i}: statusHead"
        assert r.take(1) == disk[0x15:0x16], f"case {i}: dmgFloatFrames"
        assert r.take(STATE) == _cleared(disk, [(0, 2)]), f"case {i}: packed again"
