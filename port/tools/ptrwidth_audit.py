"""Audit the game sources for 16-bit pointer habits that clang's diagnostics let through.

    python -I port/tools/ptrwidth_audit.py [--build <dir>] [--jobs N]

The original kept near pointers in 2 bytes and far pointers as two words. The build makes the
casts between pointers and narrower integers errors; this audit walks clang's JSON AST of every
game source (clang-cl /Zs -ast-dump=json, with the flags in the build's compile_commands.json)
for what remains:

  alloc-size       an allocation sized without sizeof whose result becomes a pointer to a struct
                   holding pointers, or to pointers: the original's size, with 2-byte pointers
  io-sizeof        a file read or write sized by sizeof of a struct holding pointers: on disk the
                   pointers take 2 bytes, so such records are converted field by field
  word-of-pointer  a 16-bit lvalue over a pointer's storage, *(u16 *)&p or ((u16 *)&p)[1]: natively
                   the low word is not an offset and carries are lost
  pointer-to-word  a pointer converted to an integer and then to 16 bits, (u16)(uintptr_t)p

Findings are matched with port/tools/ptrwidth_allowlist.txt (kind, file, function and a reason, one
a line; a function listed n times allows n findings). Anything else fails the audit, which CI runs
after the build.
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
ALLOWLIST = Path(__file__).with_name("ptrwidth_allowlist.txt")

ALLOCATORS = {
    "galloc_safe_zcalloc": 0,
    "my_malloc": 0,
    "alloc_far": 0,
    "actormotion_xzmalloc": 0,
    "malloc": 0,
    "calloc": 1,
    "farmalloc": 0,
    "_fmalloc": 0,
}
IO_CALLS = {
    "res_fread",
    "res_fwrite",
    "res_fread_far",
    "res_fwrite_far",
    "fread",
    "fwrite",
    "read",
    "write",
    "gstate_temp_file_read_at",
    "gstate_temp_file_write_at",
    "stream_read",
}
WORD_TYPES = {"short", "unsigned short", "s16", "u16"}
SKIP = {"ParenExpr", "ImplicitCastExpr"}


class Types:
    """Which types hold pointers, from the translation unit's records and typedefs."""

    def __init__(self) -> None:
        self.records: dict[str, list[str]] = {}
        self.typedefs: dict[str, str] = {}
        self.memo: dict[str, bool] = {}

    def collect(self, node: dict) -> None:
        kind = node.get("kind")
        if kind == "RecordDecl" and node.get("completeDefinition") and node.get("name"):
            fields = [
                f["type"].get("desugaredQualType", f["type"]["qualType"])
                for f in node.get("inner", [])
                if f.get("kind") == "FieldDecl"
            ]
            self.records[f"{node['tagUsed']} {node['name']}"] = fields
        elif kind == "TypedefDecl" and node.get("name"):
            t = node["type"]
            self.typedefs[node["name"]] = t.get("desugaredQualType", t["qualType"])
        for child in node.get("inner", []):
            if child.get("kind") in ("RecordDecl", "TypedefDecl", "FieldDecl"):
                self.collect(child)

    @staticmethod
    def bare(t: str) -> str:
        t = re.sub(r"\b(const|volatile|restrict)\b", "", t)
        return re.sub(r"\s+", " ", t).strip()

    def holds_pointers(self, t: str) -> bool:
        t = self.bare(t)
        if t in self.memo:
            return self.memo[t]
        self.memo[t] = False
        if "*" in t or "(" in t:
            result = True
        elif m := re.fullmatch(r"(.*?) ?\[\d*\](?:\[\d*\])*", t):
            result = self.holds_pointers(m.group(1))
        elif t in self.records:
            result = any(self.holds_pointers(f) for f in self.records[t])
        elif t in self.typedefs:
            result = self.holds_pointers(self.typedefs[t])
        else:
            result = False
        self.memo[t] = result
        return result

    def pointee(self, t: str) -> str | None:
        t = self.bare(t)
        if t in self.typedefs:
            t = self.bare(self.typedefs[t])
        if t.endswith("*"):
            return t[:-1].strip()
        return None


def strip(node: dict) -> dict:
    while node.get("kind") in SKIP and node.get("inner"):
        node = node["inner"][0]
    return node


def qual(node: dict) -> str:
    t = node.get("type", {})
    return t.get("desugaredQualType", t.get("qualType", ""))


def is_word(t: str) -> bool:
    return Types.bare(t) in WORD_TYPES


def is_pointer(types: Types, t: str) -> bool:
    return types.pointee(t) is not None


def contains_sizeof(node: dict, types: Types | None = None) -> bool | str:
    """Whether the expression takes a sizeof; with types, the type of one that holds pointers."""
    if node.get("kind") == "UnaryExprOrTypeTraitExpr" and node.get("name") == "sizeof":
        if types is None:
            return True
        t = node["argType"]["qualType"] if "argType" in node else qual(strip(node["inner"][0]))
        return t if types.holds_pointers(t) and not is_pointer(types, t) else False
    for child in node.get("inner", []):
        found = contains_sizeof(child, types)
        if found:
            return found
    return False


def callee(node: dict) -> str | None:
    if node.get("kind") != "CallExpr" or not node.get("inner"):
        return None
    ref = strip(node["inner"][0])
    if ref.get("kind") == "DeclRefExpr":
        return ref["referencedDecl"].get("name")
    return None


