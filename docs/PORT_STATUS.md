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

**Current behaviour.** `betterkrondor` first finds the game folder (see "Settings and folders" below) and
checks that it holds the game: `KRONDOR.RMF` must be a resource index and the archive it names must be
present, since the game uses whatever it failed to load. Then it parses `resource.cfg` and `drive.cfg`,
binds the native VGA driver, installs mouse, keyboard and timer, loads the game font, shows the "Loading
Betrayal at Krondor... please wait." screen, finds no CD drive (MSCDEX is absent), installs the MT-32
driver and the sequencer (the bank's 71 SysEx messages go out through the emulated MPU-401), reads its
menu pages (`REQ_*.DAT`), composes the frame screens with the party's portraits for EMS, plays the intro
to its end and scrolls the credits, starting over until a key or a click (as the original does), then
shows the main menu with its buttons and the mouse cursor. Quit To DOS asks for confirmation (Yes quits
cleanly, No returns), Preferences and Contents open their screens and come back, and Start New Game fades
into chapter 1, whose opening book pages through with keys or clicks; the chapter cutscene after it plays
to its end, the new game's state is loaded, the 3D world is set up, and its frames render: the sky, the
hills, trees and road, sprites, and the game's screen around them (below). The world's loop then runs on;
nothing after its first frames is checked yet. The whole tour runs clean under the ASan preset. The MIDI
the game sends is not played yet: `BAK_DUMP_MIDI=<file>` writes it to a file. Development runs against a
GOG copy of 1.02 (`KRONDOR.EXE` matches upstream's 1.02 hash), configured for the MT-32; CI has no game
data and checks that an incomplete folder is turned down cleanly.

**Beyond the main menu** (M2):

1. **After the book**: the book pages through with keys or clicks and the chapter cutscene plays to its
   end (chapter one's map and title); the game then copies `STARTUP.GAM` to `TEMP.GAM` (`savegame_read`,
   byte for byte), reads the party from it and sets up the 3D world: `ts_init` sets the light (M3's R3D
   core starts there), loads the region's encounter shapes and the zone's actors, scans the zone around
   the party, checks the zone's ground at its position, and enters the world's main loop
   (`world3d_main_loop`): each frame sets up the camera (`r3d_camera_setup_view`), fills the sky's
   flat bands (VGAFILL.ASM) and its panorama (VGA slot 33), scans and sorts what is in view, and draws
   each actor through R3D (all of R3D.ASM is C now) and POLYGON.ASM's `draw_polygon`. The scripted
   run idles in the world until it quits, clean under ASan, and its first view of the world is
   identical to the original's (the `world` golden scenario, below).
2. **The game state**: see `TEMP.GAM` below; the new game's start reads the party through it.
3. **Records read straight into structs with pointers.** Menu pages are done (`menupage_load`, below).
   Other loaders that `res_fread` a struct holding pointers need the same treatment, as the plan foresaw
   for saves. A common case: a table
   of 16-bit string offsets read over a pointer table and rebased with `+= (s16)blob` (`CREDITS.C`,
   `NAMEDTBL.C`, `ASKABOUT.C`, `CACTOR.C`, `CBENC.C`, `CSPELL.C` and `DLGWIDG.C` are done). The
   TBL shape records and SPELLS.DAT are done (`SHAPETBL.C`, `CSPELL.C`, below).
4. **Pointer tables and fields sized for 2-byte pointers**, like `resblit_load_asset_table`'s (the sky's,
   the camp's and the dialog portraits' copies of it were 16-bit words; they are pointer tables now): the
   ASan preset reports them at the first write.
5. **Pointers through integers.** All 85 casts clang found between pointers and narrower integers are
   gone (2026-11-01), and the 37 assignments between incompatible pointer types: `bakgame` builds with
   `-Werror` for `pointer-to-int-cast`, `void-pointer-to-int-cast`, `int-to-pointer-cast`,
   `int-conversion` and `incompatible-pointer-types` (`BAK_GAME_ERRORS` in CMakeLists.txt), beside the
   earlier `return-type`, `unsequenced`, `tautological-constant-out-of-range-compare` and `format`.
   Casts through `uintptr_t` stay possible where a far pointer's two words are meant (the sound
   drivers' installs). Two checks in CI keep the rest out:
   - `port/tools/ptrwidth_audit.py` walks clang's AST of the game for what the diagnostics let
     through: allocations sized without `sizeof` that become records holding pointers, file I/O sized
     by `sizeof` of such records, 16-bit views of a pointer's storage, pointers narrowed through an
     integer. The far-pointer cases, the same width in both, are allowed in `ptrwidth_allowlist.txt`;
     `test_ptrwidth_audit.py` checks each detector against the bugs it is for.
   - `port/tools/warning_ratchet.py` counts the game's warnings per category (16-bit conversions,
     signedness, casts the web build rejects, uninitialized reads and the defaults) and fails when a
     count rises. Baselines are kept per clang major in `warning_counts.json`.

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

## Settings and folders

- **The user folder** holds what the game writes (the saves in `GAMES`, `TEMP.GAM`, `KRONDOR.CFG`) and
  the port's settings. It is SDL's per-user folder (`%APPDATA%\BetterAtKrondor` on Windows) unless
  `--user` names another. The game's DOS paths are looked up in the user folder, then in the game
  folder, and writes go to the user folder (`port/compat/bakrtl.c`), so the game folder is only read.
  Until 2026-11-01 the user folder defaulted to the game folder, and a run wrote `TEMP.GAM` into the
  installation.
- **The game folder** is the one `--data` names. Without it the port takes the one remembered from the
  last run, the current folder or the program's folder, and failing those asks with a folder dialog
  (not under `BAK_HEADLESS`), again while the folder picked lacks the game. A folder holds the game when
  `KRONDOR.RMF` is a resource index and the first archive it names is there. The folder found is
  remembered (`port/platform/folders.c`).
- **The DOS saves.** When the user folder has no `GAMES` and the game folder has one, it is copied over
  at start-up, through `GAMES.new` so that a copy cut short is done again, and the saves made in DOS
  load. From then on the user folder's saves are its own. (Saving and loading natively are not yet
  exercised end to end.)
- **`betterkrondor.ini`** in the user folder holds the port's settings; `KRONDOR.CFG` keeps the game's
  own (its sound choices). Each key has a type and a default (`port/platform/settings.c`), and
  `betterkrondor --help` lists them. A value the port changes is written back at once, on its own line:
  comments and keys the port does not know stay as they are. `--set section.key=value` gives a value
  for one run, which is never written back.

| Key | Meaning |
|---|---|
| `paths.data` | the game folder last used |

## Where the game sources depart from the original

