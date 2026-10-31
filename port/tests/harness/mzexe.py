"""DOS MZ executables and TLINK map files, as the reference build produces them.

The reference build (.github/workflows/reference.yml) links the original binaries with
the period toolchain and keeps the link maps. Loading an executable's load module and
looking routines up in its map lets the tests call any resident routine of the original
with its own data around it, without linking objects by hand.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class MzImage:
    """The load module of an MZ executable, before relocation."""

    image: bytes
    relocations: list[tuple[int, int]]  # (segment, offset) of each word to add the load segment to
    init_cs: int
    init_ip: int
    init_ss: int
    init_sp: int
    symbols: dict[str, tuple[int, int]] = field(default_factory=dict)  # NAME -> (seg, off)

    def symbol(self, name: str) -> tuple[int, int]:
        """(segment, offset) of a public, relative to the load segment. Names are matched
        case-insensitively (TLINK writes them upper-cased); a leading underscore is added
        when the bare name is not found, so C names work as written."""
        key = name.upper()
        if key in self.symbols:
            return self.symbols[key]
        if "_" + key in self.symbols:
            return self.symbols["_" + key]
        raise KeyError(f"no public named {name!r} in the map")


def load_mz(path: str | Path) -> MzImage:
    data = Path(path).read_bytes()
    (magic, cblp, cp, crlc, cparhdr, _minalloc, _maxalloc, ss, sp, _csum, ip, cs, lfarlc,
     _ovno) = struct.unpack_from("<14H", data, 0)
    if magic != 0x5A4D:
        raise ValueError(f"{path}: not an MZ executable")
    end = cp * 512 - ((512 - cblp) if cblp else 0)
    image = data[cparhdr * 16:end]
    relocs = [struct.unpack_from("<HH", data, lfarlc + 4 * i) for i in range(crlc)]
    return MzImage(image=image, relocations=[(seg, off) for off, seg in relocs],
                   init_cs=cs, init_ip=ip, init_ss=ss, init_sp=sp)


_PUBLIC = re.compile(r"^\s*([0-9A-F]{4}):([0-9A-F]{4})\s+(\S+)\s*$")


def parse_map(path: str | Path) -> dict[str, tuple[int, int]]:
    """The 'Publics by Name' section of a TLINK map: NAME -> (segment, offset)."""
    symbols: dict[str, tuple[int, int]] = {}
    in_publics = False
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if "Publics by Name" in line:
            in_publics = True
            continue
        if "Publics by Value" in line:
            break
        if in_publics:
            m = _PUBLIC.match(line)
            if m:
                symbols[m.group(3).upper()] = (int(m.group(1), 16), int(m.group(2), 16))
    return symbols


def load_with_map(exe: str | Path, map_file: str | Path | None) -> MzImage:
    img = load_mz(exe)
    if map_file is not None:
        img.symbols = parse_map(map_file)
    return img
