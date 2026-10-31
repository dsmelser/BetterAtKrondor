# Port status

Where the native port stands. Update this file in the same commit as the work it describes.

## Milestone 1: boot to the main menu (Windows x86)

| Step | Status |
|---|---|
| Fork docs and rules (`CLAUDE.md`, `README.md`, `CONTRIBUTING.md`) | ✅ |
| Explicit integer widths (`s16/u16/s32/u32`, `port/tools/retype16.py`) | ✅ verified byte-identical for 1.00 and 1.02 by `reference.yml` |
| Reference build in CI (`reference.yml`): objects, maps and binaries of `upstream-snapshot` | ✅ |
| Native build skeleton: CMake presets, `bak_native.h`, Borland RTL shims, generated ASM stubs | ✅ all 188 C modules compile (clang-cl x86) and link |
| DOS code on the boot path rewritten natively (BIOS checks, DOS memory, EMS, config) | ✅ |
| Native VGA driver bound by `video_init` | ⬜ **next: boot stops here** |
| SDL3 platform layer | 🚧 window + present, VGA register file, segment table, DOS paths; timer and input ⬜ |
| Boot-path ASM in C (codecs, `FARPTR`, `RAND`, `DOSRW`, `PALDAC`, ...) | ⬜ |
| Differential test harness (Unicorn) | ⬜ |
| Boots the user's data to the main menu | ⬜ (needs the GOG copy) |

**Current behaviour.** With the game files present, `betterkrondor --data <dir>` opens its window, enters the
game, parses `resource.cfg` and `drive.cfg`, and stops at `video_init` with "Not implemented yet". Without
them it says where it looked. CI runs exactly this smoke test against placeholder files.

## Native-only code in the game sources

Each site is an `#ifdef BAK_NATIVE` branch next to the untouched original, or one of the idioms in
`bak/INCLUDE/gtypes.h` (`NearPtr`, `FP_OFF_ADD`, `RETURN_AX`) that expand to the original under Borland.

| File | Native branch |
|---|---|
| `SYS/BIOSCHK.C` | Tandy ROM probe: never a Tandy |
| `SYS/DOSMEM.C` | `alloc_far` / `_freemem` on host memory; the free-memory query reports 640 KB |
| `SYS/EMSDET.C`, `SYS/EMS.C` | INT 67h calls go to the emulated LIM EMS (8 MB, exact copy-in/copy-out paging) |
| `SYS/CFGPARSE.C` | writable buffer for the drive prefix the parser overwrites |
| `GFX/DRIVER/VIDMODE.C`, `VIDINT10.C` | BIOS equipment-word video bits emulated; text-mode reset is a no-op |
| `GFX/SCREEN/SCREEN.C` | VGA latch copy as a page-to-page row copy |
| `IO/RESOURCE.C` | prototype for the INT 24h handler (never raised natively) |
| `STREAM/RESLOAD/FONTLOAD.C`, `STREAM/BUFLOAD/CHUNKRD.C`, `DIALOG/DIALOG.C`, `SCRIPT/TTM.C` | small asm sequences in C |
| `GFX/DRIVER/VIDINIT.C` `video_init` | ⬜ trap: bind the native driver |
| `GFX/SPRITE/RECTSPR.C`, `GFX/SPRITE/STRBLIT.C`, `GFX/FONT/FONT.C` fast path | ⬜ traps: driver slots called with register arguments |

## ASM modules

All 50 TASM modules of the 1.02 link are generated placeholders (`port/tools/gen_asm_stubs.py`):
routines abort with their name, and the data the C code uses is zero-initialized (16 `*_data.c` files)
until the module's `db`/`dw`/`dd` tables are converted. Re-run the generator after adding headers; it never
overwrites a file whose GENERATED marker has been removed.

## What the port has taught us about the original

- **BC++ 2.0 promotes `unsigned short` to signed `int`.** `SHAPETBL.C` (one of the 13 modules built
  with BC++ 2.0) compiles `i < g_spriteBankCount` to a signed `JL` with `unsigned short`, but to `JB` with
  `unsigned int`. BC++ 3.0/3.1 treat the two alike. Promotion semantics therefore differ by compiler
  band (`X_*` bands in `bak/RECIPES.MAK`).
- **The original writes into string literals** (`CFGPARSE.C`'s `g_cfgResourceDrivePrefix = "x:"`).
  DOS had no memory protection; natively literals are read-only, so each such site needs a writable buffer.
  Expect more.
- **The EMS globals are misnamed.** In `SYS/EMSDET.C`, `g_ems_free_pages` receives the page-frame segment
  (INT 67h AH=41h), `g_ems_total_pages` the free page count (AH=42h) and `g_ems_frame_segment` the handle
  (AH=43h). Not renamed yet.
- **Near pointers live in 16-bit tables** (`PALDRV.C`, `VIDDRV.C`, `ADSCRIPT.C`, `FONTLOAD.C`, now
  `NearPtr`). `g_apVideoDriverChunkNames[0]` is a raw DGROUP offset (`0x003a`), not a symbol.
- **File opens are unchecked.** A missing `drive.cfg` (1.02) or `resource.cfg` passes a NULL `FILE *` to
  `fscanf`; natively the C runtime's invalid-parameter handler reports it.

## Known divergences from the original

None in behaviour yet. Placeholder values to revisit: the ASM data tables (zero), the emulated DOS free
memory (640 KB) and EMS size (8 MB).
