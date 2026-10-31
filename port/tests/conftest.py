"""Fixtures for the differential tests.

Inputs, each overridable from the environment:

    BAK_REF_DIR        reference-build artifacts (default: work/ref); see README.md
    BAK_DIFFTEST_EXE   the native test runner (default: build/win-x86-clangcl/bak_difftest.exe)
    BAK_VERSION        which original to compare against, 100 or 102 (default: 102)

Tests skip when an input is missing, so a checkout without the reference artifacts still
passes ctest; CI and contributors porting ASM run them for real.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from harness.machine import Machine  # noqa: E402
from harness.mzexe import load_with_map  # noqa: E402
from harness.native import Native  # noqa: E402

REPO = Path(__file__).resolve().parents[2]

_EXECUTABLES = {
    "100": ("OUT/KRONDOR.EXE", "OUT/KRONDOR.MAP"),
    "102": ("OUT102/KRONDOR.EXE", "OUT102/KRONDOR.MAP"),
}


def ref_dir() -> Path:
    return Path(os.environ.get("BAK_REF_DIR") or REPO / "work" / "ref")


def _require(path: Path, what: str) -> Path:
    if not path.exists():
        pytest.skip(f"{what} not found at {path} (see port/tests/README.md)")
    return path


@pytest.fixture(scope="session")
def krondor_image():
    version = os.environ.get("BAK_VERSION", "102")
    exe, map_file = _EXECUTABLES[version]
    return load_with_map(_require(ref_dir() / exe, "reference KRONDOR.EXE"),
                         _require(ref_dir() / map_file, "reference KRONDOR.MAP"))


@pytest.fixture
def krondor(krondor_image) -> Machine:
    """A fresh machine with the original KRONDOR.EXE loaded (data at its initial values)."""
    return Machine(krondor_image)


def _native_exe() -> Path:
    default = REPO / "build" / "win-x86-clangcl" / "bak_difftest.exe"
    return _require(Path(os.environ.get("BAK_DIFFTEST_EXE") or default), "bak_difftest")


@pytest.fixture(scope="session")
def native():
    """The C side, shared by the session: globals persist between tests."""
    n = Native(_native_exe())
    yield n
    n.close()


@pytest.fixture
def fresh_native():
    """A C side of its own, with every global at its initial value."""
    n = Native(_native_exe())
    yield n
    n.close()
