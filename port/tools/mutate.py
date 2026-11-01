"""Mutation-check a test: break the code on purpose, one change at a time, and confirm the test
notices.

    python -I port/tools/mutate.py <mutants.py>

mutants.py is a Python file defining
    FILE     the source file to mutate, relative to the repository root
    TESTS    the pytest arguments that should fail for every mutant (differential tests), or
             "unit" for the unit tests (bak_unittest)
    MUTANTS  a list of (old, new) pairs; each old must occur exactly once in FILE

For each mutant the script writes the change, rebuilds bak_difftest or bak_unittest (Windows:
port/tools/win-build.cmd), runs the tests (pytest with -x) and reports "killed" or "SURVIVED".
The file is restored and the runner rebuilt at the end, also on errors and Ctrl+C. A survivor is
either a gap in the test (add the case that tells) or an equivalent mutant (say why in the commit
or the test's comments). Don't run it while another build uses the same build directory.
"""

from __future__ import annotations

import runpy
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def build(target: str) -> bool:
    r = subprocess.run(
        ["cmd", "/c", r"port\tools\win-build.cmd", "win-x86-clangcl", "--target", target],
        cwd=REPO,
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        print(r.stdout[-3000:], r.stderr[-2000:])
    return r.returncode == 0


def tests_pass(tests: list[str] | str) -> bool:
    if tests == "unit":
        unit = REPO / "build" / "win-x86-clangcl" / "bak_unittest.exe"
        return subprocess.run([unit], cwd=REPO, capture_output=True).returncode == 0
    r = subprocess.run(
        ["uv", "run", "--group", "difftest", "pytest", "-q", "-x", *tests],
        cwd=REPO,
        capture_output=True,
        text=True,
    )
    return r.returncode == 0


def main() -> int:
    spec = runpy.run_path(sys.argv[1])
    path = REPO / spec["FILE"]
    original = path.read_bytes()
    text = original.decode("utf-8")
    target = "bak_unittest" if spec["TESTS"] == "unit" else "bak_difftest"
    survivors = 0
    try:
        for i, (old, new) in enumerate(spec["MUTANTS"]):
            n = text.count(old)
            if n != 1:
                print(f"[{i}] SKIPPED: {old!r} occurs {n} times")
                continue
            path.write_bytes(text.replace(old, new).encode("utf-8"))
            if not build(target):
                print(f"[{i}] BUILD FAILED: {old!r} -> {new!r}")
                continue
            passed = tests_pass(spec["TESTS"])
            print(f"[{i}] {'SURVIVED' if passed else 'killed'}: {old!r} -> {new!r}", flush=True)
            survivors += passed
    finally:
        path.write_bytes(original)
        build(target)
    print(f"{survivors} survivor(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
