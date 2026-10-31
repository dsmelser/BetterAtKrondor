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
| SDL3 platform layer | ✅ window + present, VGA registers + 70 Hz retrace, chunky VRAM, segment table, DOS paths, INT 33h mouse, keyboard, timer, BIOS data area, MPU-401 (MIDI bytes not played yet) |
| Boot-path ASM in C (codecs, `FARPTR`, `RAND`, `DOSRW`, `PALDAC`, ...) | 🚧 `RAND`, `VIDDET`, `PIXEL`, `MOUSE`, `FARPTR`, `LZWDEC`, `KEYBOARD`, `TIMER`, `INT00`, `CDAUDIO`, `MIDI`, `SNDDRV`, `MUSDISP`, `DISP35E0`, `POLYRAST`, `DRAWLINE`, `DRAWRECT`, `R3DTRIG`, `VGABLIT` (span copiers), the vtable and sprite thunks |
| Native sound driver bound to the loaded SX.OVL chunk | ✅ MT-32 (`port/drivers/m32.c`); the others stop with the driver's name |
| Differential test harness (Unicorn, `port/tests`) | ✅ originals run from the reference EXE + map; CI runs it |
| Boots the user's data to the main menu | ✅ intro, credits, main menu; Quit To DOS, Preferences and Contents work and the game quits cleanly; Start New Game goes on into chapter 1 (M2). At 4bd88e5 native CI is green (106 differential tests) and the reference build is byte-identical for 1.00 and 1.02 |
| Golden screenshot of the main menu against the original in DOSBox Staging | ✅ identical pixel for pixel, cursor included (`dosbox_golden.py run main-menu`, below) |

