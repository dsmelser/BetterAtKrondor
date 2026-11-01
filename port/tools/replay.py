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
game's art. The scenarios are the golden screenshots' (dosbox_golden.py), a walk and a fight.
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
# Up steps forward, Left and Right turn: three steps along the road, a turn, a step, two turns
# back, a step.
SCENARIOS["walk"] = world_then(
    "90000:key:Up;91000:key:Up;92000:key:Up;93000:key:Left;94000:key:Up;"
    "95000:key:Right;96000:key:Right;97000:key:Up;", 100000)
# Chapter 1's first fight (DEF_COMB record 0, the box of cells 24-33 x 4-10 of zone 1's tile
# 10,14): the party is teleported two cells north of it, facing 8400h (the game skips a teleport
# at the zone's kept heading, 8000h, onto another row of the party's tile column), turns south,
# steps in with Space for the dialog's pages, and A plays the fight out (auto-combat). Then two
# steps past the corpse.
SCENARIOS["combat"] = world_then(
    "90000:teleport:1,10,14,28,12,8400;91000:key:Right;"
    + "".join(f"{92000 + 400 * i}:key:Up;{92200 + 400 * i}:key:Space;" for i in range(8))
    + "".join(f"{95400 + 400 * i}:key:Space;" for i in range(9))
    + "100000:key:A;130000:key:Up;131000:key:Up;", 135000)


def run(scenario: str, data: Path, exe: Path, out: Path) -> None:
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
    r = subprocess.run([str(exe), "--data", str(data), "--user", str(user)], env=env,
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
    args = ap.parse_args()

    out = OUT_DIR / args.scenario
    run(args.scenario, Path(args.data), Path(args.exe), out)
    if args.cmd == "record":
        for name in LOGS:
            if (out / f"{name}.log").exists():
                shutil.copyfile(out / f"{name}.log", out / f"baseline-{name}.log")
        print(f"  recorded as the baseline in {out}")
        return 0
    ok = [compare(out / f"baseline-{name}.log", out / f"{name}.log", f"{name} log") for name in LOGS]
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main())
