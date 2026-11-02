# CLAUDE.md

BetterAtKrondor is a hard fork of
[canassa/betrayal-at-krondor](https://github.com/canassa/betrayal-at-krondor), the
byte-perfect reconstruction of *Betrayal at Krondor* (1993, DOS), turning it into a
**native SDL3 port**. Upstream's "stay byte-identical" rule does **not** apply on `main`, and
byte-identity is no goal at all: the reconstruction was the starting point, the real source.
The aim is a game that looks and feels modern, by whatever means. There is no exact "Original"
mode to keep, but the game's rules stay the original's (see Rules); behaviour is checked
against the original where code still mirrors it (see Verification).

## Layout

| Path | What |
|---|---|
| `bak/SRC/`, `bak/INCLUDE/` | The reconstructed game. The C compiles natively; the TASM modules stay as the reference for their C replacements. |
| `port/` | Everything native: `include/bak_native.h` (force-included), `compat/` (Borland RTL), `platform/` (SDL3), `drivers/`, `asm/` (C replacements for the in-EXE ASM), `tests/`, `tools/`. |
| `cli/`, `viewer/` | Upstream's Python CLI and browser asset viewer. `uv run bak rmf` and the viewer decode game data. `bak build` (the Borland build) only works at the `upstream-snapshot` tag and in CI (`.github/workflows/reference.yml`). |
| `docs/PORT_STATUS.md` | What is ported, stubbed, or known to diverge. Keep it current. |

## Commands

```
port\tools\win-build.cmd [preset]                 # Windows: VS env + configure + build (default win-x86-clangcl)
build\win-x86-clangcl\betterkrondor.exe --data <game dir> [--user <dir>] [--set section.key=value]   # user folder: %APPDATA%\BetterAtKrondor
port\tools\win-build.cmd win-x86-clangcl-asan     # AddressSanitizer: finds buffers sized for 16-bit pointers
uv run --group difftest pytest -q port/tests       # differential tests (needs work/ref, see port/tests/README.md)
build\win-x86-clangcl\bak_unittest.exe            # unit tests of the port's own code (no game data)
uv run python -I port/tools/replay.py check world --data <game dir>   # replay on the virtual clock vs its baseline (record first)
uv run python -I port/tools/dosbox_golden.py run main-menu --data <game dir>   # the original in DOSBox Staging vs the port
python -I port/tools/gen_asm_stubs.py              # regenerate placeholders for unported ASM modules
python -I port/tools/asm_publics.py                # inventory of the in-EXE ASM modules
python -I port/tools/ptrwidth_audit.py             # 16-bit pointer habits the diagnostics miss (CI)
python -I port/tools/warning_ratchet.py [--update] # no warning category may grow (CI)
gh workflow run reference.yml -f ref=<ref>         # Borland build of <ref>: the reference objects (test fixtures)
```

Environment: `BAK_HEADLESS=1` (no dialogs; errors to stderr), `BAK_TRACE=1` (platform milestones),
`SDL_VIDEO_DRIVER=dummy` (no window), `BAK_DUMP_FRAME=<file.bmp>` (the picture shown, rewritten whenever it
changes), `BAK_DUMP_CURSOR=<file.bmp>` (each system cursor made, on a real window), `BAK_DUMP_FRAMES=<dir>` (every new picture, numbered as the frame log's lines),
`BAK_DUMP_MIDI=<file>` (the MIDI bytes sent to the MPU-401), `BAK_DUMP_AUDIO=<file.wav>` (the sound mixed, on the
virtual clock rendered as the clock moves), `BAK_AUDIO_LOG=<file>` (what the sound was told: the CD's plays and
stops, then the WAV's hash),
`BAK_INPUT="6000:key:Escape;9000:click:160,100"` (scripted keys and clicks, ms from start-up:
`port/platform/inputscript.c`), `BAK_CLOCK=virtual` (a clock that moves only as the game runs: scripted runs
repeat exactly and take seconds), `BAK_FRAME_LOG=<file>` (a hash of each picture shown),
`BAK_STATE_LOG=<file>` (hashes of the game state on each world-loop pass) and `BAK_PARCHMENT=<n>` (where the
dialogs' parchment pattern starts, 3D40h-3DA3h, instead of the cosmetic stream's draw: the goldens pin it to
the original's). Crashes print a symbolized stack
on Windows. Unprompted, the intro and credits loop like the original's attract mode; Escape skips them.

## Rules

- **Modern by default.** An improvement may become the default and needs no toggle. The
  game's rules stay those of the release whose data it runs (1.00 or 1.02): combat maths,
  stats, encounters, dialogs, time. Its bugs get fixed, each listed in
  `docs/PORT_STATUS.md`. New controls may change how the party moves, as long as time,
  encounters and triggers advance as the original's steps do.
- **Saves stay DOS-compatible**, both ways: the original's saves load natively, and the
  port's load in DOS. Data only the port uses goes in files of its own.
- **Rewrite freely.** Any subsystem (renderer, UI, main loop, audio) may be replaced
  outright; the reconstruction is the reference for behaviour. `main` need not build with
  Borland, so change `bak/` directly, with no `#ifdef BAK_NATIVE` branch and no
  Borland-neutral idiom (`gtypes.h`).
- **Mechanical vs semantic.** Sweeps (retyping, renames, removing `far`) never share a
  commit with behaviour changes, so a regression bisects to one kind of change.
- **Integer widths are explicit.** Game code uses `s16`/`u16`/`s32`/`u32`
  (`bak/INCLUDE/gtypes.h`): the original `int` was 16 bits and `long` 32. Use plain `int`
  only at host boundaries (SDL, libc callbacks, `main`). Natively, 16-bit operands promote
  to 32-bit `int`, so when touching an expression check `-`, comparisons, `~`, `>>`, `/` and
  `%` on `u16` values.
- **Verification follows longevity.** Code that stays a translation of an original routine
  (codecs, the sequencer, sound drivers, R3D while it draws) is verified against that routine
  before it replaces a stub: the Unicorn harness in `port/tests/`, fed by the reference
  build's objects, with the test mutation-checked by `port/tools/mutate.py`. Code that a new
  subsystem will replace gets a plain native version, checked by a DOSBox golden of its
  screen. New code gets tests of its own.
- **No game data, ROMs or upscaled art in the repo.** Users supply their own copy; HD art
  packs are generated locally from it.
- **Names from evidence.** Many names in `bak/SRC` are decompiler guesses. Rename or
  document game code only from reading the code and the data. Code under `port/` is
  commented normally.
- `NULL` for pointers, never bare `0`.

## Naming conventions (inherited from upstream)

| Kind | Convention | Example |
|---|---|---|
| Trivial variable | one letter | `i`, `n`, `c` |
| Local variable | `camelCase` | `count`, `curSlot` |
| Global variable | `g_` + `camelCase` | `g_modelTables` |
| Const / macro | `SCREAMING_SNAKE_CASE` | `MAX_ACTORS` |
| Struct / enum | `PascalCase` | `ModelTable` |
| Struct member | `camelCase` | `lodCount` |
| Enum value | `SCREAMING_SNAKE_CASE` | `LOC_CHEST` |
| Public function | `module_snake_case` | `pagedrec_get` |
| Private function | `static snake_case` | `static build_lod` |

## Verification

- **Reference build** (CI, manual): `reference.yml` builds a ref with the period Borland
  toolchain, verifies the binaries by sha256, and uploads every `.OBJ`, map and binary: the
  original routines the differential tests run. Build `upstream-snapshot` or an older ref
  for them; byte-neutrality of `main` is not checked.
- **Differential tests**: original routine (from the reference objects) vs its C
  translation, for code that stays a translation.
- **Black box**: the original EXE from the user's copy, running in DOSBox Staging.
  `port/tools/dosbox_golden.py` runs it headless next to the port and compares the pictures
  of screens the port still draws as the original does (the 1× pages of the 2-D screens).
- **Replays**: `port/tools/replay.py` runs a scenario on the virtual clock and compares its
  frame and state logs with a recorded baseline (`work/replay/`). A mechanical change must
  leave both identical; a change to the presentation only must leave the state log identical.
- **Unit tests** (`port/tests/unit`, `bak_unittest`) for the port's own code; CI runs them.
