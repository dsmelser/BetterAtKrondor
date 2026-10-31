"""Inventory of the TASM modules linked into KRONDOR.EXE and the symbols they export.

Shared by the stub generator and status tooling. Reads the 1.02 link response file
(bak/KRN102.RSP, a superset of the 1.00 one) and every PUBLIC of each in-EXE .ASM.

    python -I port/tools/asm_publics.py        # print the inventory
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BS = chr(92)

SEG_RE = re.compile(r"^\s*([A-Za-z0-9_@$?]+)\s+segment\b(.*)$", re.I)
ENDS_RE = re.compile(r"^\s*[A-Za-z0-9_@$?]+\s+ends\b", re.I)
PUBLIC_RE = re.compile(r"^\s*public\s+(.+)$", re.I)
PROC_RE = re.compile(r"^\s*([A-Za-z0-9_@$?]+)\s+proc\b", re.I)
DATA_RE = re.compile(r"^\s*([A-Za-z0-9_@$?]+)\s+(db|dw|dd|df|dq|dt|label)\b", re.I)
CODE_LABEL_RE = re.compile(r"^\s*([A-Za-z0-9_@$?]+)::?")


@dataclass
class AsmSymbol:
    name: str          # assembler name, e.g. _rand
    c_name: str        # C name, e.g. rand
    kind: str          # "code" or "data"
    segment: str


@dataclass
class AsmModule:
    name: str
    path: str
    symbols: list[AsmSymbol] = field(default_factory=list)


def _strip_comment(line: str) -> str:
    out = []
    quote = None
    for ch in line:
        if quote:
            out.append(ch)
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
            out.append(ch)
        elif ch == ";":
            break
        else:
            out.append(ch)
    return "".join(out)


def link_objects(rsp: str = "bak/KRN102.RSP") -> list[str]:
    text = open(os.path.join(ROOT, rsp)).read()
    return re.findall(r"OUT(?:102)?" + re.escape(BS) + r"([A-Z0-9_]+)\.OBJ", text)


def _sources() -> dict[str, str]:
    found = {}
    for d, _, fs in os.walk(os.path.join(ROOT, "bak", "SRC")):
        for f in fs:
            stem, ext = os.path.splitext(f)
            if ext.upper() == ".ASM" and "DRIVERS" not in d.upper():
                found[stem.upper()] = os.path.join(d, f)
    return found


def parse_module(name: str, path: str) -> AsmModule:
    mod = AsmModule(name, os.path.relpath(path, ROOT).replace(BS, "/"))
    lines = [_strip_comment(l) for l in open(path, encoding="latin-1").read().split("\n")]
    publics: list[str] = []
    for line in lines:
        m = PUBLIC_RE.match(line)
        if m:
            publics += [p.strip() for p in m.group(1).split(",") if p.strip()]
    wanted = set(publics)
    kinds: dict[str, tuple[str, str]] = {}
    segment = ""
    for line in lines:
        m = SEG_RE.match(line)
        if m:
            segment = m.group(1)
            continue
        if ENDS_RE.match(line):
            continue
        for rx, kind in ((PROC_RE, "code"), (DATA_RE, "data")):
            m = rx.match(line)
            if m and m.group(1) in wanted and m.group(1) not in kinds:
                k = kind
                if kind == "data" and m.group(2).lower() == "label":
                    k = "code" if re.search(r"\b(far|near|proc)\b", line, re.I) else "data"
                kinds[m.group(1)] = (k, segment)
        m = CODE_LABEL_RE.match(line)
        if m and m.group(1) in wanted and m.group(1) not in kinds:
            kinds[m.group(1)] = ("code", segment)
    for p in publics:
        kind, seg = kinds.get(p, ("code", "?"))
        if "DATA" in seg.upper() or "BSS" in seg.upper():
            kind = "data"
        c_name = p[1:] if p.startswith("_") else p
        mod.symbols.append(AsmSymbol(p, c_name, kind, seg))
    return mod


def in_exe_modules() -> list[AsmModule]:
    srcs = _sources()
    mods = []
    for obj in link_objects():
        if obj in srcs:
            mods.append(parse_module(obj, srcs[obj]))
    return mods


if __name__ == "__main__":
    total = {"code": 0, "data": 0}
    for m in in_exe_modules():
        kinds = [s.kind for s in m.symbols]
        total["code"] += kinds.count("code")
        total["data"] += kinds.count("data")
        print(f"{m.name:10s} code={kinds.count('code'):3d} data={kinds.count('data'):3d}  {m.path}")
    print(total)
