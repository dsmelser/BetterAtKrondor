"""RAND.ASM, the game's PRNG, against port/asm/rand.c."""

from __future__ import annotations

import random
import struct

STATE = {"g_randPosJ": 2, "g_randPosI": 2, "g_randLast": 2, "g_randState": 112}


def original_state(m) -> dict[str, bytes]:
    return {name: m.read(name, size) for name, size in STATE.items()}


def native_state(n) -> dict[str, bytes]:
    return {name: n.peek(name) for name in STATE}


def draw_original(m, count: int) -> list[int]:
    return [m.call_far("rand").ax for _ in range(count)]


def draw_native(n, count: int) -> list[int]:
    return list(struct.unpack(f"<{count}H", n.call("rand.draw", struct.pack("<H", count))))


def test_initial_state(krondor, fresh_native):
    assert native_state(fresh_native) == original_state(krondor)


def test_cold_start_sequence(krondor, fresh_native):
    # Before _rand_reset_indices both cursors sit at slot 0.
    assert draw_native(fresh_native, 400) == draw_original(krondor, 400)
    assert native_state(fresh_native) == original_state(krondor)


def test_sequence_after_reset(krondor, native):
    for name, data in original_state(krondor).items():
        native.poke(name, data)
    krondor.call_far("_rand_reset_indices")
    native.call("rand.reset_indices")
    assert native_state(native) == original_state(krondor)
    assert draw_native(native, 2000) == draw_original(krondor, 2000)
    assert native_state(native) == original_state(krondor)


def test_arbitrary_states(krondor, native):
    rng = random.Random(1993)
    for _ in range(20):
        state = {
            "g_randPosJ": struct.pack("<H", 2 * rng.randrange(56)),
            "g_randPosI": struct.pack("<H", 2 * rng.randrange(56)),
            "g_randLast": struct.pack("<H", rng.randrange(0x10000)),
            "g_randState": bytes(rng.randrange(256) for _ in range(112)),
        }
        for name, data in state.items():
            krondor.write(name, data)
            native.poke(name, data)
        assert draw_native(native, 150) == draw_original(krondor, 150)
        assert native_state(native) == original_state(krondor)
