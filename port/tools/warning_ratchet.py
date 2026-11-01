"""A ratchet on the game sources' warnings: each category's count may only go down.

    python -I port/tools/warning_ratchet.py [--build <dir>]           # compare with the baseline
    python -I port/tools/warning_ratchet.py --update                  # record the counts
    python -I port/tools/warning_ratchet.py --list <category>         # where they are

Every game source in the build's compile_commands.json is syntax-checked (clang-cl /Zs) with the
warnings below, classes that mark 16-bit habits and what the web build will reject; each warning
is counted once however many sources include it. The counts are kept per clang major version in
warning_counts.json beside this script, since versions differ in what they report. A category
whose count rose fails the check; one that fell asks for --update, which keeps the gain. With no
baseline for the running clang the counts are printed and the check passes.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path(__file__).with_name("warning_counts.json")

WARNINGS = [
    "implicit-int-conversion",  # 16-bit stores of wider values
    "sign-compare",
    "pointer-sign",
    "constant-conversion",
    "bitfield-constant-conversion",
    "cast-align",
    "cast-qual",
    "cast-function-type-strict",  # calls through mismatched function pointers trap in wasm
    "bad-function-cast",
    "incompatible-pointer-types-discards-qualifiers",
    "uninitialized",
    "sometimes-uninitialized",
    "conditional-uninitialized",
    "array-bounds",
    "array-bounds-pointer-arithmetic",
    "null-pointer-arithmetic",
    "tautological-compare",
    "int-in-bool-context",
    "comma",
    "unused-value",
]
DIAG = re.compile(r"^(.*?)\((\d+),(\d+)\): warning: (.*) \[-W([a-zA-Z0-9-]+)")


def split_command(command: str) -> list[str]:
    """A compile command's words: the compiler's path may be quoted, the rest has no spaces."""
    if command.startswith('"'):
        exe, rest = command[1:].split('"', 1)
        return [exe, *rest.split()]
    return command.split()


def game_sources(build: Path) -> list[dict]:
    cc = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    return [e for e in cc if "/BAK/SRC/" in e["file"].upper().replace("\\", "/")]


def check_source(entry: dict) -> list[tuple[str, str, int, int, str]]:
    args = [
        a
        for a in split_command(entry["command"])
        if a and not a.startswith("/Fo") and not a.startswith("/Fd") and a != "-c"
    ]
    flags = ["/Zs"] + [f"/clang:-W{w}" for w in WARNINGS]
    sep = args.index("--") if "--" in args else len(args) - 1
    args[sep:sep] = flags
    r = subprocess.run(
        args, capture_output=True, text=True, cwd=entry["directory"], encoding="latin-1"
    )
    out = []
    for line in (r.stdout + r.stderr).splitlines():
        m = DIAG.match(line.strip())
        if m:
            path = Path(m.group(1)).resolve()
            try:
                path = path.relative_to(ROOT)
            except ValueError:
                pass
            out.append((m.group(5), path.as_posix(), int(m.group(2)), int(m.group(3)), m.group(4)))
    return out


def clang_major(build: Path) -> str:
    entry = game_sources(build)[0]
    exe = split_command(entry["command"])[0]
    r = subprocess.run([exe, "--version"], capture_output=True, text=True)
    m = re.search(r"clang version (\d+)", r.stdout)
    return f"clang {m.group(1)}" if m else "clang ?"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default=str(ROOT / "build" / "win-x86-clangcl"))
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--update", action="store_true", help="record the counts as the baseline")
    ap.add_argument("--list", metavar="CATEGORY", help="print where a category's warnings are")
    args = ap.parse_args()

    build = Path(args.build)
    with ThreadPoolExecutor(args.jobs) as ex:
        results = list(ex.map(check_source, game_sources(build)))
    unique = {
        (cat, path, line, col): msg for diags in results for cat, path, line, col, msg in diags
    }
    counts = collections.Counter(cat for cat, _, _, _ in unique)
    if args.list:
        for (cat, path, line, col), msg in sorted(unique.items()):
            if cat == args.list:
                print(f"{path}:{line}:{col}: {msg}")
        return 0

    version = clang_major(build)
    baselines = json.loads(BASELINE.read_text(encoding="utf-8")) if BASELINE.exists() else {}
    if args.update:
        baselines[version] = dict(sorted(counts.items()))
        BASELINE.write_text(
            json.dumps(baselines, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
        )
        print(f"{version}: {sum(counts.values())} warnings in {len(counts)} categories recorded")
        return 0
    if version not in baselines:
        print(f"no baseline for {version}; the counts ({sum(counts.values())} warnings):")
        for cat, n in sorted(counts.items()):
            print(f"  {cat}: {n}")
        return 0
    base = baselines[version]
    failed = False
    for cat in sorted(set(base) | set(counts)):
        was, now = base.get(cat, 0), counts.get(cat, 0)
        if now > was:
            print(f"{cat}: {now}, up from {was} (--list {cat} shows them)")
            failed = True
        elif now < was:
            print(f"{cat}: {now}, down from {was}: keep the gain with --update")
    print(
        f"{version}: {sum(counts.values())} warnings, baseline {sum(base.values())}: "
        f"{'FAILED' if failed else 'ok'}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