**Current behaviour.** `betterkrondor --data <dir>` first checks that the folder holds the game: `KRONDOR.RMF`
must be a resource index and the archive it names must be present, since the game uses whatever it failed to
load. Then it parses `resource.cfg` and `drive.cfg`, binds the native VGA driver, installs mouse, keyboard
and timer, loads the game font, shows the "Loading Betrayal at Krondor... please wait." screen, finds no CD
drive (MSCDEX is absent), installs the MT-32 driver and the sequencer (the bank's 71 SysEx messages go out
through the emulated MPU-401), reads its menu pages (`REQ_*.DAT`), composes the frame screens with the
party's portraits for EMS, plays the intro to its end and scrolls the credits, starting over until a key
or a click (as the original does), then shows the main menu with its buttons and the mouse cursor. Quit To
DOS asks for confirmation (Yes quits cleanly, No returns), Preferences and Contents open their screens and
come back, and Start New Game fades into chapter 1, whose opening book pages through with keys or
clicks; the chapter cutscene after it plays to its end, the new game's state is loaded, and setting up
the 3D world stops (below).
The whole tour runs clean under the ASan preset. The MIDI the game sends is not played yet:
`BAK_DUMP_MIDI=<file>` writes it to a file. Development runs against
a GOG copy of 1.02 (`KRONDOR.EXE` matches upstream's 1.02 hash), configured for the MT-32; CI has no game data
and checks that an incomplete folder is turned down cleanly.

**Beyond the main menu** (M2):

1. **After the book**: the book pages through with keys or clicks and the chapter cutscene plays to its
   end (chapter one's map and title); the game then copies `STARTUP.GAM` to `TEMP.GAM` (`savegame_read`,
   byte for byte), reads the party from it and stops setting up the 3D world: `ts_init` calls R3D.ASM's
   `r3d_light_set_direction` (M3's R3D core).
2. **The game state**: see `TEMP.GAM` below; the new game's start reads the party through it.
3. **Records read straight into structs with pointers.** Menu pages are done (`menupage_load`, below).
   Other loaders that `res_fread` a struct holding pointers need the same treatment, as the plan foresaw
   for saves. A common case: a table
   of 16-bit string offsets read over a pointer table and rebased with `+= (s16)blob` (`CREDITS.C`,
   `NAMEDTBL.C` and `ASKABOUT.C` are done; `DLGWIDG.C`, `CSPELL.C`, `CBENC.C`, `CACTOR.C` remain).
4. **Pointer tables and fields sized for 2-byte pointers**, like `resblit_load_asset_table`'s (the dialog
   sprite cache in `ASKABOUT.C` still keeps them in 16 bits): the ASan preset reports them at the first
   write.

## TEMP.GAM and saves

`TEMP.GAM`, the working save (a save is a 100-byte header and a copy of it), keeps the original's 16-bit
layout exactly: it stores absolute offsets into itself (the objfixed directory points at the actor pool at
3A791h), and DOS saves must load natively and native saves in DOS. Its region offsets (`GAM_*` in
`GSTATE.H`) come from the records' sizes there (`GAM_SIZEOF_*`). Three of its records hold near pointers, 2
bytes on disk and 4 natively: `GameState` (AD7h on disk, AEFh natively), `Combatant` (5Fh, 63h) and
`CombatantState` (16h, 18h); the others have none and keep their size. The three are read and written
field by field (`gstate_temp_read_gamestate` and the others in `GSTATE.C`). No loader uses a stored pointer:
the game sets each one again after a read, and the party's `inner` is always 0 on disk. So pointers, the far
ones too, read back as NULL and are written as 0, where DOS writes whatever they held. `test_gstate.py`
checks the native fields against the original's offsets and the round trip.

## Native-only code in the game sources

Each site is an `#ifdef BAK_NATIVE` branch next to the untouched original, or one of the idioms in
`bak/INCLUDE/gtypes.h` (`NearPtr`/`NearWord`, `NEAR_PTR_SIZE`/`NEAR_PTR_SHIFT` for tables of near pointers,
`RECORD_SIZE` for records holding them, `FP_OFF_ADD`, `RETURN_AX`, `NEAR_STR` for a string pointer that may be
null) that expand to the original under Borland.

| File | Native branch |
|---|---|
| `SYS/BIOSCHK.C` | Tandy ROM probe: never a Tandy |
| `SYS/DOSMEM.C` | `alloc_far` / `_freemem` on host memory; the free-memory query reports 640 KB |
| `SYS/EMSDET.C`, `SYS/EMS.C` | INT 67h calls go to the emulated LIM EMS (8 MB, exact copy-in/copy-out paging) |
| `SYS/CFGPARSE.C` | writable buffer for the drive prefix the parser overwrites |
| `SCRIPT/ADSCRIPT.C`, `SCRIPT/ANIMSCR.C`, `INCLUDE/structs.h` | the animation scripts' object nodes, slots and operand lists sized with `RECORD_SIZE`/`NEAR_PTR_SIZE`; `Slot`'s node-chain head and operand-list fields are `NearPtr` |
| `SCRIPT/TTM.C`, `SCRIPT/TTMDLG.C`, `DIALOG/DIALOG.C`, `GFX/SPRITE/SPRTHNKS.H`, `INCLUDE/structs.h` | a script object's image tables (`pAhPagedImage`, `NearPtr`) and the sprite pointers taken from them (`NearWord`, or a native local where the original reuses a 16-bit parameter); the EMS page's segment with `FP_SEG` |
| `STREAM/RESLOAD/IMGLOAD.C`, `IMGLOAD.H` | `free_image_record` frees the image block through `MK_FP` (the original builds the far pointer as segment << 16) and reads the table's records pointer at native width. The file's own loader (`BOOK.BMX`, and EMSIMG.C's combat images) builds its NULL-terminated table of record pointers at native width (`NearPtr`); the original holds them as 16-bit near pointers |
| `DIALOG/ASKABOUT.C` | KEYWORD.DAT's 16-bit string offsets stay offsets and are resolved where a keyword is read (`KEYWORD`); the scratch menu's page and entries are cleared and sized at native size (`RECORD_SIZE`); a party member's name is read by its field, not by its offset in the game state; the dialog box comes back in a real four-word block (below) |
| `SCREENS/CREDITS.C` | `credits_load` reads CRED.DAT's 16-bit string offsets aside and builds the pointer table from them |
| `SCREENS/BOOKVIEW.C` | `bookview_init` allocates `BookViewerState` with `RECORD_SIZE`: 100 bytes in the original, 102 natively (`pImageRecord` is a near pointer) |
| `UI/NAMEDTBL.C` | label tables (`LBL_*.DAT`) are loaded into a handle with the string pool and the labels' pointers; the original rebases the records' 16-bit offsets in place and frees the pool as the lowest of them |
| `GAME/STATE/GSTATE.C` | event 7535h (the chapter's default speaker) in chapter 0, before a game is loaded, gives what the original reads before its table (below) |
| `GAME/STATE/GSTATE.C`, `GSTATE.H`, `SYS/BOOT.C`, `SCREENS/MAINMENU.C`, `COMBAT/ENC/CBENC.C`, `GAME/ENC/RGNENC.C`, `COMBAT/ACTOR/CACTOR.C` | `TEMP.GAM`'s records with near pointers are converted field by field (below); P1.DAT's `CombatantState`s too |
| `COMBAT/ENC/CBENC.C`, `CBENC.H`, `SCREENS/CMBINV.C` | encounter pools sized with `RECORD_SIZE` (7 `Combatant`s, `CombatantState`s, `AnimSlot`s); `g_inner_pool_B` is a `NearPtr`; a party slot's `Combatant` taken as `characters[slot - 1]`, where the original adds the 16-bit stride to `g_gameState` |
| `UI/MENUPAGE.C` | `menupage_load` reads each field of the 1Ch-byte page and 21h-byte entry records from its place on disk; their 16-bit string offsets (-1: none) become pointers into the string blob. Checked against all 24 menu files the game loads: offsets inside their blob, none without one |
| `GFX/RASTER/CIRCLE.C` | `draw_circle_bresenham` builds the span table's far pointer with `MK_FP` (the original builds it as segment << 16) |
| `INCLUDE/r3d.h` | the names of the renderer's state words (labels on its state block in the original) are `g_abR3dState`'s fields, so both reach the same storage; `g_pColorRemapTable`, past the struct, is a global |
| `GFX/DRIVER/VIDMODE.C`, `VIDINT10.C` | BIOS equipment-word video bits emulated; text-mode reset is a no-op |
| `GFX/SCREEN/SCREEN.C` | VGA latch copy as a page-to-page row copy |
| `INCLUDE/gfx169d.h` | the fog and cursor remap tables are views into one block, laid out as in POLYRAST.ASM's code segment from `FOG_REMAP_TAB_OFF`: the scaled blit reads them by those offsets and the combat code fills four more tables past the cursor one |
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
| `GFX/DRIVER/VTHUNKS.ASM`, `GFX/RASTER/PRESENT.ASM`, `GFX/DRIVER/CGAPASTE.ASM`, `GFX/SPRITE/SPRTHNKS.ASM`, `GFX/SPRITE/BLITCHNK.ASM` | `port/asm/vthunks.c`, `present.c`, `cgapaste.c`, `sprthnks.c`, `blitchnk.c` | no logic: each routine forwards to a vtable slot (SPRTHNKS's `blit_sprite` and `blit_stretched_dispatch` and BLITCHNK's `blit_chunky_planar_4bit` have no caller and trap) |
| `GFX/RASTER/POLYGON.ASM` | `port/asm/polygon.c`: `fill_spans_dispatch` (the polygon rasterizer, `draw_polygon` and its edge walkers, still traps; `blit_rect_buf_dispatch` has no caller and traps) | no logic: it forwards the span table to slot 27 |
| `INPUT/MOUSE.ASM` | `port/asm/mouse.c`, `mouse_data.c` | `test_mouse.py`: INT 33h call sequences of install/clip/restore/set-position, queries, the event callback's register save/restore and latch parking on a planar VGA |
| `SYS/FARPTR.ASM` | `port/asm/farptr.c` | `test_farptr.py`: fills and copies across chunk and 64 KB boundaries, overlapping copies at both parities, pointer compare and normalize |
| `IO/DOSRW.ASM` | `port/asm/dosrw.c` (DOS's calls: the runtime's handle functions; a write of 0 bytes ends the file at the position) | `test_dosrw.py`: reads and writes on a DOS model (the same on both sides): counts around 7FFFh and 64 KB, files that end in each call, failing and short calls, 0 bytes, counts with bit 31 set, 300 random cases, with the original's pointer split in different ways: the result, every call, the file, its position and the buffer. On a real file: the runtime's calls do what the model does |
| `INPUT/KEYBOARD.ASM` | `port/asm/keyboard.c`, `keyboard_data.c` | `test_keyboard.py`: the tables; 1500-byte random key sequences through the INT 9 handler (BIOS shift state and buffer, key states, channels) with and without the Tandy paths; `kbhit_read`; queries; the INT 1Ch axis tick; install/restore |
| `INPUT/TIMER.ASM` | `port/asm/timer.c`, `timer_data.c` | `test_timer.py`: registry alloc/free; the order of callbacks over runs of IRQ 0 ticks and the BIOS chaining; install/restore |
| `AUDIO/CDAUDIO.ASM` | `port/asm/cdaudio.c` | `test_cdaudio.py`: random detect/track/play/stop sequences with MSCDEX absent and with drives at D: and E:, against the same MSCDEX model |
| `INPUT/INT00.ASM` | `port/asm/int00.c` | `test_int00.py`: the handler's retry rule on 2000+ overflowing byte DIV/IDIV and 600 word IDIVs, against `bak_div8`/`bak_idiv8`/`bak_idiv16`; install/restore |
| `STREAM/CODEC/LZWDEC.ASM` | `port/asm/lzwdec.c` | `test_lzw.py`: memory streams read through `stream_read` in random chunk sizes (suspend and resume mid-string), random, text-like, run-heavy and image-like data, resets, truncated input; the output must also equal the encoded input |
| `AUDIO/DRIVER/MIDI.ASM` | `port/asm/midi.c`, `midiseq.h` | `test_midi.py`: generated scripts (every event kind, running status, loop points, meta events, long waits, digital streams) played by up to 17 tracks under random sessions of every command, against fake drivers: the driver calls, the state block, every track record and script after each step. Reaches 312 of the module's 314 branch targets; the other two are dead (see below) |
| `AUDIO/DRIVER/SNDDRV.ASM` | `port/asm/snddrv.c` | `test_midi.py`'s digital streams (its only caller) |
| `AUDIO/DRIVER/MUSDISP.ASM`, `DISP35E0.ASM` | `port/asm/musdisp.c`, `disp35e0.c` | `test_snddrv.py`: each stub's command, the start command's arguments and result |
| `GFX/RASTER/DRAWLINE.ASM` | `port/asm/drawline.c` | `test_drawline.py`: 1500 lines against random clip rectangles and 600 far-flung ones whose clipping overflows IDIV, with probes in the line and span slots: what reaches the driver |
| `GFX/SPRITE/DRAWRECT.ASM` | `port/asm/drawrect.c` | `test_drawline.py`: 1000 rectangles, filled or not, clipped or not: the span table and the outline's lines |
| `GFX/RASTER/VGABLIT.ASM` | `port/asm/vgablit.c` (`vga_planar_fill_rects`, which nothing calls, traps) | `test_vgablit.py`: 360 random span tables through the tiled and untiled copiers with random texture parameters and pages, 150 lines through the line-slot stand-in, on a planar VGA model: display memory, the map mask and the graphics mode left behind |
| `R3D/CORE/R3DDATA.ASM` | `port/asm/r3ddata_data.c`, `r3ddata.h`: the state block, as `g_abR3dState` (the part C code reads) and `g_r3d` (R3D.ASM's own, in the original's order, with the words R3D.ASM keeps between its routines); the cosine table is R3DTRIG's | data only |
| `R3D/CORE/R3DTRIG.ASM` | `port/asm/r3dtrig.c` (with R3DDATA.ASM's cosine table) | `test_r3dtrig.py`: every entry of the cosine, arcsine and arctangent tables through sin, cos, asin and the raw lookup; atan2 over 3000 vectors of three magnitudes, the axes, diagonals and -8000h |
| `GFX/RASTER/POLYRAST.ASM` | `port/asm/polyrast.c` (quads with slanted sides go on to GOURAUD.ASM, not in C yet) | `test_polyrast.py`: the edge stepper over 3000 start/end/step cases; scaled sprites, plain and RLE, at random places, scales, flips, modes (copy, fill, outline, remap through the fog and cursor tables), clip rectangles and pages, on a planar VGA model: display memory, the steppers, the map mask and the RLE scratch after each. Reaches all 40 of the routine's checkpoints. The textured quad blit (the intro's page turn) over 360 quads with vertical sides, either way round, clipped or not: the same, and the corners it swaps |

## Video drivers (VMCODE.OVL)

The VGA chunk (`DRIVERS/VMCODE/VGA.ASM`) is `port/drivers/vga.c`. Display memory is the 256 KB chunky
model of `bak_video.h`: planar byte (offset o, plane p) is element 4o + p, so pages, overruns and the CRTC
start address behave as in Mode Y. `test_vga.py` runs the reference build's `VGA.EXE` on a planar VGA
model (`harness/vga.py`) and compares display memory, the graphics-context block, the DAC, the CRTC start
address and the palette cache after every call.

The platform's VGA (`port/platform/vgaregs.c`) also takes CPU accesses to display memory through the
graphics controller: the four latches, read modes 0-1 and write modes 0-3 with rotation, set/reset,
logical operation, bit mask and map mask (`bak_vga_read`, `bak_vga_write`). A driver that programs the
hardware the way its original does can then keep the original's register writes and memory accesses as
they are. INT 10h sets modes 13h and 10h with the IBM BIOS's register values (`bak_int10_set_mode`). The
picture shown follows the registers: 256 colours from the CRTC start address, or 16 colours, eight pixels
a byte, through the attribute controller's palette and colour select. The harness's planar model latches
from the last byte the CPU reads: Unicorn hands an unaligned word read to the MMIO callback as two aligned
words, so the latches come from the access its memory hook sees.

| Slots | Status |
|---|---|
| init, 1 glyph, 2 line, 3/29 sprite (29 flipped), 4 page copy, 5/6/7 save/size/paste rectangle, 8 flip + retrace, 10/11 solid and dithered runs, 13/14/15 image install/decode/nibbles, 20 palette, 21/22 pixels, 27 span fill, 34/35 DAC blend/upload | ✅ verified |
| 24 rect-buffer offset, 32 clear sprite cache | ported; verified with their consumers (23, 33) |
| 17-19, 23, 30, 31, 33, 36-40 | ⬜ trap with the routine's name |
| 9, 12, 16, 25, 26, 28, 41-48 | empty in the original: no-ops |

Slots the game calls with register arguments have native signatures in `port/drivers/vga.h` (`DrawLineFn`,
`FillSpansFn`). The routines the game swaps into them take the same form: VGABLIT.ASM's texture copiers
(dialog backgrounds and the sky), and slot 39 for textured polygons when it is ported.

The books' HEG chunk (`DRIVERS/VMCODE/HEG.ASM`) is `port/drivers/heg.c`: 640 pixels across in 16 colours,
eight to a byte in each plane. It keeps every register write and display-memory access of the original
and runs them through the platform's VGA, so it relies on the register state as the original does.
`test_heg.py` runs the reference build's `HEG.EXE` on the planar model, from scrambled graphics-controller
and sequencer registers and latches, and compares display memory, the graphics-context block, every VGA
register (the index registers, the attribute flip-flop and the latches too), the DAC and the palette mirror
after every call. Each of 13 deliberate breaks of the translation fails it. The sprite tests blit images
that slot 14 installed and records into random bytes, with each flip, at each edge of the clip rectangle
with x at every value mod 8, on both pages; each of 33 breaks of the sprite code fails them. The original
copies the sprite's record into its stack frame through DS, so they call it with DS = SS, as the game does.

| Slots | Status |
|---|---|
| init (the 350-line mode 10h; the 400- and 480-line mode 12h and the 640x200 mode 0Eh stop in the platform), 1 glyph, 3/29 sprites (29 flipped), 4 page copy, 5/6/7 save/size/paste rectangle, 8 flip + retrace, 13/14 image size/install, 17 packed rows, 20 palette, 21/22 pixels, 25 border, 34 blend | ✅ verified |
| 32 reset the archive cursor | ported; observable only through 31 |
| 2, 10, 11, 16, 26, 27, 30, 31, 33, 36-38 | ⬜ trap with the routine's name |
| 9, 12, 15, 18, 19, 23, 24, 28, 35 | empty in the original: no-ops |

The other adapters' chunks are not ported.

## Sound drivers (SX.OVL)

The game still loads the driver chunk the configuration names (every setting loads one: "none" is `NLD`, a
debug MT-32 driver that skips the patch upload). The sequencer recognises the chunk by the device name in its
DUDE header and calls the port's C code for it (`port/drivers/dude.h`), which works on the chunk's own data at
the original's offsets: the driver's state and power-on values are the original's.

| Driver | Status |
|---|---|
| `M32` Roland MT-32 | ✅ `port/drivers/m32.c`; `test_m32.py`: `M32.EXE` and the translation on the same MPU-401 model, a generated bank, then thousands of random slot calls: registers, MIDI bytes and the whole chunk after each |
| `NLD`, `ADL`, `SBP`, `GMD`, `PRO`, `PS1`, `STD`; digital `ASB`, `APA`, `ADS` | ⬜ stop with "Not implemented yet: the SX.OVL sound driver \"<device name>\"" |

The MIDI the MT-32 driver sends goes to the emulated MPU-401 and no further yet: playing it (host MIDI, or Munt
with user-supplied ROMs) comes after M1, behind a toggle.

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
- **The keyboard buffer holds one key.** The INT 9 handler treats the BIOS buffer as full when head + 2
  equals tail, which is when it already holds one key: there is no typeahead. The translation keeps it.
- **A null near string is empty.** DGROUP starts with four zero bytes, Borland's null-pointer check area,
  so a near pointer left 0 reads as an empty string. The dialogs rely on it: before a game is loaded the
  party's name pointers are 0, and the main menu's dialogs copy them (`NEAR_STR` natively).
- **Chapter 0's default speaker comes from the byte before the table.** `gstate_event_read(7535h)` indexes
  `g_chapterDefaultSpeaker` by chapter - 1, and the main menu's dialogs ask before any game is loaded: the
  original reads the high byte of `g_pendingTempWriteRegion`, FFh while no save is pending, so the speaker
  is -1 (and its name, read from before the party's records, empty).
- **Four locals filled through the address of one.** The ask-about menu builders pass `&nX` to
  `dialog_apply_style_state`, which writes eight bytes: x, y, width and height land in `nX`, `nY`, `nWidth`
  and `nHeight` because Borland lays those locals out in a row. Natively the write hit the stack guard.
- **The text wrapper takes most style codes for letters.** The font's style codes (E0h-F5h in the
  dialogs' text) are plain chars, negative, and Borland's `isalpha` indexes `_ctype` with them: it reads
  the bytes DGROUP holds before the table, the same in 1.00 and 1.02. F1h-F7h land on the EMS driver's
  signature "EMMXXXX", so `textwrap_is_break_point` takes them for letters and a line may break between
  an ellipsis and the code after it (`...` then F1h or F3h, 18 places in the dialogs; F0h reads a zero).
  E1h-E4h read the audio state, and no ellipsis precedes them in the game's text. Natively the game's
  `isalpha`, `isdigit`, `toupper` and `tolower` are Borland's (`bak_native.h`), over a copy of the table
  and the 128 bytes before it (`bakrtl_ctype_block`); `test_rtl.py` checks them against the original.
- **File opens are unchecked.** A missing `drive.cfg` (1.02) or `resource.cfg` passes a NULL `FILE *` to
  `fscanf`; natively the C runtime's invalid-parameter handler reports it.
- **The sequencer's priority change re-sorts with a MOV where a CMP was meant.** `midi_chord_propagate_setting`
  takes the track out of the registry and walks it to re-insert it in order, but its test writes
  (`MOV ES:[BX+15Ch],DL`): every track ahead of the first empty slot gets the new priority too, the track goes
  last, and the insertion loop is dead code.
- **The sequencer's command chain falls through.** In `midi_dispatch_command` (never called by the game)
  0Fh, 11h and 12h do not return, and the tests after them run on what they leave in CL: 0Fh its argument
  (MIDI.ASM's comment says CL stays 0Fh, but CX is loaded from the slot before the call), 11h the track's
  mark count, 12h a part of its clock. A value of 13h-17h then runs that command too.
- **A seek hangs the next tick.** `midi_seek_with_volume_preserve` (unused) restarts the track through
  `midi_track_init`, which releases it first (state FFh) and, while seeking, does not reset its state. The
  scheduler then takes it for a track that stopped during its tick and plays it again, forever.
- **The MT-32 driver's slot 1 result is used.** M32.ASM's comment calls AX = 0961h, CL = 1, CH = 8 leftover
  scratch; the sequencer keeps CL..CH as the voices its arbiter may give out (1-8: the MT-32's parts).
- **Without a digital driver `snddrv_dispatch_cached` returns DGROUP's segment** (it loads it for DS
  before testing the driver flag), so the result depends on where DOS loaded the game. Only a poll of a
  digital stream looks at it, and the game never polls one without a digital driver: every synth driver
  reports no digital capability.
- **The sequencer reads its own code.** After a driver call `midi_apply_vel_change_active` indexes its voice
  table with CH:CL, CH being what the driver left (7 from the MT-32's volume controller): the bytes at
  2E2h + 7xxh are instructions. None of those it can reach is FFh, so the channel is passed over.
- **The scaled sprite blit starts each column's row fraction from the wrong byte.** It loads the y
  stepper's top byte (`prs_e1_acc + 3`) where its fraction was meant, so the sub-row start the stepper
  computed is dropped, and a flipped column can end a byte before its first row: before the data, at the
  end of the source's segment. Its edge stepper tests the quotient's sign with JNS after IDIV, whose flags
  are those of an earlier `AND ECX,0FFFFh`: it always takes the path meant for positive quotients.
- **The line clipper relies on the divide-error handler.** `draw_line` moves an end onto the clip edge with
  a 16-bit IMUL and IDIV; for ends more than 32K apart the quotient overflows and the handler's halving of the
  dividend decides where the end lands. Its minimum tests compare signed and its maximum tests unsigned, and
  it orders the ends with an unsigned compare: with clipping off, a line across x = 0 reaches the driver right
  end first.
- **The VGA driver's sprite blit draws rows of width & ~3 pixels.** Slot 29 steps its source rows by the
  width rounded down to a multiple of 4, so a sprite's last width & 3 pixels in a row are never drawn (the
  game's sprites are multiples of 4 wide). The source position is kept as a quad-aligned offset plus a
  phase, so a record offset that is not a multiple of 4 shifts the picture, and a clip rectangle narrower
  than nothing (xmax < xmin) leaves a negative width whose plane lanes run 16K bytes a row.
- **The VGA driver's span filler and line slot slip far off the screen.** The span filler (slot 27) treats a run as one
  byte when its ends differ only above bit 11 (it masks with 0FFCh where the run slots use FFFCh), and an
  upward vertical line (slot 2) loses bit 15 of its height, which it doubles and halves around a table read.
  Only coordinates far off the screen reach either.
- **The HEG driver's row table runs into the render tail.** Its init writes 480 row offsets from the context's
  200-entry table on, through the 560 reserved bytes after it, up to the span-table segment.
- **The HEG image install finds its mask's segment in a leftover CS.** It calls its helpers with PUSH CS and a
  near CALL, but they return with a near RET, so each call leaves CS on the stack. The arguments it pops
  afterwards are therefore off by one word, and the mask builder takes the image's segment from the slot
  where the plane copier's argument was. The result is right.
- **The HEG chunky-to-planar converter crosses 64 KB on its second word only.** It advances the source
  segment when the step after a group's second word carries out of the offset; a source whose offset wraps
  at the first word reads the segment's start again. Buffers from `alloc_far` start at offset 0 and never
  wrap there.
- **The HEG driver's sprites clip in whole bytes.** Slot 29 takes the bytes wholly past the right edge off
  each row, with the tail byte that holds a row's last x & 7 pixels (even when the sprite only reaches the
  edge), and skips the bytes before the left edge rounded up: an edge inside a byte lets up to 7 pixels
  past it or loses up to 7 inside it. Rows cut off the top move the source by an 8-bit product, wrong from
  256 rows or 2048 pixels wide on, and the bottom and top tests look at a difference's sign only. A row
  takes (width + 7) / 8 bytes of a plane but rows are width / 8 bytes apart, so a width that is not a
  multiple of 8 reads into the next row. The translation keeps all of it.

## Black-box comparison with the original

`port/tools/dosbox_golden.py` runs the user's KRONDOR.EXE in DOSBox Staging (0.84 or later) and the port
on the same data, and compares the pictures they end on, pixel by pixel in DAC colours. DOSBox runs headless,
on its own settings and with sound off. The game folder is mounted under an overlay, so the original's
writes stay out of it. Keys go in through DOSBox's `AUTOTYPE`. The picture is read through
DOSBox's HTTP API: a small routine hooked on IRQ 0 saves the VGA registers and the DAC, and selects each
plane in turn while the API reads A000h. Captures and diffs go to `work/golden/<scenario>/`.

| Scenario | Result |
|---|---|
| `main-menu`: Escape during the intro | ✅ identical (the original shows the page at 4000h) |
| `book`: N (Start New Game) after the intro, chapter 1's first page | ✅ identical, 640x350 in 16 colours (the original shows the page at 8000h) |

The capture also reads the attribute controller, and the decoder handles Mode Y and the 16-colour planar
modes.

## Platform services

| Service | Implementation |
|---|---|
| Interrupts | `port/platform/machine.c`: host events and a virtual 8253 become interrupts when the game polls (`bak_poll`: its wait loops, input and timer reads, retrace waits). Vectors 08h/09h/1Ch are a table the install routines replace and chain through; the BIOS INT 8 handler counts time and calls 1Ch. Reading the variables the timer advances (`g_nFrameTickCountdown`, `g_timer_ticks`, `g_dwSysTickCount`) polls first, so bare spins on them work, and so do the mouse queries (`mouse_button_pressed`, `mouse_get_position`): the book waits in a bare loop for the buttons' release |
| INT 33h mouse | `port/platform/mouse.c`: the host pointer's position over the picture, in the ranges the game sets (four units per pixel); the driver calls the game's handler as an interrupt |
| Keyboard | `port/platform/scancode.c`: host keys as scan code set 1 bytes (E0h-prefixed for the 101-key additions), one IRQ 1 each, read by the game's handler at port 60h |
| BIOS data area | `bak_bios_data()`, also at segment 0040h: keyboard buffer and shift state, CRTC port, time of day |
| MSCDEX | `port/platform/cdrom.c`: absent (as without the disc) unless a drive is attached; drive check, IOCTL audio-track info, play, stop. Playing the GOG release's OGG tracks comes with the audio work |
| Scripted input | `port/platform/inputscript.c`: `BAK_INPUT` lists timed key presses, pointer moves and clicks (in picture pixels) and a quit, for runs without a user |
| MPU-401 | `port/platform/mpu401.c`, ports 330h/331h: UART mode, always ready, every command acknowledged. MIDI bytes go to a sink (`bak_midi_set_sink`); without one they are dropped, or written to the file `BAK_DUMP_MIDI` names |

## Known divergences from the original

- **ROM 8x8 font.** `video_init` installs the BIOS 8x8 font as font slot 1; natively its glyphs are
  blank. The game switches to `game.fnt` before drawing text, so only the slot numbering matters.
- **The BIOS's default palette.** A BIOS mode set (13h, 10h) loads a default DAC palette; natively the DAC
  keeps its contents. The game sets its own palette before anything is visible.
- **Degenerate driver calls.** Where a zero count makes the original's `LOOP` run 65536 times (a rectangle
  0 pixels wide, a 0-colour palette upload or blend) or the original miscomputes an address (an image whose
  packed size is a multiple of 64 KB), the native driver does nothing or stops with a message.
- **Slack after DOS blocks.** Every emulated DOS block is followed by 4 KB of slack, so the original's
  overruns past a block (the LZW decoder can spell long strings beyond its scratch area) stay in memory the
  game owns instead of reaching the host heap.
- **Division the divide-error handler cannot resolve.** Where the original hangs (DIV by zero, or an
  unsigned overflow its arithmetic shift cannot shrink), the translated code stops with a message.
- **Memory past the VGA window** (A000:0000 + 64 KB) reads as FFh and ignores writes, as on a VGA-only PC.
- **MPU-401 acknowledgements.** Every command is acknowledged, UART mode or not; cards and clones differ.
  The drivers only wait for a reply with a bounded poll, so nothing they send changes.
- **The sequencer's limits.** Where the original would write past its tables (more than 16 streams in a
  script's track table; no free voice left for a new owner, which `test_midi.py`'s sessions never reach),
  the translation stops with a message. Scripts are read with linear pointers, where the original's 16-bit
  offsets into a script would wrap at the end of its segment.
- **DGROUP's segment** (see above) is 29DDh natively, DGROUP's paragraph in the 1.02 image.
- **Reads past a sprite's memory** (a flipped column's last row, see above; any sprite blit reading past
  its block) give 0, a transparent pixel: the original reads whatever DOS memory lies there.
- **A filled rectangle taller than the span table** (203 rows) stops with a message; the original writes
  its spans past the table's block. Clipped to the screen, a rectangle has at most 200 rows.
- **The bytes before the ctype table** keep the values the original's image starts with; in the game
  most of them are variables (the audio state, the EMS counters). Only text with a byte from 80h up
  after an ellipsis or an '@' could tell, and the game's has none.
- **Trig tables read out of range.** `r3d_tbl_asin` of a value past +-1.0 (beyond 401Fh) and
  `r3d_tbl_atan_lookup_raw` past index 1FFh stop with a message; the original reads the code after the table.

Placeholder values to revisit: the remaining ASM data tables (zero), the emulated DOS free memory
(640 KB) and EMS size (8 MB).
