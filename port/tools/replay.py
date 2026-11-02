"""Deterministic replays: the port on the virtual clock, scripted, logged frame by frame.

A replay runs the port headless on the virtual clock (BAK_CLOCK=virtual) with a scenario's
scripted input and a fresh user folder, so it plays out the same way every time and takes
seconds. It writes three logs: every picture shown (BAK_FRAME_LOG: the time, the size, a hash of
the pixels), the game's state on each pass of the world loop (BAK_STATE_LOG), and what the sound
was told (BAK_AUDIO_LOG: the CD's plays and stops), ending with the hash of the sound rendered
(BAK_DUMP_AUDIO, kept as audio.wav). Builds are compared by their logs. A change that must not
alter the game (a mechanical sweep) leaves them identical; a change to the presentation only
leaves the state log identical.

The fresh user folder gets the game's preferences (KRONDOR.CFG) the baselines were recorded with,
so the game folder's own file, if any, plays no part.

    uv run python -I port/tools/replay.py record world --data "E:/GOG/Betrayal at Krondor"
    uv run python -I port/tools/replay.py check world --data "E:/GOG/Betrayal at Krondor"

`record` keeps a run's logs as the scenario's baseline, `check` runs again and compares with it.
Runs go to work/replay/<scenario>/, never committed: the logs and the last frame come from the
game's art. The scenarios are the golden screenshots' (dosbox_golden.py), walks on keys, a
gamepad and held buttons, a fight, the spawned objects of zone 4, a teleport within the zone,
the map's riddle chest, saves and loads, autosaves (off in the others), chapter 9's last
cutscene, and the zone sweeps: every tile of a zone viewed at 8 headings. A golden scenario
with a prelude (a quicksave the port makes first) starts from what it leaves, as the golden does.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dosbox_golden import SCENARIOS as GOLDEN  # noqa: E402  (the golden scenarios' scripts)
from dosbox_golden import run_prelude  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "work" / "replay"
GOLDEN_DIR = ROOT / "work" / "golden"
PORT_EXE = ROOT / "build" / "win-x86-clangcl" / "betterkrondor.exe"
LOGS = ("frames", "state", "audio")
# KRONDOR.CFG: step speed, combat step speed, detail, text speed, flags. The flags' 10h is the CD
# music; the scenarios run with it off (the music on the MT-32 driver), as the GOG copy's file
# had it when the first baselines were recorded.
PREFS = bytes([0, 0, 3, 0, 0x0F])
PREFS_CD_MUSIC = bytes([0, 0, 3, 0, 0x1F])


def world_then(steps: str, quit_ms: int) -> str:
    """The world scenario's script with its quit replaced by more steps and a later quit."""
    script = GOLDEN["world"].port_input
    end = "90000:quit"
    assert script.endswith(end), script
    return script[: -len(end)] + steps + f"{quit_ms}:quit"


SCENARIOS = {name: s.port_input for name, s in GOLDEN.items()}
# Scenarios that must play out as another does, so they are checked against its baseline.
SAME_AS: dict[str, str] = {}
# Scenarios that must leave the game in the same states as another, in the same order, though at
# other moments and with other pictures (the presentation changed): only their state logs are
# compared with its baseline, the times set aside.
SAME_STATES_AS: dict[str, str] = {}
# Settings a scenario runs with (as --set), besides the command line's. Every scenario runs with
# the autosaves off unless it turns them on: they make a folder the save dialogs list.
DEFAULT_SETS = ["saves.autosave=false"]
SCENARIO_SETS: dict[str, list[str]] = {}
# Checks of what a run left in its user folder, after its logs.
SCENARIO_CHECKS: dict[str, Callable[[Path], bool]] = {}
# Up steps forward, Left and Right turn: three steps along the road, a turn, a step, two turns
# back, a step.
WALK = ("90000:key:Up;91000:key:Up;92000:key:Up;93000:key:Left;94000:key:Up;"
        "95000:key:Right;96000:key:Right;97000:key:Up;")
