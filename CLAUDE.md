# CLAUDE.md

BetterAtKrondor is a hard fork of
[canassa/betrayal-at-krondor](https://github.com/canassa/betrayal-at-krondor), the
byte-perfect reconstruction of *Betrayal at Krondor* (1993, DOS), turning it into a
**native SDL3 port**. Upstream's "stay byte-identical" rule does **not** apply on `main`, and
byte-identity is no goal at all: the reconstruction was the starting point, the real source.
The aim is a game that looks and feels modern, by whatever means. Behaviour is checked against
the original instead (see Verification).

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
build\win-x86-clangcl\betterkrondor.exe --data <game dir> [--user <save dir>]
port\tools\win-build.cmd win-x86-clangcl-asan     # AddressSanitizer: finds buffers sized for 16-bit pointers
uv run --group difftest pytest -q port/tests       # differential tests (needs work/ref, see port/tests/README.md)
uv run python -I port/tools/dosbox_golden.py run main-menu --data <game dir>   # the original in DOSBox Staging vs the port
python -I port/tools/gen_asm_stubs.py              # regenerate placeholders for unported ASM modules
python -I port/tools/asm_publics.py                # inventory of the in-EXE ASM modules
gh workflow run reference.yml -f ref=<ref>         # Borland build of <ref>: the reference objects (test fixtures)
```

Environment: `BAK_HEADLESS=1` (no dialogs; errors to stderr), `BAK_TRACE=1` (platform milestones),
`SDL_VIDEO_DRIVER=dummy` (no window), `BAK_DUMP_FRAME=<file.bmp>` (each presented frame),
`BAK_DUMP_MIDI=<file>` (the MIDI bytes sent to the MPU-401), `BAK_INPUT="6000:key:Escape;9000:click:160,100"`
(scripted keys and clicks, ms from start-up: `port/platform/inputscript.c`). Crashes print a symbolized
stack on Windows. Unprompted, the intro and credits loop like the original's attract mode; Escape skips them.

## Rules

- **Faithful by default.** At default settings the port must behave like the original
  release whose data it runs (1.00 or 1.02). Every enhancement is a runtime toggle, and the
  "Original" setting stays exact.
- **Change the game code directly.** `main` need not build with Borland, so a change to
  `bak/` needs no `#ifdef BAK_NATIVE` branch and no Borland-neutral idiom (`gtypes.h`); the
  existing ones can stay or go.
- **Mechanical vs semantic.** Sweeps (retyping, renames, removing `far`) never share a
  commit with behaviour changes, so a regression bisects to one kind of change.
- **Integer widths are explicit.** Game code uses `s16`/`u16`/`s32`/`u32`
  (`bak/INCLUDE/gtypes.h`): the original `int` was 16 bits and `long` 32. Use plain `int`
  only at host boundaries (SDL, libc callbacks, `main`). Natively, 16-bit operands promote
  to 32-bit `int`, so when touching an expression check `-`, comparisons, `~`, `>>`, `/` and
  `%` on `u16` values.
- **ASM replacements are verified** against the original routine (the Unicorn harness in
  `port/tests/`, fed by the reference build's objects) before they replace a stub.
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
- **Differential tests**: original routine (from the reference objects) vs its C translation.
- **Black box**: the original EXE from the user's copy, running in DOSBox Staging.
  `port/tools/dosbox_golden.py` runs it headless next to the port and compares the pictures.