The table lists where the game's code departs from the reconstruction. Until 2026-10-31 each site was an
`#ifdef BAK_NATIVE` branch beside the untouched original. They are plain code now, and the originals stay
at the `upstream-snapshot` tag.

- The fold kept every source's preprocessed tokens identical; one `__LINE__` in an error message moved.
- The idioms with nothing left to stand for became plain C: `RECORD_SIZE` is `sizeof`, `NEAR_PTR_SIZE` is
  `sizeof(void *)`, `FP_OFF_ADD` is pointer arithmetic, `RETURN_AX` is `return 0`.
- These `bak/INCLUDE/gtypes.h` idioms remain: `NearPtr`/`NearWord` (a pointer the original held in a
  16-bit word), `NEAR_STR` (a string pointer that may be null) and `FP_SEG_NORM` (the segment of a pointer
  the original holds normalized).
- Two `#ifdef BAK_NATIVE` branches remain: RECTSPR.C and STRBLIT.C, whose native side is still a trap.

| File | Native branch |
|---|---|
| `SYS/BIOSCHK.C` | Tandy ROM probe: never a Tandy |
| `SYS/DOSMEM.C` | `alloc_far` / `_freemem` on host memory; the free-memory query reports 640 KB |
| `SYS/EMSDET.C`, `SYS/EMS.C` | INT 67h calls go to the emulated LIM EMS (8 MB, exact copy-in/copy-out paging) |
| `SYS/BOOT.C`, `BOOT.H` | `g_nWorldViewFovChapter` and `g_bssgap_4c53` are the two bytes of one short (START.DAT's word is read into the first and WORLDHIT.C reads it as a short): natively one short, the names its bytes |
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
| `COMBAT/ACTOR/CACTOR.C` | `combat_actor_bnames_load_cached` reads BNAMES.DAT's records at their 16-bit offsets into the blob (the original rebases the offsets in place), and its `nameChars` holds the whole 8-byte record with `nameMeta` pointing at the second half (below) |
| `GFX/SPRITE/DECOMP.H`, `RESBLIT.C` | `decomp_rle`'s buffer is a `NearPtr` (the original passes it as a 16-bit int) |
| `INCLUDE/structs.h`, `R3D/ACTOR/ACTRENDR.C`, `ACTOROVL.C`, `ACTOROVL.H`, `ACTSHAKE.C`, `R3D/VIS/VISENTRY.C`, `GAME/ENC/WORLDDOR.C`, `RGNENC.C`, `R3D/SCENE/CZONE.C`, `WORLDHIT.C`, `R3D/FX/WORLDFX.C` | an actor's state word (`WorldObjectState`) is a value or a near pointer to the table of its mesh parts' variants (an encounter actor's aux record; the render wrappers swap a small table in while it is drawn): natively pointers go through `visTable` or `encounterAux`, and the whole union is cleared, saved and restored, where the original stores the pointer in the word. The wrappers' tables are ten-byte arrays, as in the original's DGROUP, where they were a byte or two and a separate tail. The mesh-part sort keeps each part's offset from the shape record's paragraph as it reads it, where the original takes the far pointer's offset (natively the offset in the whole block) |
| `R3D/SCENE/CZONE.C` | the zones' actor pool (9 zones of 300, 35 fixed objects, 93 visible) is sized with `sizeof(WorldObject)`: 24 bytes natively, where the original's 22 make F308h |
| `R3D/VIS/PROXSCAN.C`, `VISLIST.C`, `R3D/SCENE/PROXIM.C`, `WORLDFRM.C`, `WORLDHIT.C`, `ZONE.C`, `GAME/WORLD/WORLDMOV.C`, `GAME/ENC/HOTSPOT.C`, `COMBAT/GRID/CMBTGRID.C` and headers | the list of entries in view (`g_ppVisibleEntries`) holds their pointers; the original holds 16-bit offsets in the pool's segment (`g_wVisibleEntrySegment`). Natively the pool is 67,872 bytes, and the last 97 entries (fixed objects 31-34, the spawned objects) resolved 64 KB too early. `vislist_sort`, `proximity_scan_list` and WORLDFRM.C's two list passes take the list alone |
| `GAME/GMAIN.C`, `R3D/SCENE/ZONE.C`, `SCREENS/ENCAMP.C`, `DIALOG/ASKABOUT.C`, `INCLUDE/structs.h` | the sky's panorama, the camp's pictures and the dialog's portraits keep the table `resblit_load_asset_table` returns as `ImageRecord **` (the portrait cache, `ActSpriteCache`, sized with `sizeof`); the original holds the near pointer in a 16-bit word |
| `R3D/TBLSTORE/SHAPETBL.C`, `SHAPETBL.H`, `SHAPEBLD.C`, `R3D/ACTOR/ACTRENDR.C`, `R3D/SPRITE/WORLDRND.C` | a TBL's record table holds normalized far pointers from its DAT chunk's start (paragraphs in the high word, under 16 bytes in the low one: so in all 3065 records of the game's TBLs, whose chunks are under 64 KB), which the original adds to the chunk's seg:0000; natively each becomes the record's address. A `Shape` is file data, so its near pointers stay 16-bit offsets, and the offsets inside a record count from its own segment: `FP_SEG_NORM(record)`, the paragraph it is in |
| `R3D/SCENE/PROXIM.C` | the zone table of a TBL's GID chunk holds normalized far pointers too, rebased the same way (`bak_far_add`); a zone's offsets count from its own segment (`FP_SEG_NORM`), and so do its polygons', whose pointers keep the zone's segment in the original: natively the zone under test keeps it for them |
| `INCLUDE/r3d.h` | the names of the renderer's state words (labels on its state block in the original) are `g_abR3dState`'s fields, so both reach the same storage; `g_pColorRemapTable`, past the struct, is a global |
| `GFX/DRIVER/VIDMODE.C`, `VIDINT10.C` | BIOS equipment-word video bits emulated; text-mode reset is a no-op |
| `GFX/SCREEN/SCREEN.C` | VGA latch copy as a page-to-page row copy |
| `INCLUDE/gfx169d.h` | the fog and cursor remap tables are views into one block, laid out as in POLYRAST.ASM's code segment from `FOG_REMAP_TAB_OFF`: the scaled blit reads them by those offsets and the combat code fills four more tables past the cursor one |
| `IO/RESOURCE.C` | prototype for the INT 24h handler (never raised natively) |
| `STREAM/RESLOAD/FONTLOAD.C`, `STREAM/BUFLOAD/CHUNKRD.C`, `DIALOG/DIALOG.C`, `SCRIPT/TTM.C` | small asm sequences in C |
| `GFX/DRIVER/VIDINIT.C` `video_init`, `VIDDRV.C` `video_driver_bind` | the built-in driver replaces loading the VMCODE.OVL chunk; same mode remapping and context setup |
| `GFX/FONT/FONT.C` fast path | slot 1 called with its registers as arguments |
| `GEN/GFXCTX.H`, `GEN/RNDVTBL.H` | the original's labels inside the graphics-context block and the vtable name the same storage |
| `COMBAT/GRID/CMBTGRID.C`, `CMBTGRID.H`, `COMBAT/ACTOR/CACTOR.C`, `CACTOR.H`, `COMBAT/ARENA/COMBAT.C`, `COMBAT/SPELL/CSPELL.C`, `COMBAT/AI/CMBTAI.C`, `COMBAT/ENC/CBENC.C`, `R3D/SCENE/WORLDHIT.C` | the combat grid's occupant accessors (`combatgrid_tile_terrain`, `combatgrid_tile_set_word`, `combat_actor_terr_under_cur`) give and take a `Combatant *`, and the locals that hold one are pointers; the original passes the near pointer as a word |
| `R3D/SCENE/WORLDHIT.C`, `WORLDHIT.H`, `COMBAT/ACTOR/CACTOR.C`, `CACTOR.H` and the combat views | `world_render_with_overlay(cursor_kind, focus)` takes two arguments, which the original packs into one far pointer: the cursor marker's part variant as the offset, the near address of the object in flight as the segment. `combat_actor_play_short_cine` takes that object, where the original takes its address as a word (below) |
| `COMBAT/AI/CBTAI.C`, `CMBTAI.C`, `CBTAITRN.C` | the AI's three dispatchers index their own tables of actions and of each profile's order; the original reaches them from neighbouring DGROUP addresses (below) |
| `R3D/SCENE/WORLDHIT.C`, `COMBAT/AI/CMBTAI.C`, `COMBAT/ENC/CBENC.C` | near data the original reads as DS:offset through `MK_FP(FP_SEG(near pointer), offset)` (the animation templates, an actor's state) is read directly; a party member's conditions are `abActorStatusRanks[charSlot - 1]`, which the original addresses from `aSkillTrainRate` (below) |
| `COMBAT/SPELL/CSPELL.C`, `CSPELL.H` | SPELLS.DAT's 22-byte records are read field by field (`cspell_spelldef_unpack`), the name's 16-bit offset made a pointer into the string blob; `test_spells.py` checks each field's place |
| `COMBAT/ENC/CBENC.C` | MNAMES.DAT's 16-bit string offsets index the blob, where the original rebases them in place |
| `GAME/ENC/RGNENC.C` | the corpse table's walk keeps an actor template's address whole; the original keeps it as a 16-bit offset |
| `R3D/ACTOR/ACTMOTN.C` | a `WorldEntity` (the two cameras) is allocated with `sizeof`: 38 bytes natively, 24h in the original |
| `R3D/ACTOR/ACTRENDR.C`, `R3D/CORE/R3D.H` | `r3d_setup_actor_in_camera_space` takes the entry's pointer alone; the original takes it far, offset and segment (an entry on the stack has no segment natively) |
| `COMBAT/ACTOR/CACTOR.C`, `COMBAT/ARENA/COMBAT.C`, `COMBAT/AI/CBTAI.C` | three reads through pointers the original leaves unset or null (below): the speed contest's sentinel gets a zeroed state of its own, and the summon test after the last turn and the AI's support cast test their pointer first |
| `COMBAT/ARENA/COMBAT.C`, `COMBAT/SPELL/CSPELL.C`, `GFX/SCREEN/SCREEN.C`, `SCREENS/FMAP.C`, `UI/DLGWIDG.C` and their headers | pointers the original keeps in 16-bit words are pointers: the target `combat_arena_pick_target_actor` returns, the castable spells' names, the cursor table saved across a scene, the full map's town labels (FMAP_TWN.DAT), the save dialog's labels (offsets into its string table) |
| `COMBAT/ACTOR/CACTOR.C`, `CACTOR.H`, `DIALOG/DIALOG.C`, `SCREENS/ITEMUSE.C` | `combat_actor_bitmap_set_bit` and `combat_actor_flag_bitset_clear` take the combatant and use its `spellsKnown`; the original takes its near address as a word and adds 2, where natively the name pointer before it takes 4 |
| `UI/DLGWIDG.C` | a `Dialog`, its widget table and a `DialogWidget` are allocated with `sizeof`; the original's 8 bytes, 2 a widget and 21h hold 2-byte pointers |
| `COMBAT/ARENA/COMBAT.C` | the terrain cutscene's table of eight row pointers is allocated with `sizeof`; the original's 10h bytes hold 2-byte pointers, and natively the last four rows were written past it |
| `GFX/RASTER/POLYFILL.C`, `POLYFILL.H`, `SCREENS/ENCAMP.C` | `draw_polygon_textured` takes the translation table's pointer and keeps it in `g_pTexturedSpanLut` for slot 39; the original stores its DGROUP offset in the context's `wTextureIdx`. Only the camp clock's hand draws this way |
| `R3D/SCENE/WORLDHIT.C`, `GFX/SPRITE/SPRTHNKS.H` | the 1.02 non-rotating map's party marker calls `blit_sprite` with the record's pointer; the original casts the function to pass it as a word |
| `INPUT/WCURSOR.C` | a clicked encounter actor's state index is read through `encounterAux`; the original reads the state word as a near pointer to the same record |
| `STREAM/RESLOAD/FONTLOAD.C` | an uncompressed font's bitmap is freed by its pointer; the original frees the far pointer's offset |
| `AUDIO/ENGINE/AUDIO.C`, `AUDIO/SND/SNDINST.C` | `audio_sfx_register` returns whether the chunk loaded (the original returns its near address; no caller reads it); the drivers' chunks reach their installs as the host pointer's two words |
| `GAME/ENC/HOTSPOT.C`, `STREAM/RESLOAD/IMGLOAD.C`, `R3D/TBLSTORE/SHAPEBLD.C` | an integer counter where the original counts with a near pointer (`hotspotevt_done_reset_all`), a test that is never true left out (below), a far pointer rebuilt from its parts taken whole (`ts_project_shape`, which nothing calls) |
| `SCREENS/MAINMENU.C` | the save listings pass `findfirst` a `struct ffblk` and read its `ff_name`; the original passes a `char[30]` and reads the name from the local after it (below) |
| `SCREENS/PICKLOCK.C`, `PICKLOCK.H` | the lockpick screen's copy of the party's inventory is one buffer, its record and item slots; the original copies it over two locals (below) |
| `R3D/ACTOR/ACTRENDR.C`, `GAME/ACTOR/ACTORREC.C`, `UI/UIWIDGET.C`, `STREAM/RESLOAD/FONTLOAD.C` | pointers step by pointer arithmetic where the original adds to the far pointer's offset word: a mesh part's polygon variant, an actor record's subrecords, a glyph's rows. Natively that word is the address's low half, whose carry would be lost. FONTLOAD.C's own `FP_OFF`, the same word, is gone |
| `SCRIPT/ANIMSCR.C`, `ANIMSCR.H`, `SCREENS/TOWNSCN.C` | the animation scripts' last condition result and a town scene's chapter are variables of their own; the original keeps them in the two words of `g_anim_slots[1]` (below) |
| `GFX/SPRITE/RECTSPR.C`, `GFX/SPRITE/STRBLIT.C` | ⬜ traps: driver slots called with register arguments |

## Changes from the original

The game's rules stay the original's (CLAUDE.md); these change how it runs.

- **Cosmetic random draws have a stream of their own** (`rand_fx` and `RND*_FX` in `SYS/RAND.H`, the same
  generator on a ring of its own in `port/asm/rand.c`). It serves:
  - the world's particle effects (WORLDFX.C) and actors' shake (ACTSHAKE.C);
  - the ground pattern's offset after each step and turn (WORLDMOV.C, read by SKY.C);
  - sprite flips picked while drawing (WORLDHIT.C);
  - ambient sound (AUDIO.C) and the credits' colours;
  - the animation scripts' random timing and branches (TTM.C, ANIMSCR.C).

  The original drew these from the one generator its combat, encounters and dialogs use, so how many
  frames were drawn moved that sequence; the ambient roll alone drew once per world-loop pass. Now the
  game's sequence moves only with play: the world tour's state log has 2 lines instead of 3,411, and a
  walk draws from it not at all.
- **Memory the game reads before setting it is zero.** DOS blocks and the game's own `malloc`s are
  zero-filled, and `bakgame` builds with `-ftrivial-auto-var-init=zero`. Debug, release and sanitized
  builds then run alike: their replays agree frame for frame.
- **`findfirst` returns names sorted**, not in the host directory's order.

## Bugs of the original, fixed

Where the original's code plainly does what its authors did not mean, the port does what was meant.
The quirks below (what the port has taught us) stay unless they are listed here.

- **The AI's spell search starts one past the last spell.** `cspell_ai_pick_castable_spell` counts down
  from `g_nSpellCount`, so it first looks at a 46th SPELLS.DAT record: whatever the heap holds after the
  table. Bytes that matched the school and kind sought would spend a random draw there and could cast a
  spell that does not exist. The search starts at the last spell, and so does
  `cspell_paged_record_lookup_by_id`'s (nothing calls it).
- **A teleport to another row of the party's tile column is ignored.** `modalscreen_pending_scene_trans`
  carries out the pending teleport (TELEPORT.DAT's destinations: the dialogs' and the temples'). It
  skips the move when the destination's zone, tile x and heading are the party's, the heading being
  the one kept from the zone's entry or the last visit to the main menu, and its tile *y* is the
  party's too, but that test reads `==` where `!=` was meant. So it skips a destination on another row
  of the party's tile column, and moves the party into its own tile all the same. The port moves the
  party to every destination, as the original does everywhere but there (`teleport` replay).
- **The AI never gives Gift of Sung to an ally under attack.** One of the caster AI's turn actions,
  `combat_ai_try_aoe_cast_spell_7`, is meant to cast Gift of Sung (spell 7, which moves the caster's
  health to its target) on an ally an opponent attacks when the ally is below 70% of its health (80% for
  the encounter's first enemy). Turn profile 8, Owyn's and Pug's in P1.DAT, tries it first, and the
  monsters' profiles try it too. The original never casts there: it tests the ally's flags as
  `!flags & 2`, which is 0 whatever they are (`!` binds first), where `CAF_DEAD` was meant. Two more
  slips hide behind that one. It walks the caster's own side (in an AI turn `g_pCombatOtherActors`)
  and takes each one's target, an opponent, so fixing the test alone would heal the opponents. And it
  casts whether or not the caster knows the spell, which in STARTUP.GAM Owyn, Patrus and 60 of the 189
  enemies that know spells do. The port walks the opponents (`combat_ai_sung_ally_under_attack`, checked
  by `bak_unittest`) and asks for the spell, as the AI's other cast of it (`combat_ai_pick_heal_spell`)
  does.
- **The AI's healers choose their spell by an opponent's health.** `combat_ai_pick_heal_spell` decides
  what the caster AI's healing action (`combat_ai_try_cast_heal`) casts on a hurt ally, if the caster
  knows it: Gift of Sung when the lowest health among the caster's allies, in points, is below a roll of
  0-79 (the likelier the lower it is), else Hocho's Haven when the first of the caster's side lacks
  it. The original reads the health of `g_pCombatActiveActors`, in an AI turn the caster's opponents
  (its test that skips the caster shows the side it meant), keeps the lowest, and tests the last one's.
  The port takes the lowest health among the caster's living allies other than itself, skipping the
  dead as the cast that follows does, and still draws one roll a call. `bak_unittest` checks the choice
  and the draw.

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
| `GFX/DRIVER/VTHUNKS.ASM`, `GFX/RASTER/PRESENT.ASM`, `GFX/DRIVER/CGAPASTE.ASM`, `GFX/SPRITE/SPRTHNKS.ASM`, `GFX/SPRITE/BLITCHNK.ASM` | `port/asm/vthunks.c`, `present.c`, `cgapaste.c`, `sprthnks.c`, `blitchnk.c` | no logic: each routine forwards to a vtable slot (SPRTHNKS's `blit_stretched_dispatch` and BLITCHNK's `blit_chunky_planar_4bit` have no caller and trap) |
| `GFX/SPRITE/PALBLEND.ASM` | `port/asm/palblend.c`: the colour between two palette entries and the entry from 10h to E6h nearest to it, for the anti-aliased edges of the dialogs' portraits (the palette is read at the pointer; the original reads it from the segment's start, where `alloc_far`'s palettes begin) | `test_palblend.py`: palettes of 6-bit values and of full bytes, 1500 random pairs each (some with a high byte, which the original masks off); 400 palettes built around a pair's average: exact matches, totals of 2 and 3 (the early return), channels at the threshold, best totals of 26h-29h, runs of ever closer entries; the scan's first and last entries. Its threshold only drops to the best total, so it rejects nothing the total's test accepts: mutants without the drop are equivalent |
| `GFX/RASTER/POLYGON.ASM` | `port/asm/polygon.c`: `draw_polygon` with its edge walkers, and `fill_spans_dispatch` (the edge walkers' register-argument entry points trap: `draw_polygon` calls their C forms; `blit_rect_buf_dispatch` has no caller and traps) | `test_polygon.py`: 1500 random polygons (convex and scattered, with repeated vertices, level, upright and 45-degree edges), the edge cases (two vertices, one row, one point, collinear, the same slope on both sides of the top vertex, which fills twice) both ways round, counts from -8000h to 3, 20 vertices, two vertices left by the clipper, level top edges across more than 32767 in x (where the stand-in slopes decide the chains' order), clipped or not, filled or outlined, with an outline colour of their own or not, in the 640x200 mode or not, with the line and span slots probed: what reaches them, the result, the module's state words, the graphics context and the span buffer (a whole segment of random bytes) |
| `GFX/RASTER/POLYCLIP.ASM` | `port/asm/polyclip.c` | `test_polygon.py`: the clipper alone over 1500 polygons (convex and scattered, vertices on the bounds) against rectangles around, inside, across them and inverted, and 600 across the 16-bit range whose quotients overflow (through the divide-error handler's rule): the graphics context |
| `INPUT/MOUSE.ASM` | `port/asm/mouse.c`, `mouse_data.c` | `test_mouse.py`: INT 33h call sequences of install/clip/restore/set-position, queries, the event callback's register save/restore and latch parking on a planar VGA |
| `SYS/FARPTR.ASM` | `port/asm/farptr.c` | `test_farptr.py`: fills and copies across chunk and 64 KB boundaries, overlapping copies at both parities, pointer compare and normalize |
| `GFX/SPRITE/DECOMP.ASM` | `port/asm/decomp.c` (the refill pump reads through `res_fread`) | `test_decomp.py`: LZSS and RLE streams (back-references to earlier output, overlapping, and anywhere in the segment, of every length; literal and repeat runs) whole, cut short, or shorter than the caller's count so that stale buffer bytes decode, through buffers of 1 to 2500 bytes, RLE output wrapping at 64 KB: the bytes read, the buffer and the whole 64 KB segment, against the original reading through a hooked `res_fread` |
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
| `GFX/RASTER/VGAFILL.ASM` | `port/asm/vgafill.c`: the sky's flat bands, through the VGA's write path (a shaded strip taller than its 34-row ramp stops with a message) | `test_vgafill.py`: 260 bands and 200 shaded strips on a planar VGA model, from Mode Y's registers or scrambled ones (set/reset, rotation, logical operation, write mode, bit mask, map mask, latches), in every page, at the game's rows and any others (past the page, across the 64 KB wrap, more rows than a page holds): display memory and every register |
| `R3D/CORE/DISTDIR.ASM` | `port/asm/distdir.c`: the octagonal distance from two points and from two deltas (`distdir_vec3_distance_dir_approx` has no caller in the game and traps) | `test_distdir.py`: 3000 random cases each and the edges of the 32-bit range, where the differences, their magnitudes and 3 * min overflow |
| `R3D/CORE/R3DDATA.ASM` | `port/asm/r3ddata_data.c`, `r3ddata.h`: the state block, as `g_abR3dState` (the part C code reads) and `g_r3d` (R3D.ASM's own, in the original's order, with the words R3D.ASM keeps between its routines); the cosine table is R3DTRIG's | `test_r3d.py`: the lighting ramp |
| `R3D/CORE/R3D.ASM` | `port/asm/r3d.c`, so far `r3d_light_set_direction`, `r3d_build_rotation_matrix`, `r3d_matrix3x3_transform_vec3` (with its far wrapper), `r3d_imul_full32`, the camera: `r3d_camera_setup_view`, `r3d_matrix3x3_multiply`, `r3d_matrix3x3_transpose`, `r3d_matrix_to_euler`; the projection: `r3d_ldiv_helper`, `r3d_vec2_perspective_divide`, `r3d_actor_proj_scale_compute`, `r3d_vec2_long_translate_yflip`; the actor: `r3d_setup_actor_in_camera_space` (with its 48-bit vector transform), `r3d_compose_actor_camera_matrix`, `r3d_actor_compute_lod_z`; the faces: `r3d_mat3_xform_vec3_long`, and the C forms of `r3d_backface_dot_indexed`, `r3d_polygon_resolve_shade_color` (with its lit tail), `r3d_polygon_submit_to_rasterizer` and `r3d_polygon_submit_textured_quad`, whose register-convention entry points trap; the mesh producers: `r3d_mesh_part_polygons` (LOD 0), `r3d_mesh_polygons` (LODs 1 and 2) and `r3d_mesh_part_polygons_lod3`, with `r3d_poly_emit_screen_or_world`; and in `port/asm/r3dclip.c` the polygon clippers: the near plane in 16 and 32 bits (`r3d_polygon_clip_near_z`, `r3d_polygon_clip_against_zplane`, `r3d_polygon_clip_near_z_lod2`, with their crossings), the clip rectangle (`r3d_polygon_scissor_2d`, `r3d_clip_polygon_to_viewport`, `r3d_polygon_clip_rect_2d`, with their edge routines) and the viewport offsets; the rest traps | `test_r3d.py`: the multiply over the 16-bit edges and 2000 random pairs; the rotation matrix of every kind over angles at the cosine table's folds and 600 random ones, the vector transform over every type byte and 1500 random matrices and vectors, with inputs and outputs overlapping (the original reads its inputs again after each store); the light direction for `ts_init`'s angles and 500 pairs; the matrix product over every pair of type bytes and 1200 random pairs, Q14 or overflowing, with the product apart, on or across either operand; the transpose; the angles of 1500 matrices, at and around +-90 degrees, written over the matrix too; the camera of 1200 views, turned or not, with viewports and zooms over the 16-bit range: every word it sets, the clip rectangle and the three matrices; the long scaler over 2600 cases (the game's ratios, every path of its normalization, signs, 80000000h, zero high words), and the projection over 600 points, results over the arguments too; the actor's set-up, matrix and depth over 800 renderer states each (every matrix type, the game's magnitudes and any, mesh shifts up to the 16-bit range, sums at the renormalization threshold): the whole state block in the original's layout and the scratch vectors; the 48-bit mesh vertex transform over every type byte and mesh shift, the back-face dot over any residual shift, the face colours over 1500 faces (day and night, filled or not, the material path with flipped normals and shades at the ramp's ends, the remap), and the submissions with the game's dispatchers hooked: what reaches them and the context's corner arrays. `test_r3dclip.py`: the 15 clippers on the polygon arrays and vertex caches laid out as in the original, compared whole: the rectangle clippers over 659 polygons each (any shape up to 5 vertices, convex ones up to 14, 20 vertices kept, 10 edges across both sides, vertices on and past each bound, coordinates far enough apart to overflow IDIV, which divides as the divide-error handler does, rectangles inverted or past 0), the near clippers over 600 rings of 1-20 vertices (repeated vertices, every pattern of behind and in front), the crossings alone over 1500 cases each with every side byte, the viewport offsets over counts 0-20. `test_r3dmesh.py`: the three producers over 400 generated meshes each (faces of every kind and flag, shared vertices, floor tiles, near-plane crossings, depths on the plane and at 0, the vertex cache kept or not, face counts the loop ends on at once), with the game's polygon fillers hooked: every polygon submitted, with the context's corners and colours, and all the state left behind; the floor-tile emitter over 1500 corner lists |
| `R3D/CORE/R3DDOT3.ASM` | `port/asm/r3ddot3.c` | `test_r3d.py`: 1000 dot products, Q14 and full-range, through `r3d_dot3_long_far` |
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
| init, 1 glyph, 2 line, 3/29 sprite (29 flipped), 33 opaque sprite, 4 page copy, 5/6/7 save/size/paste rectangle, 8 flip + retrace, 10/11 solid and dithered runs, 13/14/15 image install/decode/nibbles, 20 palette, 21/22 pixels, 27 span fill, 34/35 DAC blend/upload | ✅ verified |
| 24 rect-buffer offset, 32 clear sprite cache | ported; verified with their consumers (23, 33) |
| 17-19, 23, 30, 31, 36-40 | ⬜ trap with the routine's name |
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

The MIDI the MT-32 driver sends goes to the emulated MPU-401 and no further yet. Sound is planned in this
order: music from the 1.02 CD tracks (the GOG copy's `music\bakNN.ogg`), then the sound effects through
the AdLib and Sound Blaster digital drivers (the AdLib tracks cover all 179 pieces, and 41 effects exist
only as digital samples), then MT-32 music through Munt with user-supplied ROMs.

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
- **Search results land in the next local.** The save listings (`mainmenu_save_any_exists`,
  `mainmenu_save_scan_highest_slot`, `mainmenu_save_enum_files_slot`) give `findfirst` a `char[30]` for
  its 43-byte DTA. Borland lays the name buffer declared before it just above it, so the found name, at
  offset 30, lands in that buffer, which the code then reads. Natively the overrun went elsewhere and
  the names stayed empty.
- **The lockpick screen copies an inventory over two locals.** `picklock_screen_run` copies 86h bytes,
  the party's inventory record (22 bytes) and its item slots, over an `Actor` and the `items[0x70]`
  declared before it, and reaches the slots through `items`.
- **A creature's name record runs over two locals.** `combat_actor_bnames_load_cached` copies a BNAMES.DAT
  record, three letters and a NUL then four bytes (its images' numbers and a palette), into the 4-byte
  `nameChars`, and reads the second half as `nameMeta`, which Borland places after it; building the file
  name it copies five bytes of `nameChars`. (The file's header gives its string blob 481 bytes; it holds
  349, and the rest stays zero.)
- **The text wrapper takes most style codes for letters.** The font's style codes (E0h-F5h in the
  dialogs' text) are plain chars, negative, and Borland's `isalpha` indexes `_ctype` with them: it reads
  the bytes DGROUP holds before the table, the same in 1.00 and 1.02. F1h-F7h land on the EMS driver's
  signature "EMMXXXX", so `textwrap_is_break_point` takes them for letters and a line may break between
  an ellipsis and the code after it (`...` then F1h or F3h, 18 places in the dialogs; F0h reads a zero).
  E1h-E4h read the audio state, and no ellipsis precedes them in the game's text. Natively the game's
  `isalpha`, `isdigit`, `toupper` and `tolower` are Borland's (`bak_native.h`), over a copy of the table
  and the 128 bytes before it (`bakrtl_ctype_block`); `test_rtl.py` checks them against the original.
- **16-bit promotion decided two tests.**
  - RGNENC.C compared a `u16` with -1, and ANIMSCR.C an `s16` with 0xffff. With Borland's 16-bit `int`
    both matched FFFFh; natively neither did.
  - Effects: a chapter's empty creature slot loaded BNAMES records for shape FFFFh, and the script
    opcode F010h never fell back to the current channel.
  - Both compare in FFFFh's own type now, and `-Werror=tautological-constant-out-of-range-compare`
    keeps the class out.
  - In the same sweep: seven functions that return no value although declared to (no caller reads it)
    now return 0. WIDGET.C assigned `step` and divided by it in one unsequenced expression, and now
    divides by the new step, as the original's code does.
- **The game formats with Borland's printf.**
  - Far strings are written `%Fs`, with Borland's F (far) size modifier. A native printf reads that as `%F`,
    a float, followed by an "s". So dialog and cutscene titles, item names, condition names and combat
    names printed whatever 8 bytes followed in the argument list; in the chapter-1 cutscene the title
    plate read "0.000000s".
  - Borland's printf prints an unknown conversion as it stands. "(%d%)" on the character screen and the
    trailing "%" of "Value Rating: %d%" (CHARSCRN.C, INVINSP.C) showed a percent sign, where the native
    runtime rejects them and the port stopped.
  - Natively these are `%s` and `%%`. A replay showed it: the debug and ASan builds drew that title plate
    differently.
- **The combat AI finds its tables through their neighbours.** Three dispatchers number their actions
  from 1 and read a profile's order of actions from rows past the end of another table.
  `combat_ai_take_turn` reads `__dat_1492 + 9Ah`, `combataipath_select_action` reads
  `g_combat_ai_action_table + 7` and the encounter AI's turn in CBTAITRN.C reads
  `g_encounter_ai_action_table + 10h`. In DGROUP these land on `__dat_153c`, on the rows that follow
  `__dat_1492`'s ten-byte animation template, and on `__dat_15dc`. The monsters' profiles in MONSTnn.DAT
  run from 0 (none) to 8, inside the tables.
- **Pointers left unset or null are read.** `combat_actor_pick_next`'s sentinel combatant never gets a
  state block, and its speed test reads the sentinel's flags through whatever the stack held (charSlot 0
  decides the test either way). After a fight's last turn `combat_arena_turn_actor_inact` tests the
  current actor's flags with no actor left: a near NULL, so DGROUP's opening strings, and the bytes a
  word of them points at.
- **A projectile is its tile effect's status word.** `combat_actor_tile_entry_effect` takes an `s16 *`
  status that both callers point at a projectile's `WorldObject`. The status is its first word, the
  shape, set to -1 to stop the shot. The grid render passes the projectile it draws, and the tile effect
  hands that address on to `world_render_with_overlay` as a far pointer's segment.
- **A party member's conditions are read through the training table.** `combatenc_actor_enter_defense`
  reads bytes at `aSkillTrainRate + 5 + charSlot * 7`. Past the 12-byte table they are
  `abActorStatusRanks[charSlot - 1]` (charSlot is the character's id plus 1): conditions 0-3, 5 and 6.
- **Two words live in a slot pointer no script uses.** Scripts load into `g_anim_slots[0]` only. The
  original keeps the last script condition's result in the low word of `g_anim_slots[1]` and, from
  TOWNSCN.C, the chapter a town scene opened in in its high word, which nothing reads (the map lists
  only `g_anim_slots`, 4EC0h-4EC7h). Natively the condition test, through `FP_OFF`, would stop the game.
- **Loops and tests on pointers.** `hotspotevt_done_reset_all` clears every zone's 400 hotspot flags
  counting with a near pointer, whose + 200 steps 400 bytes. IMGLOAD.C's image loader tests the file it
  opened for failure as `(u16)pointer < 0`, which never holds.
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
- **The renderer's vector transform tests a matrix's type with a signed compare.** A type byte from 80h
  up takes the identity's path (the vector is copied), where the full rotation's would be expected.
  The matrix builder writes 0-3 only.
- **The camera stores the view's zoom as a word.** `r3d_camera_setup_view` writes it over the
  projection's shift count and the byte after it, and the mesh producers load the two as the count of
  their projection loop, so a zoom from 256 on would shift by far more than its low byte. The game's
  zooms are small. (R3D.ASM's C equivalent named the flag it clears `g_bActorViewportClip`; the code
  clears `g_bCamSetupResetFlag`.) `r3d_compose_actor_camera_matrix` stores its renormalization count as a
  word the same way. Its 48-bit vector transform takes any type byte but 0 and 1 for a full matrix,
  where the 16-bit one tests the type as signed.
- **The 16-bit mesh producers project with the screen shift's whole word.** LODs 0 and 3 load the
  shift count and the byte after it (see the camera's word store above) as the count of their shift
  loop, and divide with a 16-bit IDIV that the divide-error handler resolves for vertices near the
  view's edge. LOD 3 tests no depth first: a vertex at depth 0 divides by zero, on which the original
  hangs (and the translation stops). R3D.ASM's C equivalent of LOD 3 had it culling the faces of kind 0
  that the code draws, as the other producers do.
- **The renderer's long scaler loses the ratio's sign and approximates.** `r3d_ldiv_helper` (A and B
  times num / den, the perspective divide's kernel) takes the magnitudes of num and den but builds its
  sign word from their high words afterwards, so the sign is set only when one of them is 80000000h:
  the results take the inputs' signs. It divides num by den's high word once both are shifted left
  until den's top bit is set; for a den under 64K it divides by den's low word instead and swaps num's
  halves, which is right only for a num under 64K, and its byte-wide shift keeps the old low bytes
  where zeros were meant. The products drop the terms below the result's integer part. The game's
  ratios (the view distance over a depth beyond it) stay clear of all of it.
- **The 32-bit rectangle clippers compare low words unsigned.** `r3d_clip_polygon_to_viewport` and
  `r3d_polygon_clip_rect_2d` test a coordinate by its high word's sign, then its low word unsigned
  against the bound, so a bound below 0 counts as above 32767 and takes most vertices for outside it.
  Their 16-bit crossings (`r3d_clip_interp_x_edge`, `r3d_clip_interp_y_edge`,
  `r3d_interp_vertex_at_zclip_long`) multiply and divide the low words of the 32-bit differences, so
  a crossing edge whose width (or depth) is a multiple of 64K divides by zero and the game hangs in
  its divide-error handler; natively the game stops with a message. The game's view rectangle lies on
  the screen, so only the division can arise.
- **The perspective divide writes its result through BP.** `r3d_vec2_perspective_divide` pops the
  scaled values into `[BP+n]`, an SS-relative address, with BP holding the DS offset of the result: it
  relies on SS = DS, as the game has.
- **The VGA driver's sprite blit draws rows of width & ~3 pixels.** Slot 29 steps its source rows by the
  width rounded down to a multiple of 4, so a sprite's last width & 3 pixels in a row are never drawn (the
  game's sprites are multiples of 4 wide). The source position is kept as a quad-aligned offset plus a
  phase, so a record offset that is not a multiple of 4 shifts the picture, and a clip rectangle narrower
  than nothing (xmax < xmin) leaves a negative width whose plane lanes run 16K bytes a row.
- **The VGA driver's opaque sprite blit starts a row early when cut at the top.** Slot 33 (the sky's
  panorama) moves its source by one row less than it cuts off the top (it decrements the count before
  its multiply), so the first visible row is the one above, and the sprite's last row is lost. It also
  drops bit 14 of the row offset, which slot 29 keeps in its carry; the game's row offsets stay below
  4000h.
- **A polygon's vertex arrays hold 20 vertices, closed by a 21st.** `draw_polygon` closes the
  outline's copy of the polygon with a copy of its first vertex one past the end, so with 20
  vertices and an outline colour of its own the 21st y lands on the vertex count, which becomes the
  first vertex's y; with fill off the 21st x lands on the first y. The clipper writes as many
  vertices as it makes, on into the next arrays. The game's polygons are far smaller.
- **A polygon whose top vertex has the same slope on both sides is filled twice.** `draw_polygon`
  cannot tell its two chains apart, so it fills the polygon, then fills it again with its vertices
  reversed. For that it saves the vertex list over the outline's copy, so an outline of its own
  colour is drawn twice from a mix: the reversed vertices, then the rest of the outline's closed
  copy.
- **The 640x200 outline halves the clip rectangle's y bounds without their sign.** `draw_polygon`'s
  outline halves ymin and ymax with an unsigned shift for the length of the outline and doubles them
  back with a left shift: a negative bound turns huge meanwhile, and an odd one comes back even.
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
| `world`: N, Space through the book, the chapter cutscene played out: the 3-D world's first view | ✅ identical: the sky and its panorama, the hills, trees and road, a sprite, the compass and the party (in DOSBox the view comes after about 4 minutes; the capture waits 7) |

The capture also reads the attribute controller, and the decoder handles Mode Y and the 16-colour planar
modes.

## Replays

`port/tools/replay.py` runs a scenario's scripted input headless on the virtual clock, from a fresh user
folder, and compares two logs with a baseline recorded in `work/replay/<scenario>/`.

- **The virtual clock** (`BAK_CLOCK=virtual`, `port/platform/clock.c`): each poll costs 1 µs, and where the
  game waits (an idle poll, the retrace) the clock jumps to the next timer tick or look at the host's
  events. The game itself reads no wall clock: the PIT, the retrace, the scripted input and the frames
  shown are all that move with time, and they all follow this clock.
- **`BAK_FRAME_LOG`**: the time, size and hash of each picture shown that differs from the one before.
- **`BAK_STATE_LOG`**: written on each pass of the world loop when something changed. It holds hashes of
  the game state in TEMP.GAM's layout, of TEMP.GAM (read through the game's own stream) and of the random
  generator, and the party's position and heading.

| Scenario | Result |
|---|---|
| `main-menu`, `book`, `world` (the golden scenarios' scripts) | ✅ two runs give identical logs; each ends on its golden capture. The world tour takes about 19 s, against 90 s on the host's clock, because computing costs no game time |
| `walk`: the world, then three steps, a turn left, a step, two turns right, a step | ✅ identical logs; the state log shows each 400-unit step and each 1024-unit turn |
| `combat`: the world, then a scripted teleport beside chapter 1's first fight (the assassin, zone 1's tile 10,14), seven steps into it, its dialog paged through, auto-combat (A) to the end, two steps past the corpse | ✅ identical logs, the ASan build's too; the fight's result shows in TEMP.GAM and the random state at 128 s. Since the fixes of the AI's healing, Owyn takes the Gift of Sung action on his turns and reaches the healer's choice three times (lowest ally health 55, rolls 77, 11 and 36: Gift of Sung once), but Locklear, whom the assassin attacks, keeps all his health: nothing is cast and the logs are unchanged |
| `spawns`: the world, then a teleport into zone 4 (tile 10,14) among the sixteen stones chapter 1 spawns there, and a full turn about | ✅ identical logs, the ASan build's too; the stones live in the visible pool, past the zone pool's first 64 KB natively, and did not show before the visible list held pointers |
| `teleport`: the world, then a teleport two rows south in the party's tile column (zone 1's tile 10,14) at the heading kept from the zone's entry | ✅ identical logs, the ASan build's too; the state log shows the party at the destination (685600,916000) and the random state moved by the actors loaded around it. Without the fix of the teleport test the party stays at the start (669600,1064800) |

On the virtual clock the game reaches the world sooner than on the host's (its last new picture comes at
41 s of game time), since decoding and drawing take no game time; the golden scripts still work.

## Platform services

| Service | Implementation |
|---|---|
| Interrupts | `port/platform/machine.c`: host events and a virtual 8253 become interrupts when the game polls (`bak_poll`: its wait loops, input and timer reads, retrace waits). Vectors 08h/09h/1Ch are a table the install routines replace and chain through; the BIOS INT 8 handler counts time and calls 1Ch. Reading the variables the timer advances (`g_nFrameTickCountdown`, `g_timer_ticks`, `g_dwSysTickCount`) polls first, so bare spins on them work, and so do the mouse queries (`mouse_button_pressed`, `mouse_get_position`): the book waits in a bare loop for the buttons' release |
| INT 33h mouse | `port/platform/mouse.c`: the host pointer's position over the picture, in the ranges the game sets (four units per pixel); the driver calls the game's handler as an interrupt |
| Keyboard | `port/platform/scancode.c`: host keys as scan code set 1 bytes (E0h-prefixed for the 101-key additions), one IRQ 1 each, read by the game's handler at port 60h |
| BIOS data area | `bak_bios_data()`, also at segment 0040h: keyboard buffer and shift state, CRTC port, time of day |
| MSCDEX | `port/platform/cdrom.c`: absent (as without the disc) unless a drive is attached; drive check, IOCTL audio-track info, play, stop. Playing the GOG release's OGG tracks comes with the audio work |
| Scripted input | `port/platform/inputscript.c`: `BAK_INPUT` lists timed key presses, pointer moves and clicks (in picture pixels), teleports and a quit, for runs without a user; times are on the platform's clock. A teleport fills in the game's own pending teleport (`port/platform/teleport.c`), which the world loop carries out at its next pass, as for TELEPORT.DAT's destinations: the zone's actors and encounters are loaded around the new place |
| Clock | `port/platform/clock.c`: the host's monotonic clock, or (`BAK_CLOCK=virtual`) one that moves only as the game runs, for replays (below) |
| Files | `port/compat/bakrtl.c`: the game's DOS paths (any case, backslashes, a drive letter) are looked up in the user folder, then in the game folder; writes go to the user folder (see "Settings and folders"). `chdir` tells whether the folder is there, looked for as reads are: the game asks it whether `GAMES` exists and makes one, with a first `SAVES.G01`, when not (`mainmenu_save_games_dir_exists`). Until 2026-11-01 it answered yes to everything, so a first save failed in a user folder without `GAMES`. Host paths are UTF-8, SDL's and the C runtime's alike: on Windows the executable's manifest (`port/platform/betterkrondor.manifest`) makes UTF-8 the process code page (Windows 10 1903 on) |
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
- **The BMX codecs past their data.** Output past its block, and input past the buffer (LZSS with a
  compressed size of 0 takes a flag byte from the empty buffer and reads on into DGROUP), stop with a
  message. An LZSS word copy across the end of the output's segment is two byte copies, wrapping as on
  an 8086; a 286 or later faults.
- **The bytes before the ctype table** keep the values the original's image starts with; in the game
  most of them are variables (the audio state, the EMS counters). Only text with a byte from 80h up
  after an ellipsis or an '@' could tell, and the game's has none.
- **R3D's polygon arrays past their 20 entries.** The clippers stop with a message where the original
  writes on into the arrays after them and into the vertex count; a near-plane clip of 0 vertices stops
  too, where the original goes round 32768 times. A convex polygon gains at most one vertex per bound,
  so only one of 17 or more vertices can get there.
- **Polygons past their tables.** An edge of more than 400 rows (the end of `draw_polygon`'s chain of
  400 row stubs: the original jumps into its own code), a row past the span buffer (the original
  writes past it) and a vertex past the graphics context (the original reaches other DGROUP data)
  stop with a message. The game's polygons are clipped to the 3-D view or the screen first.
- **The sky's shaded strip past its ramp.** A strip taller than VGAFILL.ASM's 34 shades stops with a
  message; the original takes the colours from its own code. The game's strips are 27 or 29 rows with
  a level horizon.
- **The face shading past its lighting ramp.** A face material's shade that indexes past the 256-byte
  ramp stops with a message; the original reads on into its segment. The shipped game sets no material
  table, so its faces never take that path.
- **A plain value in an actor's state word.** Drawn with no wrapper's table in it, the word points into
  DGROUP's start in the original, whose first four bytes are zeros and then the runtime's copyright
  string; natively such a value reads as no table (every part visible, every variant 0), which agrees
  only while the value plus the part's group is under 4.
- **Trig tables read out of range.** `r3d_tbl_asin` of a value past +-1.0 (beyond 401Fh) and
  `r3d_tbl_atan_lookup_raw` past index 1FFh stop with a message; the original reads the code after the table.

Placeholder values to revisit: the remaining ASM data tables (zero), the emulated DOS free memory
(640 KB) and EMS size (8 MB).