SCENARIOS["walk"] = world_then(WALK, 100000)
# The walk with the party's steps and turns at once, as the original drew them: smooth steps are
# a picture only, so the game goes through the same states.
SCENARIOS["walk-instant"] = SCENARIOS["walk"]
SCENARIO_SETS["walk-instant"] = ["video.step_time=0"]
SAME_STATES_AS["walk-instant"] = "walk"
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
# with Space for the dialog's pages, and A plays the fight out (auto-combat). Then Space for the
# victory's dialog (dialogs leave the arrows alone) and two steps past the corpse. (The heading dates from the original's teleport test, which skipped this
# teleport at the zone's kept heading, 8000h: see the teleport scenario.)
SCENARIOS["combat"] = world_then(
    "90000:teleport:1,10,14,28,12,8400;91000:key:Right;"
    + "".join(f"{92000 + 400 * i}:key:Up;{92200 + 400 * i}:key:Space;" for i in range(8))
    + "".join(f"{95400 + 400 * i}:key:Space;" for i in range(9))
    + "100000:key:A;128000:key:Space;130000:key:Up;131000:key:Up;", 135000)
# The zone pool's last entries, past its first 64 KB natively: zone 4's sixteen stones (shape
# D4h) that chapter 1 spawns around tile 10,14 sit in the visible pool. The party is teleported
# among them, looks north, then turns about.
SCENARIOS["spawns"] = world_then(
    "90000:teleport:4,10,14,14,10,0;"
    + "".join(f"{92000 + 500 * i}:key:Left;" for i in range(32)), 110000)
# The teleport the original skipped: to zone 1's tile 10,14, two rows from the party's start in
# its tile column (10,16), at the heading kept from the zone's entry, 8000h. It is the combat
# scenario's place, two cells north of the fight (the next row, 10,15, is the road scenario's).
# The party arrives and waits.
SCENARIOS["teleport"] = world_then("90000:teleport:1,10,14,28,12,8000;", 93000)
# The road south of the start (zone 1's tile 10,15), whose trees are GOURAUD.ASM's textured
# quads (until 2026-11-02 the port stopped there, the module a stub): a teleport onto it, two
# steps, a turn left, a step, two turns right, a step.
SCENARIOS["road"] = world_then(
    "90000:teleport:1,10,15,18,30,8000;92000:key:Up;93000:key:Up;94000:key:Left;95000:key:Up;"
    "96000:key:Right;97000:key:Right;98000:key:Up;", 101000)
# A labyrinth (zone 10, mode 2), whose walls are GOURAUD's quads too: a teleport into it, two
# steps, a turn left, a step, a turn right, a step. Then the same with the party's steps at
# once (a picture only, the same states), and with autosaves on (the zone's entry saves).
SCENARIOS["labyrinth"] = world_then(
    "90000:teleport:10,10,16,21,2,0;92000:key:Up;93000:key:Up;94000:key:Left;95000:key:Up;"
    "96000:key:Right;97000:key:Up;", 100000)
SCENARIOS["labyrinth-instant"] = SCENARIOS["labyrinth"]
SCENARIO_SETS["labyrinth-instant"] = ["video.step_time=0"]
SAME_STATES_AS["labyrinth-instant"] = "labyrinth"
SCENARIOS["labyrinth-autosave"] = SCENARIOS["labyrinth"]
SCENARIO_SETS["labyrinth-autosave"] = ["saves.autosave=true", "saves.autosave_minutes=1"]
SAME_AS["labyrinth-autosave"] = "labyrinth"
# The map's hidden riddle chest (MAP.C): on the map (M), the grave key held half a second with
# the right Shift and an Alt shows the Moredhel lock vision, whose dialog waits for a sound to
# end, then the chest's word lock (CIPHER.C): Space through its dialogs, the first dial turned
# (a click), Exit (a click) and Space through the two dialogs after it, back on the map.
SCENARIOS["riddle"] = world_then(
    "90000:key:M;92000:key:Right Shift,3000;92100:key:Left Alt,2900;92200:key:`,1000;"
    "96000:key:Space;100000:key:Space;104000:key:Space;106000:click:136,93;"
    "108000:click:268,176;111000:key:Space;114000:key:Space;", 117000)
# Saving, quicksave and quickload. The Save Game dialog first (O, S: the folder "one" and the save
# "test" typed into its two fields, Save): GAMES\one.G02\SAVE01.GAM, the folder chosen. F9 then
# finds no Bookmark there and does nothing. Two steps, F5 (the Bookmark, SAVE00.GAM, without its
# question), two steps, F9, and Space through the load's map and caption: the party is back where
# F5 saved it, not where the dialog did, with the TEMP.GAM written at F5.
SCENARIOS["quicksave"] = world_then(
    "91000:key:O;94000:key:S;97000:key:T;97300:key:E;97600:key:S;97900:key:T;98500:key:Tab;"
    "99000:key:O;99300:key:N;99600:key:E;100500:click:242,158;104000:key:F9;106000:key:Up;"
    "107000:key:Up;109000:key:F5;111000:key:Up;112000:key:Up;114000:key:F9;119000:key:Space;"
    "123000:key:Space;127000:key:Space;", 131000)
