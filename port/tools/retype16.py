"""Spell the reconstruction's integer types with explicit widths.

Borland C++ 3.1 (the original compiler) has a 16-bit ``int`` and a 32-bit
``long``. A modern compiler widens ``int`` to 32 bits (and ``long`` to 64 on
LP64 hosts), which silently changes struct layouts, file formats and 16-bit
wraparound. This tool rewrites every integer type-specifier sequence in the
game's C sources to the fixed-width names from ``bak/INCLUDE/gtypes.h``:

    int, signed, signed int                       -> s16
    unsigned, unsigned int                         -> u16
    long, long int, signed long, ...               -> s32
    unsigned long, unsigned long int               -> u32

``char`` and ``short`` types are left alone: both already have the same width
natively. ``short`` must not become ``s16``/``u16`` either, because under
Borland those are ``int``/``unsigned int``, and BC++ 2.0 (which compiles a few
of the game's modules) treats ``unsigned short`` differently from ``unsigned
int``: it promotes ``unsigned short`` to signed ``int``, so a comparison with an
``int`` is signed (verified: SHAPETBL.C's ``i < g_spriteBankCount`` assembles to
JL with ``unsigned short`` and to JB with ``unsigned int``).

Under Borland the typedefs resolve to exactly the original types, so the
rewrite is byte-neutral for the DOS build; natively they resolve to <stdint.h>
types.

The rewrite is token-aware: comments, string/char literals, ``#include`` /
``#pragma`` / conditional directives and Borland inline ``asm`` statements
(single-line and ``asm { ... }`` blocks, which contain x86 mnemonics such as
``int 21h``) are copied verbatim. ``#define`` bodies are rewritten.

Usage: python -I port/tools/retype16.py [--check] FILE...
"""

from __future__ import annotations

import re
import sys

INT_KEYWORDS = {"signed", "unsigned", "short", "long", "int", "char"}
ASM_KEYWORDS = {"asm", "_asm", "__asm"}
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
WS = re.compile(r"[ \t\r\n]+")


class SweepError(Exception):
    pass


def canonical(words: list[str]) -> str | None:
    """Map a type-specifier keyword run to its fixed-width name (None = keep)."""
    if "char" in words or "short" in words:
        return None
    is_unsigned = "unsigned" in words
    longs = words.count("long")
    if longs > 1:
        raise SweepError(f"unsupported type {' '.join(words)!r}")
    if longs == 1:
        return "u32" if is_unsigned else "s32"
    return "u16" if is_unsigned else "s16"


def skip_string(text: str, i: int, quote: str) -> int:
    """Return the index just past the literal starting at text[i] == quote."""
    i += 1
    while i < len(text):
        c = text[i]
        if c == "\\":
            i += 2
            continue
        if c == quote:
            return i + 1
        if c == "\n":
            raise SweepError("unterminated literal")
        i += 1
    raise SweepError("unterminated literal")


def end_of_logical_line(text: str, i: int) -> int:
    """Index of the newline ending the (backslash-continued) line at i."""
    while True:
        j = text.find("\n", i)
        if j == -1:
            return len(text)
        k = j - 1
        if k >= 0 and text[k] == "\r":
            k -= 1
        if k >= 0 and text[k] == "\\":
            i = j + 1
            continue
        return j


def sweep(text: str) -> tuple[str, int]:
    out: list[str] = []
    i = 0
    n = len(text)
    at_line_start = True
    changes = 0
    while i < n:
        c = text[i]
        if c == "\n":
            out.append(c)
            i += 1
            at_line_start = True
            continue
        if c in " \t\r\f\v":
            out.append(c)
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append(text[i:j])
            i = j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j == -1:
                raise SweepError("unterminated comment")
            out.append(text[i : j + 2])
            i = j + 2
            continue
        if c == "#" and at_line_start:
            j = end_of_logical_line(text, i)
            m = re.match(r"#[ \t]*([A-Za-z_]+)", text[i:j])
            directive = m.group(1) if m else ""
            if directive == "define":
                # Keep "#define NAME" (and a parameter list) verbatim, sweep the body.
                head = re.match(r"#[ \t]*define[ \t]+[A-Za-z_][A-Za-z0-9_]*(\([^)]*\))?", text[i:j])
                hend = i + (head.end() if head else 0)
                out.append(text[i:hend])
                body, k = sweep(text[hend:j])
                out.append(body)
                changes += k
            else:
                out.append(text[i:j])
            i = j
            continue
        at_line_start = False
        if c in "\"'":
            j = skip_string(text, i, c)
            out.append(text[i:j])
            i = j
            continue
        m = IDENT.match(text, i)
        if m:
            word = m.group(0)
            if word in ASM_KEYWORDS:
                k = m.end()
                ws = WS.match(text, k)
                k2 = ws.end() if ws else k
                if k2 < n and text[k2] == "{":
                    j = text.find("}", k2)
                    if j == -1:
                        raise SweepError("unterminated asm block")
                    j += 1
                else:
                    j = text.find("\n", i)
                    j = n if j == -1 else j
                out.append(text[i:j])
                i = j
                continue
            if word in INT_KEYWORDS:
                words = [word]
                end = m.end()
                k = end
                while True:
                    ws = WS.match(text, k)
                    k2 = ws.end() if ws else k
                    m2 = IDENT.match(text, k2)
                    if m2 and m2.group(0) in INT_KEYWORDS:
                        words.append(m2.group(0))
                        end = k = m2.end()
                        continue
                    if m2 and m2.group(0) == "double":
                        raise SweepError("long double is not supported")
                    break
                name = canonical(words)
                if name is None:
                    out.append(text[i:end])
                else:
                    out.append(name)
                    changes += 1
                i = end
                continue
            out.append(word)
            i = m.end()
            continue
        out.append(c)
        i += 1
    return "".join(out), changes


def main(argv: list[str]) -> int:
    check = "--check" in argv
    files = [a for a in argv if a != "--check"]
    total = 0
    touched = 0
    for path in files:
        with open(path, encoding="latin-1", newline="") as f:
            text = f.read()
        try:
            new, k = sweep(text)
        except SweepError as e:
            print(f"{path}: {e}", file=sys.stderr)
            return 2
        if k:
            touched += 1
            total += k
            if not check:
                with open(path, "w", encoding="latin-1", newline="") as f:
                    f.write(new)
    verb = "would rewrite" if check else "rewrote"
    print(f"{verb} {total} type specifiers in {touched} of {len(files)} files")
    return 1 if check and total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
