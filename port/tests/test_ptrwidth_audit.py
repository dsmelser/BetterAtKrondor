"""port/tools/ptrwidth_audit.py's detectors, on a source holding the bugs they are for.

Each line marked with a kind is a bug of that kind the sweep of 2026-11-01 fixed in the game
(the cameras' WorldEntity sized as 24h bytes, the Dialog as 8, a widget table at 2 bytes a pointer,
SPELLS.DAT read as native SpellDefs, offsets added to a pointer's low word); the unmarked lines are
the same operations done right. The source is compiled with a game source's flags from the build.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "port" / "tools"))

import ptrwidth_audit  # noqa: E402

SOURCE = r"""
#include "structs.h"
#include "SRC/SYS/MEM.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/R3D/ACTOR/ACTMOTN.H"

void t_alloc(void) {
    WorldEntity *e = actormotion_xzmalloc(0x24);         /* alloc-size */
    Dialog *d = galloc_safe_zcalloc(8);                   /* alloc-size */
    DialogWidget **w = galloc_safe_zcalloc(4 << 1);       /* alloc-size */
    Dialog *ok = galloc_safe_zcalloc(sizeof(Dialog));
    DialogWidget **wok = galloc_safe_zcalloc(4 * sizeof(DialogWidget *));
    unsigned short *words = galloc_safe_zcalloc(26);
    (void)e; (void)d; (void)w; (void)ok; (void)wok; (void)words;
}

void t_io(ResFile *f, SpellDef *defs, Vec3Long *v) {
    res_fread(defs, sizeof(SpellDef), 45, f);             /* io-sizeof */
    res_fread(v, sizeof(Vec3Long), 1, f);
}

void t_words(unsigned char *p, char *q, u16 n) {
    *(u16 *)&p += 8;                                      /* word-of-pointer */
    ((u16 *)&q)[1] = 3;                                   /* word-of-pointer */
    u16 lo = (u16)(uintptr_t)p;                           /* pointer-to-word */
    *(u16 *)&n += 1;
    p += 8;
    (void)lo;
}
"""


def test_detectors(tmp_path):
    commands = REPO / "build" / "win-x86-clangcl" / "compile_commands.json"
    if not commands.exists():
        pytest.skip(f"no build at {commands.parent}")
    cc = json.loads(commands.read_text(encoding="utf-8"))
    model = next(e for e in cc if e["file"].upper().endswith("/COMBAT/ARENA/COMBAT.C"))
    source = tmp_path / "audit_case.c"
    source.write_text(SOURCE, encoding="latin-1")
    command = model["command"].replace(model["file"].replace("/", "\\"), str(source))
    assert str(source) in command
    _, findings, error = ptrwidth_audit.audit(dict(model, file=str(source), command=command))
    assert not error
    lines = SOURCE.splitlines()
    want = sorted(
        (line.split("/* ")[1].split(" ")[0], n + 1)
        for n, line in enumerate(lines)
        if "/* " in line and line.rstrip().endswith("*/") and "-" in line.split("/* ")[1]
    )
    got = sorted((kind, line) for kind, _, line, _ in findings)
    assert got == want