# A quicksave as the first save of a fresh user folder, which has no GAMES: F5 makes
# GAMES\SAVES.G01 and saves the Bookmark there; two steps, F9, Space through the load.
SCENARIOS["quicksave-first"] = world_then(
    "90000:key:F5;92000:key:Up;93000:key:Up;95000:key:F9;"
    "100000:key:Space;104000:key:Space;108000:key:Space;", 112000)
# Autosaves leave the game where it was: the world's script, the fight and the teleport into zone
# 4 with them on (one a minute while the game's time moves) play out as without them. The world
# saves on its first pass (a chapter's start), the fight after its victory, a minute on, and the
# teleport on arriving in zone 4.
for name in ("world", "combat", "spawns"):
    SCENARIOS[f"{name}-autosave"] = SCENARIOS[name]
    SCENARIO_SETS[f"{name}-autosave"] = ["saves.autosave=true", "saves.autosave_minutes=1"]
    SAME_AS[f"{name}-autosave"] = name
# An autosave and its load. The world's first pass saves the chapter's start
# (GAMES\AUTOSAVE.G20\SAVE01.GAM); two steps and a teleport into zone 4, saved on arriving (the
# start moves to SAVE02); F5 there saves the Bookmark into a new GAMES\SAVES.G01, the same game
# as the autosave (checked). Then Restore Game (O, R): a click on the AUTOSAVE folder (the
# second of the list at 33,49, rows 14 pixels apart), one on its second save, Return, Space
# through the load's map and caption, and a step from the start.
SCENARIOS["autosave"] = world_then(
    "90000:key:Up;91000:key:Up;92000:teleport:4,10,14,14,10,0;96000:key:F5;98000:key:O;"
    "101000:key:R;104000:click:60,71;106000:click:170,71;108000:key:Return;"
    "113000:key:Space;117000:key:Space;121000:key:Space;126000:key:Up;", 130000)
SCENARIO_SETS["autosave"] = ["saves.autosave=true"]


def autosave_is_the_bookmark(user: Path) -> bool:
    """The autosave made on arriving in zone 4 holds the game F5's Bookmark saved there: the two
    files are the same past their titles."""
    folder = user / "GAMES" / "AUTOSAVE.G20"
    names = sorted(p.name.upper() for p in folder.iterdir()) if folder.is_dir() else []
    print(f"  {folder.name}: {', '.join(names) or 'nothing'}")
    auto = folder / "SAVE01.GAM"
    mark = user / "GAMES" / "SAVES.G01" / "SAVE00.GAM"
    a = auto.read_bytes() if auto.exists() else b""
    b = mark.read_bytes() if mark.exists() else b""
    title = a[:90].split(b"\0")[0].decode("latin-1")
    same = len(a) > 90 and a[90:] == b[90:]
    print(f'  SAVE01.GAM, "{title}": {"the" if same else "not the"} game of the Bookmark')
    return names == ["SAVE01.GAM", "SAVE02.GAM"] and same


SCENARIO_CHECKS["autosave"] = autosave_is_the_bookmark
# The camp (E): a click on the dial's 14:00 (ENCAMP.DAT's hour spots), and the party rests the six
# hours from 08:00, the clock's hand drawn by VGA slot 39 as it moves; a rest to a chosen hour
# ends the camp by itself. Then a step.
SCENARIOS["camp-rest"] = world_then("90000:key:E;94000:click:97,21;100000:key:Up;", 103000)
# (The lockpick screen's replay is its golden's script: dosbox_golden.py.) Spells cast by hand.
# In the world: C, the fifth symbol page (5), Scent of Sarig (Owyn's, chapter 1's only spell for
# the outdoors) and Space past its message.
SCENARIOS["cast-world"] = world_then(
    "90000:key:C;92000:key:5;94000:move:51,62;94500:click:51,62;98000:key:Space;", 101000)
