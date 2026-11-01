"""Deterministic replays: the port on the virtual clock, scripted, logged frame by frame.

A replay runs the port headless on the virtual clock (BAK_CLOCK=virtual) with a scenario's
scripted input and a fresh user folder, so it plays out the same way every time and takes
seconds. It writes two logs: every picture shown (BAK_FRAME_LOG: the time, the size, a CRC-32 of
the pixels) and the game's state on each pass of the world loop (BAK_STATE_LOG). Builds are
compared by their logs. A change that must not alter the game (a mechanical sweep) leaves both
identical; a change to the presentation only leaves the state log identical.

    uv run python -I port/tools/replay.py record world --data "E:/GOG/Betrayal at Krondor"
    uv run python -I port/tools/replay.py check world --data "E:/GOG/Betrayal at Krondor"

`record` keeps a run's logs as the scenario's baseline, `check` runs again and compares with it.
Runs go to work/replay/<scenario>/, never committed: the logs and the last frame come from the
game's art. The scenarios are the golden screenshots' (dosbox_golden.py), a walk, a fight, the
spawned objects of zone 4 and a teleport within the zone.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosbox_golden import SCENARIOS as GOLDEN  # noqa: E402  (the golden scenarios' scripts)

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "work" / "replay"
GOLDEN_DIR = ROOT / "work" / "golden"
PORT_EXE = ROOT / "build" / "win-x86-clangcl" / "betterkrondor.exe"
LOGS = ("frames", "state")


def world_then(steps: str, quit_ms: int) -> str:
    """The world scenario's script with its quit replaced by more steps and a later quit."""
    script = GOLDEN["world"].port_input
    end = "90000:quit"
    assert script.endswith(end), script
    return script[: -len(end)] + steps + f"{quit_ms}:quit"


SCENARIOS = {name: s.port_input for name, s in GOLDEN.items()}
# Scenarios that must play out as another does, so they are checked against its baseline.
SAME_AS: dict[str, str] = {}
# Up steps forward, Left and Right turn: three steps along the road, a turn, a step, two turns
# back, a step.
WALK = ("90000:key:Up;91000:key:Up;92000:key:Up;93000:key:Left;94000:key:Up;"
        "95000:key:Right;96000:key:Right;97000:key:Up;")
SCENARIOS["walk"] = world_then(WALK, 100000)
# The same walk on W, A and D, which the port binds to the arrows where the party moves.
SCENARIOS["walk-wasd"] = world_then(
    WALK.replace(":Up;", ":W;").replace(":Left;", ":A;").replace(":Right;", ":D;"), 100000)
SAME_AS["walk-wasd"] = "walk"
# And on a gamepad's d-pad.
SCENARIOS["walk-pad"] = world_then(
    WALK.replace("key:Up;", "pad:dpup;").replace("key:Left;", "pad:dpleft;")
    .replace("key:Right;", "pad:dpright;"), 100000)
SAME_AS["walk-pad"] = "walk"
# Movement held: W for 1.45 s, which steps on its press and then repeats on the clock; and the
# view's Up button held with the mouse as long, which steps at the same rate once the delay is
# over (REQ_MAIN.DAT's entry for 48h is 151,113, 32x8).
SCENARIOS["hold-key"] = world_then("90000:key:W,1450;", 93000)
SCENARIOS["hold-button"] = world_then("90000:hold:167,117,1450;", 93000)
# Chapter 1's first fight (DEF_COMB record 0, the box of cells 24-33 x 4-10 of zone 1's tile
# 10,14): the party is teleported two cells north of it, facing 8400h, turns south, steps in
# with Space for the dialog's pages, and A plays the fight out (auto-combat). Then two steps
# past the corpse. (The heading dates from the original's teleport test, which skipped this
# teleport at the zone's kept heading, 8000h: see the teleport scenario.)
SCENARIOS["combat"] = world_then(
    "90000:teleport:1,10,14,28,12,8400;91000:key:Right;"
    + "".join(f"{92000 + 400 * i}:key:Up;{92200 + 400 * i}:key:Space;" for i in range(8))
    + "".join(f"{95400 + 400 * i}:key:Space;" for i in range(9))
    + "100000:key:A;130000:key:Up;131000:key:Up;", 135000)
# The zone pool's last entries, past its first 64 KB natively: zone 4's sixteen stones (shape
# D4h) that chapter 1 spawns around tile 10,14 sit in the visible pool. The party is teleported
# among them, looks north, then turns about.
SCENARIOS["spawns"] = world_then(
    "90000:teleport:4,10,14,14,10,0;"
    + "".join(f"{92000 + 500 * i}:key:Left;" for i in range(32)), 110000)
