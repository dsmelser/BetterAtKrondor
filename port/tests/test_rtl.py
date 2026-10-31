"""The Borland runtime's ctype as the game links it, against port/compat/bakrtl.c.

The game's isalpha and isdigit are Borland's macros, which index _ctype with a plain char:
a byte from 80h up reads the DGROUP bytes before the table (the dialogs' style codes do).
toupper and tolower are the runtime's functions.
"""

from __future__ import annotations

import struct


def test_ctype_and_the_bytes_before_it(krondor, native):
    seg, off = krondor.sym("__ctype")
    assert native.peek("bakrtl_ctype_block") == krondor.read((seg, off - 128), 128 + 257)


def test_case_conversion(krondor, native):
    # They look at EOF (-1) and the low byte: every low byte under high bytes either side of
    # the sign and of -1.
    values = [
        high << 8 | low for high in (0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF) for low in range(256)
    ]
    for name in ("toupper", "tolower"):
        original = [krondor.call_far(name, c).ax for c in values]
        # each argument natively sign- and zero-extended
        translated = struct.unpack(
            f"<{2 * len(values)}H",
            native.call(f"rtl.{name}", struct.pack(f"<{len(values)}H", *values)),
        )
        assert list(translated[0::2]) == original, name
        assert list(translated[1::2]) == original, name
