"""COMBAT/SPELL/CSPELL.C's SPELLS.DAT reader against the file's 16-bit layout.

The original reads the records straight into its 22-byte SpellDef, whose first word is the
name's near pointer: the file holds the name's offset in the string blob there, and the loader
adds the blob's address. The ten words follow in the struct's order. Natively the name is a
4-byte pointer, so each field is read from its place: it must hold the bytes at that offset,
and the name must point at the offset into the blob (unsigned: the blob has no names before
it).
"""

from __future__ import annotations

import random
import struct

RECORD = 22
WORDS = (
    "nCost",
    "nCost_max",
    "nSchool",
    "nSpell_kind",
    "nEffect_sprite_id",
    "nEffect_kind",
    "nInv_component_id",
    "nEffect_subkind",
    "nEffect_magnitude",
    "nEffect_param",
)


def test_spelldef(native):
    rng = random.Random(1)
    records = [rng.randbytes(RECORD) for _ in range(500)]
    records += [bytes([0xFF] * RECORD), bytes(RECORD), bytes([0x80] * RECORD)]
    out = native.call("cspell.spelldef", b"".join(records))
    for i, record in enumerate(records):
        got = struct.unpack_from("<I10H", out, i * 24)
        assert got[0] == struct.unpack_from("<H", record, 0)[0], f"record {i}: pName"
        for k, name in enumerate(WORDS):
            want = struct.unpack_from("<H", record, 2 + 2 * k)[0]
            assert got[1 + k] == want, f"record {i}: {name}"