# The teleport the original skipped: to zone 1's tile 10,14, two rows from the party's start in
# its tile column (10,16), at the heading kept from the zone's entry, 8000h. It is the combat
# scenario's place, two cells north of the fight (the next row, 10,15, draws Gouraud-shaded
# shapes: GOURAUD.ASM is not translated yet). The party arrives and waits.
SCENARIOS["teleport"] = world_then("90000:teleport:1,10,14,28,12,8000;", 93000)


def run(scenario: str, data: Path, exe: Path, out: Path, sets: list[str]) -> None:
    out.mkdir(parents=True, exist_ok=True)
    user = out / "user"
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    for name in LOGS:
        (out / f"{name}.log").unlink(missing_ok=True)
    (out / "last.bmp").unlink(missing_ok=True)
    env = dict(os.environ, BAK_CLOCK="virtual", BAK_HEADLESS="1", SDL_VIDEO_DRIVER="dummy",
               BAK_INPUT=SCENARIOS[scenario], BAK_FRAME_LOG=str(out / "frames.log"),
               BAK_STATE_LOG=str(out / "state.log"), BAK_DUMP_FRAME=str(out / "last.bmp"))
    start = time.monotonic()
    settings = [arg for pair in sets for arg in ("--set", pair)]
    r = subprocess.run([str(exe), "--data", str(data), "--user", str(user), *settings], env=env,
                       capture_output=True, text=True, timeout=900, check=False)
    seconds = time.monotonic() - start
    (out / "stderr.txt").write_text(r.stderr, encoding="utf-8", newline="\n")
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit(f"{scenario}: the port exited with {r.returncode:#x} (see {out})")
    frames = (out / "frames.log").read_text(encoding="utf-8").splitlines()
    states = (out / "state.log").read_text(encoding="utf-8").splitlines() \
        if (out / "state.log").exists() else []
    end = frames[-1].split()[0] if frames else "?"
    print(f"{scenario}: {len(frames)} pictures and {len(states)} states up to {end} ms of game "
          f"time, in {seconds:.1f} s")
    golden = GOLDEN_DIR / scenario / "port.bmp"
    if golden.exists() and (out / "last.bmp").exists():
        same = golden.read_bytes() == (out / "last.bmp").read_bytes()
        print(f"  last frame {'equals' if same else 'differs from'} the golden capture {golden}")


def compare(baseline: Path, current: Path, what: str) -> bool:
    a = baseline.read_text(encoding="utf-8").splitlines() if baseline.exists() else []
    b = current.read_text(encoding="utf-8").splitlines() if current.exists() else []
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            print(f"  {what}: line {i + 1} differs\n    baseline: {x}\n    this run: {y}")
            return False
    if len(a) != len(b):
        print(f"  {what}: {len(a)} lines in the baseline, {len(b)} in this run")
        return False
    print(f"  {what}: identical ({len(a)} lines)")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for cmd, help_text in (("record", "run a scenario and keep its logs as the baseline"),
                           ("check", "run a scenario and compare its logs with the baseline")):
        p = sub.add_parser(cmd, help=help_text)
        p.add_argument("scenario", choices=sorted(SCENARIOS))
        p.add_argument("--data", required=True, help="the game folder")
        p.add_argument("--exe", default=str(PORT_EXE), help="the port's executable")
        p.add_argument("--set", action="append", default=[], metavar="SECTION.KEY=VALUE",
                       help="a setting for the run (e.g. video.cursor=game), as the port's")
    args = ap.parse_args()

    out = OUT_DIR / args.scenario
    base = OUT_DIR / SAME_AS.get(args.scenario, args.scenario)
    if args.cmd == "record" and base != out:
        print(f"{args.scenario} is checked against {base.name}'s baseline: record that one")
        return 2
    run(args.scenario, Path(args.data), Path(args.exe), out, args.set)
    if args.cmd == "record":
        for name in LOGS:
            if (out / f"{name}.log").exists():
                shutil.copyfile(out / f"{name}.log", out / f"baseline-{name}.log")
        print(f"  recorded as the baseline in {out}")
        return 0
    if base != out:
        print(f"  against {base.name}'s baseline")
    ok = [compare(base / f"baseline-{name}.log", out / f"{name}.log", f"{name} log") for name in LOGS]
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main())