# In chapter 1's first fight (the combat scenario's way in): Owyn, the fastest, acts first. C,
# the second symbol page (2), Despair Thy Eyes, and the moredhel's square clicked (its feet):
# the cast ends his turn, and Locklear's comes.
SCENARIOS["cast-combat"] = world_then(
    "90000:teleport:1,10,14,28,12,8400;91000:key:Right;"
    + "".join(f"{92000 + 400 * i}:key:Up;{92200 + 400 * i}:key:Space;" for i in range(8))
    + "96000:key:C;98000:key:2;100000:move:47,52;100500:click:47,52;102000:move:117,58;"
    "102500:hold:117,58,300;", 110000)
# Chapter 9's last cutscene, the only third part a chapter has (the golden scenario finale
# shows its first page): paged through, Space every 4 s, then the world again, in chapter 9.
SCENARIOS["finale-paged"] = world_then(
    "90000:event:7537,9;90500:event:7541,2;"
    + "".join(f"{95000 + 4000 * i}:key:Space;" for i in range(30)), 220000)
# The world's script with the CD's music on (the GOG copy's music\bakNN.ogg): the intro's, the
# book's, the cutscene's and the world's tracks.
SCENARIOS["music"] = SCENARIOS["world"]
# The main menu's track (16, CD.DAT's 44.6 s of it) played out, and the silence after.
SCENARIOS["music-menu"] = "6000:key:Escape;60000:quit"
SCENARIO_PREFS = {"music": PREFS_CD_MUSIC, "music-menu": PREFS_CD_MUSIC}
# Scenarios whose script is made from the game's data when they run.
SCENARIO_SCRIPTS: dict[str, Callable[[Path], str]] = {}
# The zone sweeps (sweep-01 to sweep-12): from the world, the party teleported to the middle
# (cell 20,20) of every tile of the zone that has a WLD file (the tiles its objects stand on),
# at 8 headings 2000h apart, a teleport every 200 ms of game time: every view the 3-D renderer
# draws there is in the frame log, and a routine the port lacks stops the run. A teleport sets
# off no encounter (steps do, and a dialog's action), so nothing waits for a key.
SWEEP_HEADINGS = range(0, 0x10000, 0x2000)


def sweep_tiles(data: Path, zone: int) -> list[tuple[int, int]]:
    """The tiles of a zone that have a WLD file (T<zone><x><y>.WLD) in the game's archive."""
    from bakbuild.commands.rmf import _load

    _, _, entries, _ = _load(data / "KRONDOR.RMF")
    prefix = f"T{zone:02d}"
    return sorted((int(e.name[3:5]), int(e.name[5:7])) for e in entries
                  if e.name.upper().startswith(prefix) and e.name.upper().endswith(".WLD")
                  and len(e.name) == 11)


def sweep_script(zone: int) -> Callable[[Path], str]:
    def script(data: Path) -> str:
        at, steps = 90000, []
        for tx, ty in sweep_tiles(data, zone):
            for heading in SWEEP_HEADINGS:
                steps.append(f"{at}:teleport:{zone},{tx},{ty},20,20,{heading:x};")
                at += 200
        return world_then("".join(steps), at + 1000)

    return script


for _zone in range(1, 13):
    SCENARIOS[f"sweep-{_zone:02d}"] = ""
    SCENARIO_SCRIPTS[f"sweep-{_zone:02d}"] = sweep_script(_zone)


# The golden scenarios of the saves. savegame: the world, the Save Game dialog, the save "test"
# in a new folder "one", and the world again; the dialog made GAMES\SAVES.G01 first, so the
# folder is ONE.G02, and the working save is the save's body.
def savegame_is_test(user: Path) -> bool:
    save = user / "GAMES" / "one.G02" / "SAVE01.GAM"
    if not save.exists():
        print(f"  no {save.relative_to(user)}")
        return False
    b = save.read_bytes()
    title = b[:90].split(b"\0")[0].decode("latin-1")
    temp = (user / "TEMP.GAM").read_bytes()
    body = temp == b[100:]
    print(f'  one.G02\\SAVE01.GAM, "{title}", {len(b)} bytes; the working save is '
          f"{'its body' if body else 'not its body'}")
    return title == "test" and len(b) == 334605 and body


SCENARIO_CHECKS["savegame"] = savegame_is_test