class Walker:
    def __init__(self, main: str, types: Types):
        self.main = main.replace("\\", "/").lower()
        self.types = types
        self.file = ""
        self.line = 0
        self.function = ""
        self.findings: list[tuple[str, str, int, str]] = []

    def _track(self, loc: dict | None) -> None:
        if not loc:
            return
        for part in ("spellingLoc", "expansionLoc"):
            if part in loc:
                self._track(loc[part])
        if "file" in loc:
            self.file = loc["file"].replace("\\", "/").lower()
        if "line" in loc:
            self.line = loc["line"]

    def _where(self, node: dict) -> int:
        self._track(node.get("loc"))
        rng = node.get("range", {})
        self._track(rng.get("begin"))
        line = self.line
        self._track(rng.get("end"))
        return line

    def report(self, kind: str, line: int) -> None:
        self.findings.append((kind, self.function, line, ""))

    def walk(self, node: dict, in_main: bool = False) -> None:
        kind = node.get("kind")
        line = self._where(node)
        if kind == "FunctionDecl":
            in_main = self.file == self.main
            self.function = node.get("name", "")
        if in_main:
            self.check(node, kind, line)
        for child in node.get("inner", []):
            if isinstance(child, dict):
                self.walk(child, in_main)

    def check(self, node: dict, kind: str, line: int) -> None:
        types = self.types
        if kind in ("CStyleCastExpr", "ImplicitCastExpr"):
            inner = strip(node["inner"][0]) if node.get("inner") else {}
            target = types.pointee(qual(node))
            if callee(inner) in ALLOCATORS and target is not None:
                args = inner["inner"][1:]
                index = ALLOCATORS[callee(inner)]
                if (
                    index < len(args)
                    and not contains_sizeof(args[index])
                    and (types.holds_pointers(target))
                ):
                    self.report("alloc-size", line)
            if is_word(qual(node)) and node.get("castKind") == "IntegralCast":
                deeper = inner
                while (
                    deeper.get("kind") in ("CStyleCastExpr", "ImplicitCastExpr")
                    and deeper.get("castKind") == "IntegralCast"
                    and deeper.get("inner")
                ):
                    deeper = strip(deeper["inner"][0])
                if deeper.get("castKind") == "PointerToIntegral":
                    self.report("pointer-to-word", line)
        elif kind in ("UnaryOperator", "ArraySubscriptExpr") and (
            kind == "ArraySubscriptExpr" or node.get("opcode") == "*"
        ):
            base = strip(node["inner"][0])
            if base.get("kind") == "CStyleCastExpr" and is_word(types.pointee(qual(base)) or ""):
                addr = strip(base["inner"][0])
                if (
                    addr.get("kind") == "UnaryOperator"
                    and addr.get("opcode") == "&"
                    and is_pointer(types, qual(strip(addr["inner"][0])))
                ):
                    self.report("word-of-pointer", line)
        elif kind == "CallExpr" and callee(node) in IO_CALLS:
            for arg in node.get("inner", [])[1:]:
                if contains_sizeof(arg, types):
                    self.report("io-sizeof", line)
                    break


def split_command(command: str) -> list[str]:
    """A compile command's words: the compiler's path may be quoted, the rest has no spaces."""
    if command.startswith('"'):
        exe, rest = command[1:].split('"', 1)
        return [exe, *rest.split()]
    return command.split()


def audit(entry: dict) -> tuple[str, list, str]:
    args = [
        a
        for a in split_command(entry["command"])
        if a and not a.startswith("/Fo") and not a.startswith("/Fd") and a != "-c"
    ]
    args[1:1] = ["/Zs", "-Xclang", "-ast-dump=json"]
    r = subprocess.run(args, capture_output=True, cwd=entry["directory"])
    if r.returncode != 0:
        return entry["file"], [], r.stderr.decode("latin-1")[-2000:]
    ast = json.loads(r.stdout)
    types = Types()
    for node in ast.get("inner", []):
        types.collect(node)
    walker = Walker(entry["file"], types)
    walker.walk(ast)
    return entry["file"], walker.findings, ""


def load_allowlist() -> collections.Counter:
    allowed = collections.Counter()
    for line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            kind, path, function = line.split()
            allowed[(kind, path, function)] += 1
    return allowed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default=str(ROOT / "build" / "win-x86-clangcl"))
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()

    cc = json.loads((Path(args.build) / "compile_commands.json").read_text(encoding="utf-8"))
    game = [e for e in cc if "/BAK/SRC/" in e["file"].upper().replace("\\", "/")]
    found = collections.Counter()
    where: dict[tuple, list[str]] = collections.defaultdict(list)
    failed = False
    with ThreadPoolExecutor(args.jobs) as ex:
        for path, findings, error in ex.map(audit, game):
            rel = Path(path).resolve().relative_to(ROOT / "bak" / "SRC").as_posix()
            if error:
                print(f"{rel}: clang failed\n{error}")
                failed = True
            for kind, function, line, _ in findings:
                key = (kind, rel, function)
                found[key] += 1
                where[key].append(f"{rel}:{line}")
    allowed = load_allowlist()
    for key in sorted(found):
        extra = found[key] - allowed[key]
        if extra > 0:
            kind, rel, function = key
            print(f"{kind}: {', '.join(where[key])} in {function}")
            failed = True
    for key in sorted(allowed):
        if found[key] < allowed[key]:
            print(f"note: the allowlist's {' '.join(key)} matches {found[key]} of {allowed[key]}")
    total = sum(found.values())
    print(
        f"{len(game)} sources, {total} findings, {sum(allowed.values())} allowed: "
        f"{'FAILED' if failed else 'ok'}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
