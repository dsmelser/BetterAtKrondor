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
| Native VGA driver bound by `video_init` | ✅ `port/drivers/vga.c`; slots ported as the boot path reaches them (below) |
| SDL3 platform layer | ✅ window + present, VGA registers + 70 Hz retrace, chunky VRAM, segment table, DOS paths, INT 33h mouse, keyboard, timer, BIOS data area (sound ⬜) |
| Boot-path ASM in C (codecs, `FARPTR`, `RAND`, `DOSRW`, `PALDAC`, ...) | 🚧 `RAND`, `VIDDET`, `PIXEL`, `MOUSE`, `FARPTR`, `LZWDEC`, `KEYBOARD`, `TIMER`, `INT00`, the vtable thunks; **next: `CDAUDIO.ASM`** |
| Differential test harness (Unicorn, `port/tests`) | ✅ originals run from the reference EXE + map; CI runs it |
| Boots the user's data to the main menu | ⬜ |

**Current behaviour.** With the game files present, `betterkrondor --data <dir>` opens its window, enters the
game, parses `resource.cfg` and `drive.cfg`, binds the native VGA driver, installs mouse, keyboard and timer, loads the game font and shows the
"Loading Betrayal at Krondor... please wait." screen, then stops at the 1.02 CD-drive check
(`CDAUDIO.ASM`) with "Not implemented yet". Without them it says where it looked. CI runs exactly this smoke test against placeholder files. Development runs
against a GOG copy of 1.02 (`KRONDOR.EXE` matches upstream's 1.02 hash).

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
| `GFX/DRIVER/VIDINIT.C` `video_init`, `VIDDRV.C` `video_driver_bind` | the built-in driver replaces loading the VMCODE.OVL chunk; same mode remapping and context setup |
| `GFX/FONT/FONT.C` fast path | slot 1 called with its registers as arguments |
| `GEN/GFXCTX.H`, `GEN/RNDVTBL.H` | the original's labels inside the graphics-context block and the vtable name the same storage |
| `GFX/SPRITE/RECTSPR.C`, `GFX/SPRITE/STRBLIT.C` | ⬜ traps: driver slots called with register arguments |

## ASM modules

The 50 TASM modules of the 1.02 link start as generated placeholders (`port/tools/gen_asm_stubs.py`):
routines abort with their name, and the data the C code uses is zero-initialized (16 `*_data.c` files)
until the module's `db`/`dw`/`dd` tables are converted. Re-run the generator after adding headers; it never
overwrites a file whose GENERATED marker has been removed.

A module leaves the placeholder state only with a differential test (`port/tests/test_<module>.py`)
that runs the original routines from the reference build against the translation.

| Module | Translation | Verified by |
|---|---|---|
| `SYS/RAND.ASM` | `port/asm/rand.c` | `test_rand.py`: seed table, cold start, 2000 draws after reset, random states |
| `GFX/DRIVER/VIDDET.ASM` | `port/asm/viddet.c` | `test_viddet.py`: every requested mode under every auto-detect state, on a modelled VGA machine; shutdown |
| `GFX/RASTER/PIXEL.ASM` | `port/asm/pixel.c` | `test_pixel.py`: the clipping decision, against probes in the pixel slots |
| `GEN/GFXCTX.ASM` | `port/asm/gfxctx_data.c` | `test_vga.py::test_context_initial_values`: the whole 2742-byte block |
| `GEN/RNDVTBL.ASM` | `port/asm/rndvtbl_data.c` | data only: every slot starts at `gfx_retf_stub` |
| `GFX/DRIVER/VTHUNKS.ASM`, `GFX/RASTER/PRESENT.ASM`, `GFX/DRIVER/CGAPASTE.ASM` | `port/asm/vthunks.c`, `present.c`, `cgapaste.c` | no logic: each routine forwards to a vtable slot |
| `INPUT/MOUSE.ASM` | `port/asm/mouse.c`, `mouse_data.c` | `test_mouse.py`: INT 33h call sequences of install/clip/restore/set-position, queries, the event callback's register save/restore and latch parking on a planar VGA |
| `SYS/FARPTR.ASM` | `port/asm/farptr.c` | `test_farptr.py`: fills and copies across chunk and 64 KB boundaries, overlapping copies at both parities, pointer compare and normalize |
| `INPUT/KEYBOARD.ASM` | `port/asm/keyboard.c`, `keyboard_data.c` | `test_keyboard.py`: the tables; 1500-byte random key sequences through the INT 9 handler (BIOS shift state and buffer, key states, channels) with and without the Tandy paths; `kbhit_read`; queries; the INT 1Ch axis tick; install/restore |
| `INPUT/TIMER.ASM` | `port/asm/timer.c`, `timer_data.c` | `test_timer.py`: registry alloc/free; the order of callbacks over runs of IRQ 0 ticks and the BIOS chaining; install/restore |
| `INPUT/INT00.ASM` | `port/asm/int00.c` | `test_int00.py`: the handler's retry rule on 2000+ overflowing DIV/IDIV, against `bak_div8`/`bak_idiv8`; install/restore |
| `STREAM/CODEC/LZWDEC.ASM` | `port/asm/lzwdec.c` | `test_lzw.py`: memory streams read through `stream_read` in random chunk sizes (suspend and resume mid-string), random, text-like, run-heavy and image-like data, resets, truncated input; the output must also equal the encoded input |

## Video drivers (VMCODE.OVL)

The VGA chunk (`DRIVERS/VMCODE/VGA.ASM`) is `port/drivers/vga.c`. Display memory is the 256 KB chunky
model of `bak_video.h`: planar byte (offset o, plane p) is element 4o + p, so pages, overruns and the CRTC
start address behave as in Mode Y. `test_vga.py` runs the reference build's `VGA.EXE` on a planar VGA
model (`harness/vga.py`) and compares display memory, the graphics-context block, the DAC, the CRTC start
address and the palette cache after every call.

| Slots | Status |
|---|---|
| init, 1 glyph, 4 page copy, 5/6/7 save/size/paste rectangle, 8 flip + retrace, 13/14/15 image install/decode/nibbles, 20 palette, 21/22 pixels, 34/35 DAC blend/upload | ✅ verified |
| 24 rect-buffer offset, 32 clear sprite cache | ported; verified with their consumers (23, 33) |
| 2, 3, 10, 11, 17-19, 23, 27, 29-31, 33, 36-40 | ⬜ trap with the routine's name |
| 9, 12, 16, 25, 26, 28, 41-48 | empty in the original: no-ops |

The other adapters' chunks are not ported; the books' 640x350 HEG driver is the next one needed (M2).

## What the port has taught us about the original

- **BC++ 2.0 promotes `unsigned short` to signed `int`.** `SHAPETBL.C` (one of the 13 modules built
  with BC++ 2.0) compiles `i < g_spriteBankCount` to a signed `JL` with `unsigned short`, but to `JB` with
  `unsigned int`. BC++ 3.0/3.1 treat the two alike. Promotion semantics therefore differ by compiler
  band (`X_*` bands in `bak/RECIPES.MAK`).
- **The original writes into string literals** (`CFGPARSE.C`'s `g_cfgResourceDrivePrefix = "x:"`;
  `resblit_load_asset_table` turns every `"name.bmp"` it is given into `"name.bmx"` in place). DOS had no
  memory protection and Borland's `-d` (merge duplicate strings) is off, so every literal is its own
  writable copy. The game library is compiled the same way (`/GF-`, `-fwritable-strings`).
- **The EMS globals are misnamed.** In `SYS/EMSDET.C`, `g_ems_free_pages` receives the page-frame segment
  (INT 67h AH=41h), `g_ems_total_pages` the free page count (AH=42h) and `g_ems_frame_segment` the handle
  (AH=43h). Not renamed yet.
- **Near pointers live in 16-bit tables** (`PALDRV.C`, `VIDDRV.C`, `ADSCRIPT.C`, `FONTLOAD.C`, now
  `NearPtr`). `g_apVideoDriverChunkNames[0]` is a raw DGROUP offset (`0x003a`), not a symbol.
- **`fmemcpy_far` never copies its alignment byte.** It tests the destination's parity with `TEST` then
  `JNC`, and `TEST` always clears CF, so it always copies words from the start. Only overlapping copies can
  tell; the translation keeps it.
- **File opens are unchecked.** A missing `drive.cfg` (1.02) or `resource.cfg` passes a NULL `FILE *` to
  `fscanf`; natively the C runtime's invalid-parameter handler reports it.

## Platform services

| Service | Implementation |
|---|---|
| Interrupts | `port/platform/machine.c`: host events and a virtual 8253 become interrupts when the game polls (`bak_poll`: its wait loops, input and timer reads, retrace waits). Vectors 08h/09h/1Ch are a table the install routines replace and chain through; the BIOS INT 8 handler counts time and calls 1Ch. Reading the variables the timer advances (`g_nFrameTickCountdown`, `g_timer_ticks`, `g_dwSysTickCount`) polls first, so bare spins on them work |
| INT 33h mouse | `port/platform/mouse.c`: the host pointer's position over the picture, in the ranges the game sets (four units per pixel); the driver calls the game's handler as an interrupt |
| Keyboard | `port/platform/scancode.c`: host keys as scan code set 1 bytes (E0h-prefixed for the 101-key additions), one IRQ 1 each, read by the game's handler at port 60h |
| BIOS data area | `bak_bios_data()`, also at segment 0040h: keyboard buffer and shift state, CRTC port, time of day |

## Known divergences from the original

- **ROM 8x8 font.** `video_init` installs the BIOS 8x8 font as font slot 1; natively its glyphs are
  blank. The game switches to `game.fnt` before drawing text, so only the slot numbering matters.
- **Mode 13h default palette.** The BIOS mode set loads a default DAC palette; natively the DAC keeps its
  contents. The game sets its own palette before anything is visible.
- **Degenerate driver calls.** Where a zero count makes the original's `LOOP` run 65536 times (a rectangle
  0 pixels wide, a 0-colour palette upload or blend) or the original miscomputes an address (an image whose
  packed size is a multiple of 64 KB), the native driver does nothing or stops with a message.
- **Slack after DOS blocks.** Every emulated DOS block is followed by 4 KB of slack, so the original's
  overruns past a block (the LZW decoder can spell long strings beyond its scratch area) stay in memory the
  game owns instead of reaching the host heap.
- **Division the divide-error handler cannot resolve.** Where the original hangs (DIV by zero, or an
  unsigned overflow its arithmetic shift cannot shrink), the translated code stops with a message.
- **Memory past the VGA window** (A000:0000 + 64 KB) reads as FFh and ignores writes, as on a VGA-only PC.

Placeholder values to revisit: the remaining ASM data tables (zero), the emulated DOS free memory
(640 KB) and EMS size (8 MB).