# load: the prelude's quicksave (two steps and a turn from the start) restored from the main
# menu: the working save after the load is its body.
def load_restored_the_quicksave(user: Path) -> bool:
    save = user.parent / "prelude" / "GAMES" / "SAVES.G01" / "SAVE00.GAM"
    temp = user / "TEMP.GAM"
    same = temp.exists() and save.exists() and temp.read_bytes() == save.read_bytes()[100:]
    print(f"  TEMP.GAM is {'the' if same else 'not the'} game of the quicksave loaded")
    return same


SCENARIO_CHECKS["load"] = load_restored_the_quicksave


def run(scenario: str, data: Path, exe: Path, out: Path, sets: list[str]) -> None:
    out.mkdir(parents=True, exist_ok=True)
    user = out / "user"
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    (user / "KRONDOR.CFG").write_bytes(SCENARIO_PREFS.get(scenario, PREFS))
    # What the golden scenario's prelude leaves for the run to start with (a save to load).
    golden = GOLDEN.get(scenario)
    if golden is not None and golden.prelude:
        for rel, src in run_prelude(str(exe), data, golden, out / "prelude").items():
            (user / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, user / rel)
    for name in LOGS:
        (out / f"{name}.log").unlink(missing_ok=True)
    (out / "last.bmp").unlink(missing_ok=True)
    script = SCENARIO_SCRIPTS[scenario](data) if scenario in SCENARIO_SCRIPTS \
        else SCENARIOS[scenario]
    if len(script) > 16000:  # more than the environment holds well: from a file
        (out / "input.txt").write_text(script.replace(";", "\n"), encoding="utf-8", newline="\n")
        script = f"@{out / 'input.txt'}"
    env = dict(os.environ, BAK_CLOCK="virtual", BAK_HEADLESS="1", SDL_VIDEO_DRIVER="dummy",
               BAK_INPUT=script, BAK_FRAME_LOG=str(out / "frames.log"),
               BAK_STATE_LOG=str(out / "state.log"), BAK_AUDIO_LOG=str(out / "audio.log"),
               BAK_DUMP_AUDIO=str(out / "audio.wav"), BAK_DUMP_FRAME=str(out / "last.bmp"))
    start = time.monotonic()
    pairs = DEFAULT_SETS + SCENARIO_SETS.get(scenario, []) + sets  # a later one wins
    settings = [arg for pair in pairs for arg in ("--set", pair)]
    r = subprocess.run([str(exe), "--data", str(data), "--user", str(user), *settings], env=env,
                       capture_output=True, text=True, check=False,
                       timeout=3600 if scenario in SCENARIO_SCRIPTS else 900)
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


def compare(baseline: Path, current: Path, what: str, untimed: bool = False) -> bool:
    """Whether two logs hold the same lines; untimed: each line's first field (its time) set
    aside."""
    a = baseline.read_text(encoding="utf-8").splitlines() if baseline.exists() else []
    b = current.read_text(encoding="utf-8").splitlines() if current.exists() else []
    if untimed:
        a = [line.split(" ", 1)[1] for line in a]
        b = [line.split(" ", 1)[1] for line in b]
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
    base = OUT_DIR / SAME_AS.get(args.scenario, SAME_STATES_AS.get(args.scenario, args.scenario))
    if args.cmd == "record" and base != out:
        print(f"{args.scenario} is checked against {base.name}'s baseline: record that one")
        return 2
    run(args.scenario, Path(args.data), Path(args.exe), out, args.set)
    checked = SCENARIO_CHECKS[args.scenario](out / "user") if args.scenario in SCENARIO_CHECKS \
        else True
    if args.cmd == "record":
        if not checked:
            print("  not recorded: the run fails its check")
            return 1
        for name in LOGS:
            if (out / f"{name}.log").exists():
                shutil.copyfile(out / f"{name}.log", out / f"baseline-{name}.log")
        print(f"  recorded as the baseline in {out}")
        return 0
    if base != out:
        print(f"  against {base.name}'s baseline")
    if args.scenario in SAME_STATES_AS:
        ok = compare(base / "baseline-state.log", out / "state.log", "states", untimed=True)
        return 0 if ok and checked else 1
    ok = [compare(base / f"baseline-{name}.log", out / f"{name}.log", f"{name} log") for name in LOGS]
    return 0 if all(ok) and checked else 1


if __name__ == "__main__":
    sys.exit(main())
