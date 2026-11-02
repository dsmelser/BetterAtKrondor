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

**Saving and loading natively** (2026-11-01, first exercised): the Save Game dialog makes its folder
(`GAMES\<name>.Gnn`) and writes `SAVEnn.GAM`, and the world's quicksave and quickload (below) write
the Bookmark and load it as Restore Game does: TEMP.GAM after the load is the TEMP.GAM written at
the save (the `quicksave` replay).

**The saves are the original's, byte for byte, but for the records' pointers** (2026-11-02, the
save-state goldens `savegame` and `load`). A save made through the Save Game dialog on both sides
is identical in all its 334605 bytes except fourteen words, the pointers the original writes as
its addresses and the port as 0 (above): the six party members' name pointers (DGROUP offsets of
the game state's `characterNames`) and the segments of their far actor records (144-byte blocks
lying in a row), and the segments of the shared inventory and the ground pile; every far
pointer's offset word and the party's `inner` pointers are 0 in the original's too. The original
in DOSBox loads a quicksave of the port's and stands where it was saved, its working save the
quicksave's body as the port's is. And the working save every golden scenario leaves is
identical on both sides: TEMP.GAM is compared after each, the pointers set apart.

**Quicksave and quickload.** In the world, F5 saves the Bookmark (`SAVE00.GAM` in the save folder
chosen, as the original's B does) without its question; with no folder chosen (a fresh user folder
has no GAMES) it first makes the Save Game dialog's first one, `GAMES\SAVES.G01`, and chooses it as
the game does at its start (`mainmenu_save_quick`). F9 loads that folder's Bookmark as Restore Game
loads a save (the world loop ends with mode 3): the full map with the chapter's caption, then the
world where the party was; without a Bookmark F9 does nothing. Neither key meant anything in the
world. Saving takes no draw from the game's random generator.

**Autosaves.** On entering a zone (a chapter's start enters its first zone: `zone_load`) and every
`saves.autosave_minutes` on the platform's clock while the game's time moves, the world loop saves
at its next pass where nothing was pressed and nothing waits for the next one (a teleport, a zone's
entry, the scene drawn again, the party's changes, items on the ground: `worldloop_autosave_safe`).
The saves go to `GAMES\AUTOSAVE.Gnn`, a folder of their own made with the highest key free, so that
it comes after the player's: `SAVE01.GAM` is the newest, and each older one moves up a key, as many
as `saves.autosave_slots` keeps (the folder's first saves are the autosaves'). Each is titled with the
game's day, counted from its first, and hour ("Autosave: day 3, 14:20"); Restore Game lists and
loads them as any save. The folder chosen at the start is the first that is not the autosaves', so
B and F5 save where the player saves; loading an autosave makes its folder the one chosen, as any
load does.

An autosave is the Bookmark's save (`mainmenu_save_auto`) that the game does not notice. A save
snapshots into the game state and TEMP.GAM the party's place (`zoneDefaultCameraPos`, its heading
and tile), the places of the zone's objects (`rgnenc_persist_zone_snapshot`) and the party's
inventories (written out, their last-touch time set), and the world started again from TEMP.GAM
puts the party where the last save was taken: after the Contents (mode 4, which saves first) and
after the cutscene that ends the world loop on event 7541h = 2 (mode 7). A fight tears the zone down
and refreshes it without `zone_load`, so it reads no saved place (until 2026-11-01 these notes said
it did). So all that the save changed is put back once its copy is written: TEMP.GAM whole, the
game state, the objects' states, the inventories' flags, the save dialog's title and map place. The
machine stands still meanwhile (`bak_poll_hold`; `savegame_write` sets `g_nFrameTickCountdown`,
which polls), so no time passes and no interrupt runs.

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
  load. From then on the user folder's saves are its own.
- **`betterkrondor.ini`** in the user folder holds the port's settings; `KRONDOR.CFG` keeps the game's
  own (its sound choices). Each key has a type and a default (`port/platform/settings.c`), and
  `betterkrondor --help` lists them. A value the port changes is written back at once, on its own line:
  comments and keys the port does not know stay as they are. `--set section.key=value` gives a value
  for one run, which is never written back.

| Key | Meaning |
|---|---|
| `paths.data` | the game folder last used |
| `video.fullscreen` | borderless full screen; Alt+Enter and F11 switch it, and the game sees neither key |
| `video.scaling` | `pixelart` (the default: sharp pixels without nearest's uneven rows and columns at scales that are not whole), `nearest`, `linear` |
| `video.renderer` | `gl` (the default: OpenGL 3.3, falling back to SDL's renderer where the driver has none) or `sdl` (SDL's renderer) |
| `video.integer` | scale by whole multiples only |
| `video.aspect` | `4:3` (the default: the picture stretched as a VGA monitor showed it) or `square` pixels |
| `video.cursor` | `system` (the default: the system's pointer wearing the game's cursor, moving at the display's rate) or `game` (drawn into the picture by the game, as the original did; the DOSBox goldens use it) |
| `video.step_time` | ms a step or turn of the party takes, drawn smoothly (100; 0: at once, as the original) |
| `window.width`, `window.height`, `window.x`, `window.y`, `window.maximized` | the window as it was when it closed; a place no display shows is dropped for the centre |
| `controls.forward`, `controls.back`, `controls.left`, `controls.right` | keys that act as Up, Down, Left and Right where the party moves (default W, S, A, D; empty for none), named as SDL names keys (`Keypad 8`, `Space`) |
| `controls.repeat_delay`, `controls.repeat_rate` | a held movement key repeats after 250 ms, 10 steps a second |
| `controls.wheel_up`, `controls.wheel_down` | the key a notch of the mouse wheel presses (default Up and Down: a step in the world, the map scrolled, the lists moved through) |
| `controls.pointer_speed` | how fast the gamepad's right stick moves the pointer, in picture pixels a second at full tilt (200) |
| `audio.volume` | the sound's volume in percent (100) |
| `audio.device` | the sound card the game drives: `sb` (the default: the Sound Blaster, the AdLib's synthesizer and digital samples), `adlib`, `mt32` (its MIDI goes nowhere yet), `none`, or `game` (RESOURCE.CFG's choice, the GOG copy's MT-32) |
| `audio.music_volume`, `audio.effects_volume` | the CD music's, and the synthesizer's and the samples', in percent of `audio.volume` (100) |
| `saves.autosave` | autosaves on entering a zone and on the clock (true; see "TEMP.GAM and saves") |
| `saves.autosave_minutes` | minutes between the clock's autosaves, while the game's time moves (10; 0: none) |
| `saves.autosave_slots` | the autosaves kept, the newest first (3, up to 20) |

## Controls

`port/platform/input.c` takes the host's keys, and the scripted ones, to the game's keyboard
(`scancode.c`). The game reports the page it polls (`menupage_run` calls `bak_input_page_polled`): on
the world's and the map's, where the party moves, a key bound to a movement sends that movement's arrow
(W, A, S, D by default), and what a press sent is kept until its release. Everywhere else a key is
itself, so letters stay letters: the save names, the riddle chests and the keyword questions poll pages
of their own. A held movement key, an arrow or a bound one, repeats on the platform's clock where the
party moves, and only once the game has taken the press before from its keyboard buffer; the host's own
repeats of it are left out (their rate is the system's setting). Elsewhere keys repeat as the host
repeats them. The `walk-wasd` replay plays the `walk` on W, A and D and is checked against `walk`'s
baseline: both logs are identical.

**The wheel and gamepads.** A notch of the wheel presses `controls.wheel_up` or `wheel_down`. On a
gamepad (SDL's, any make) the d-pad and the left stick are the arrows, as keys through the same path
(so they move the party where it moves and repeat on the clock), the right stick moves the pointer
(and the system cursor with it), A and X are the left and right mouse buttons, B is Escape and Y
Return. The `walk-pad` replay plays the `walk` on the d-pad and is checked against `walk`'s baseline:
identical. ASan builds leave SDL's gamepad subsystem out: on Windows ASan takes SDL's string moves in
its gamepad mappings for overlapping copies (the C runtime's memmove is its memcpy), and scripted pads
need no subsystem. Nor do scripted runs (`BAK_INPUT`) take it: a gamepad connected to the machine
plays along otherwise, SDL delivering its events under the dummy video driver too, and on 2026-11-02
one paged a replay's book early, so that two runs of a scenario no longer agreed.

The world's and the map's arrow buttons held down with the mouse step at the same rate
(`bak_input_hold_start`, `bak_input_hold_step`, from WORLDLP.C and MAP.C), the first after the same
delay. The original waited 90 timer ticks (about 0.38 s at the game's 237 Hz) on the shared
`g_nFrameTickCountdown`, then stepped on every pass of its loop, as fast as the machine drew the view:
natively 46 steps in the 1.45 s the `hold-button` replay holds the button, against 12 now (and 13 in
`hold-key`, which holds W as long: its press steps at once).

**Smooth steps.** A step or a turn of the party in the 3-D view takes `video.step_time` (100 ms: 7
frames of the VGA's 70 Hz): the frames between the place and heading before it and after it are
drawn as the game's own automatic turns at crossings are (`world_render_scene_dispatch(0)`, nothing
animated, the compass; WORLDMOV.C's `worldmove_smooth_from`), and the camera then stands where the
step put it. The original jumped a whole step (400 units) or turn (400h) at once. The in-between
places are a picture only: the game's state is that of the step's end, the ground's pattern keeps
its offset until the step's last frame, and the labyrinths' record of what is near the party
(`worldframe_enc_rec_prox`) is left alone meanwhile. (The `labyrinth` replays walk one, its walls
GOURAUD.ASM's textured quads: the same states smoothed or not, 2026-11-02.) The map's view does not smooth. Held
movement steps at the keys' rate as long as a step takes no longer than the repeat's period.

The map's hidden riddle-chest test (the grave key held with the right Shift and an Alt down, MAP.C)
waits for half a second of holding on the platform's clock (`bak_clock_ms`). The original counted
500,000 polls of the key, which lasted about that long on a 386 and natively as long as the host's
polls. Scripted, a 0.3 s hold does nothing and a 1 s hold shows the Moredhel lock vision after 0.5 s,
then the chest's word lock (the `riddle` replay).

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
- One `#ifdef BAK_NATIVE` branch remains: RECTSPR.C, whose native side is still a trap.

| File | Native branch |
|---|---|
| `SYS/BIOSCHK.C` | Tandy ROM probe: never a Tandy |
| `SYS/DOSMEM.C` | `alloc_far` / `_freemem` on host memory; the free-memory query reports 640 KB |
| `SYS/EMS.C` | EMS is plain memory (since 2026-11-01): an allocation is a block of its own (`bak_plain_alloc`, outside the segment table), and mapping one gives the block whole, with no page frame and no copies. The original detects the driver (EMSDET.C, gone), takes every free page at start-up and maps four 16 KB pages at a time into the 64 KB frame; its count of free pages is kept (512 at start, 8 MB, which the start-up check reads). A pointer into a plain block has no segment, but `FP_OFF` gives its offset in the block as for a DOS block (`segments_note_plain`): the 16-colour driver's copies normalize their buffer with it (the book's pages). Until 2026-11-01 INT 67h was emulated exactly, the frame's pages copied in and out |
| `SYS/BOOT.C`, `BOOT.H` | `g_nWorldViewFovChapter` and `g_bssgap_4c53` are the two bytes of one short (START.DAT's word is read into the first and WORLDHIT.C reads it as a short): natively one short, the names its bytes |
| `SYS/CFGPARSE.C` | writable buffer for the drive prefix the parser overwrites |
| `SCRIPT/ADSCRIPT.C`, `SCRIPT/ANIMSCR.C`, `INCLUDE/structs.h` | the animation scripts' object nodes, slots and operand lists sized with `RECORD_SIZE`/`NEAR_PTR_SIZE`; `Slot`'s node-chain head and operand-list fields are `NearPtr` |
| `SCRIPT/TTM.C`, `SCRIPT/TTMDLG.C`, `DIALOG/DIALOG.C`, `GFX/SPRITE/SPRTHNKS.H`, `INCLUDE/structs.h` | a script object's image tables (`pAhPagedImage`, `NearPtr`) and the sprite pointers taken from them (`NearWord`, or a native local where the original reuses a 16-bit parameter); until 2026-11-01 the EMS page's segment with `FP_SEG` (images no longer go to EMS) |
| `STREAM/RESLOAD/IMGLOAD.C`, `IMGLOAD.H` | `free_image_record` frees the first image's pixels (the block's start) and reads the table's records pointer at native width. The file's own loader (`BOOK.BMX`) builds its NULL-terminated table of record pointers at native width (`NearPtr`); the original holds them as 16-bit near pointers |
| `INCLUDE/structs.h`, `STREAM/RESLOAD/IMGLOAD.C`, `GFX/SPRITE/RESBLIT.C`, `GFX/SPRITE/DECOMP.H`, `SYS/EMSIMG.C`, `SCRIPT/TTM.C`, `GFX/RASTER/POLYRAST.H`, `GOURAUD.H` and the drivers (`port/drivers/vga.c`, `heg.c`, `port/asm/polyrast.c`, `decomp.c`, `port/platform/cursor.c`) | images live in plain memory (`bak_plain_alloc`, outside the segment table, whose conventional range holds about 650 KB), never in EMS: RESBLIT.C's tables of every storage mode (the original puts mode 1's in EMS, and mode 2's under 0xffdc bytes, its blits mapping the chain: EMSIMG.C's wrappers and TTM.C call straight through now) and IMGLOAD.C's. So an `ImageRecord` holds a pointer to the paragraph its pixels' offset counts from and the bytes from there to its block's end (`pImageBase`, `nImageExtent`), where the original's first word is the segment, or while the table sits in EMS its page chain: `imgload_set_plain` places one in a block as a far pointer into a DOS block would be (the block's start while the offset is under 64 KB, normalized past it), `imgload_set_far` from a far pointer. The decompressors write into a plain block through `decomp_lzss_into`/`decomp_rle_into` (the block stands for the segment their 16-bit offsets wrap in) and the drivers' decoders through a base pointer (`heg_image_decode_at`, which the differential test calls with an offset). The records are allocated at native size (the original's 10 bytes in two places), and the routines that took a record as 16-bit words (the GOURAUD entry, the EMS blit wrappers) take the record. `ResourceRemapDesc`, a second view of the same ten bytes, is gone |
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
| `COMBAT/SPELL/CSPELL.C` | the world's cast screen takes the place of its caster's highlight from the table of the party's portrait places (`__dat_165e`: x and y by party slot); the original reads the same words past `__dat_15dc` and `g_nSpellSymbolAnimFrame`, the DGROUP data laid out before the table (below) |
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
| `GFX/SCREEN/SCREEN.C` | where the game would draw its cursor into the picture (`screen_frame_present`, `screen_cur_refr_during_long_op`, `screen_wipe_horizontal`), it first hands it to the system cursor (`bak_cursor_system`), and draws it only when that declines (`video.cursor = game`) |
| `AUDIO/ENGINE/AUDIO.C`, `AUDDRVST.C` | the waits for a sound to end (a blocking sound effect, a track stopped) poll (`BAK_IDLE`); the original spins while the sequencer's timer interrupt ends the sound. Natively that interrupt runs only where the game polls, so the dialogs' blocking sounds hung the game (the riddle chest's vision) |
| `SCREENS/PICKLOCK.C`, `PICKLOCK.H` | the lockpick screen's copy of the party's inventory is one buffer, its record and item slots; the original copies it over two locals (below) |
| `R3D/ACTOR/ACTRENDR.C`, `GAME/ACTOR/ACTORREC.C`, `UI/UIWIDGET.C`, `STREAM/RESLOAD/FONTLOAD.C` | pointers step by pointer arithmetic where the original adds to the far pointer's offset word: a mesh part's polygon variant, an actor record's subrecords, a glyph's rows. Natively that word is the address's low half, whose carry would be lost. FONTLOAD.C's own `FP_OFF`, the same word, is gone |
| `SCRIPT/ANIMSCR.C`, `ANIMSCR.H`, `SCREENS/TOWNSCN.C` | the animation scripts' last condition result and a town scene's chapter are variables of their own; the original keeps them in the two words of `g_anim_slots[1]` (below) |
| `GAME/WORLD/WORLDLP.C`, `SCREENS/MAINMENU.C`, `MAINMENU.H` | the world's F5 (quicksave: the Bookmark without its question) and F9 (quickload of the Bookmark), see "TEMP.GAM and saves" |
| `GAME/WORLD/WORLDLP.C`, `R3D/SCENE/ZONE.C`, `GAME/GMAIN.C`, `SCREENS/MAINMENU.C`, `MAINMENU.H` | autosaves: asked for on entering a zone (`zone_load`), forgotten as the world starts again (`gmain_start_dispatch`), written at a world-loop pass where nothing was pressed (`mainmenu_save_auto`); the folder chosen at the start is the first that is not theirs (`mainmenu_save_request_load_game`), see "TEMP.GAM and saves" |
| `GAME/WORLD/WORLDMOV.C`, `WORLDMOV.H`, `R3D/SCENE/WORLDFRM.C` | smooth steps: a step's or turn's in-between frames (`worldmove_smooth_from`, as many as `bak_smooth_frames` gives), during which the labyrinths' record of what is near the party is left alone (see "Controls") |
| `GFX/SPRITE/RECTSPR.C` | ⬜ trap: driver slots called with register arguments |
| `GFX/SPRITE/STRBLIT.C` | each row of a stretched sprite through SPRTHNKS's `blit_stretched_dispatch`, slot 37 in its native form (the original called the slot with its registers) |
| `GFX/SPRITE/ROTBLIT.H` | ROTBLIT.ASM's data as two blocks laid out as in DGROUP, the old names macros into them: the corners and which is which (`g_anRotBlitState`), and the X and Y source tables STRBLIT.C shares (`g_aStretchSrcTables`). The original's tables of its octants' code addresses and the pointer to its own body are gone |

## Changes from the original

The game's rules stay the original's (CLAUDE.md); these change how it runs.

- **Cosmetic random draws have a stream of their own** (`rand_fx` and `RND*_FX` in `SYS/RAND.H`, the same
  generator on a ring of its own in `port/asm/rand.c`). It serves:
  - the world's particle effects (WORLDFX.C) and actors' shake (ACTSHAKE.C);
  - the ground pattern's offset after each step and turn (WORLDMOV.C, read by SKY.C);
  - sprite flips picked while drawing (WORLDHIT.C);
  - ambient sound (AUDIO.C) and the credits' colours;
  - the animation scripts' random timing and branches (TTM.C, ANIMSCR.C);
  - where a dialog box's parchment pattern starts (DIALOG.C; since 2026-11-01, missed by the first sweep:
    every dialog box drawn moved the game's sequence, so a Bookmark's question or a load's caption shifted
    the next fight's rolls).

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
- **Records of noise in zone 9's tile 10,11.** The shipped T091011.WLD (1.02) holds, among its 275
  records, eight whose bytes are noise: one with a real shape (141) at a place far outside the zone,
  then seven whose shape ids (1600 to 60136) are in none of the zone's tables (222 shapes). The original
  looks them up all the same as the tile's objects load (`czone_load_actors`): an id from 8000h up as a
  negative index, reading a far pointer from before its table, and the others past every table, which
  give a null pointer, so their kind is read from the interrupt vectors; whatever it finds, they lie
  far out of sight. Natively the lookup faulted, and the port stopped whenever the party came within a
  tile of 10,11 in zone 9 (found by the zone sweep, 2026-11-02). `ts_get_shape` answers no shape for an
  index below 0, as for one past the tables, and `czone_load_actors` drops a record without a shape,
  as it drops the objects its detail level leaves out. The record far away stays, as in the original.
- **A column clipped at the top shifts the texture of every column after it.** GOURAUD.ASM's
  rasterizer (the textured quads with slanted sides: the trees of the road south of the start,
  the labyrinths' walls) skips as many steps of a clipped column's texture walk as rows were
  cut, and keeps the count for the columns after it, clipped or not: their textures start as
  many steps in, and a span shorter than the count walks its texture column past the table of
  column offsets, into the stack. The port starts each column's count at 0
  (`gfx_gouraud_blit_quad`); its differential test runs the original's way through
  `gouraud_blit_quad_as_original`, and adopts the original's display memory where it read its
  stack (2026-11-02).

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
| `GFX/DRIVER/VTHUNKS.ASM`, `GFX/RASTER/PRESENT.ASM`, `GFX/DRIVER/CGAPASTE.ASM`, `GFX/SPRITE/SPRTHNKS.ASM`, `GFX/SPRITE/BLITCHNK.ASM` | `port/asm/vthunks.c`, `present.c`, `cgapaste.c`, `sprthnks.c`, `blitchnk.c` | no logic: each routine forwards to a vtable slot (SPRTHNKS's `blit_stretched_dispatch` had no caller, STRBLIT.C calling slot 37 with its registers: natively STRBLIT.C calls it, with the slot's native arguments; BLITCHNK's `blit_chunky_planar_4bit` has no caller and traps) |
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
| `GFX/RASTER/POLYRAST.ASM` | `port/asm/polyrast.c` (quads with slanted sides go on to GOURAUD.ASM's) | `test_polyrast.py`: the edge stepper over 3000 start/end/step cases, the harness setting the flags after its IDIV as DOSBox does (`Machine.model_division_flags`; see "Flags after IDIV" under what the port has taught us); scaled sprites, plain and RLE, at random places, scales, flips, modes (copy, fill, outline, remap through the fog and cursor tables), clip rectangles and pages, on a planar VGA model: display memory, the steppers, the map mask and the RLE scratch after each. Reaches all 40 of the routine's checkpoints. The textured quad blit (the intro's page turn) over 360 quads with vertical sides, either way round, clipped or not: the same, and the corners it swaps |
| `GFX/RASTER/GOURAUD.ASM` | `port/asm/gouraud.c`: the textured quad rasterizer for quads with slanted sides (the trees of the road south of the start, the labyrinths' walls), column by column along the two chains of edges from the leftmost corner, with 16.16 steppers for the span and for the texture column and row at its ends; the corners' texture coordinates (corner 1 at column width - 2, corner 2 at row height - 2 as well: the texture's last column and row are never sampled), the slope comparison (CWD, then an unsigned DIV) and the edge stepper, over count + 1 steps. One fix ("Bugs of the original, fixed"): each column's top-clip skip count starts at 0 | `test_gouraud.py`: 300 quads of random corners (often sharing one), textures, clip rectangles, pages and remap tables drawn the original's way, both GOURAUDs called directly and the original's display memory adopted where it read its stack, the harness setting the flags after the stepper's IDIV as DOSBox does; 200 more with no corner above the clip rectangle through `polyrast_gouraud_blit_clipped`, the port's way, identical; the fix on its own (a quad clipped at the top shows, from that row down, its unclipped picture); the stepper over 3000 cases, the slope comparison over 3000, every ordered pair of corners. 16 of 17 mutants killed; the survivor compares a span's last row with the clip's bottom by <= instead of <, which is equivalent: on equality the clamp gives the same count |
| `GFX/SPRITE/ROTBLIT.ASM` | `port/asm/rotblit.c`, `rotblit_data.c`: a sprite drawn rotated and scaled (the TTM scripts' opcode A5A7h: the shops' and the taverns' animations, chapter 8's ending), one span at a time through VGA slot 36. The quad's corners from the angle and the two scales, the walk round it from the corner highest on the screen (its octant picks how a step becomes a source offset), the left edge's x by row, the source offsets across the quad's width and the tables of the step each source row and column starts on; each row a span, clipped. The scratch frame is an array laid out as the original's, and the corners and the source tables (STRBLIT.C's too) are blocks laid out as in DGROUP: the original reads past the ends of its tables | `test_rotblit.py`: 1500 random sprites (the scripts' sizes and others, any angle, the axes and near them, scales from tiny to far past the screen, about their centres or from a corner, any clip rectangle, page and adapter) with slot 36 a stand-in on both sides: every span (page, row offset, x, count, row base, the source table's words), the corners' block, the source tables, the VGA's registers and what `read_4words_from_2tables` gives; 300 more whose centre's IDIVs overflow (the divide-error handler's halving); 300 wide flat sprites a degree or a few off an axis, whose ratio passes 16 bits and saturates. 26 of 31 mutants killed; the 5 survivors are equivalent: a top row of 0 clamped to 0 or not, a fill of 0 words done or skipped (twice), a level right edge's product (always 0 then: the bottom corner is the lowest), and the clamp at 320 (the ratio stays under the other scale, so the quotient stays under the sprite's width or height, which the tables keep under 320) |

Two codec modules stay placeholders, because the game cannot reach them. A stream starts with its
codec's number, and every stream the game opens is a file's, opened for reading:
- `STREAM/CODEC/LZHUF.ASM` (codec 3, LZH, both ways) is used by no stream in the data.
- `STREAM/CODEC/BLITRLE.ASM` (`blit_rle_sprite`) decodes codec 1 only for a stream in memory
  (`stream_open_from_memory`), and nothing opens one. A file's RLE stream goes through CODEC.C's own
  decoder.

The tally from a scan of the GOG copy's 1.02 data on 2026-11-02 covers the archive and the loose
`VMCODE.OVL`, `SX.OVL` and `FRP.SX`, wherever the game opens a stream:
- codec 0: the 179 sound records of FRP.SX;
- codec 1: the TTM scripts' 42 `TT3:` chunks and the 5 fonts;
- codec 2: the other 393 streams (the BMX payloads DECOMP.ASM does not unpack, the screens, the ADS
  scripts, BOOK.BMX's `BMP:BIN:` and the drivers in VMCODE.OVL and SX.OVL).

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
| init, 1 glyph, 2 line, 3/29 sprite (29 flipped), 33 opaque sprite, 4 page copy, 5/6/7 save/size/paste rectangle, 8 flip + retrace, 10/11 solid and dithered runs, 13/14/15 image install/decode/nibbles, 20 palette, 21/22 pixels, 27 span fill, 34/35 DAC blend/upload, 39 spans translated through a table (the camp clock's hand; since 2026-11-01), 37 a row of a sprite stretched or shrunk (the town scenes' and the cutscenes' zooms; since 2026-11-02), 36 a row of a sprite rotated (ROTBLIT.ASM's spans; since 2026-11-02) | ✅ verified |
| 24 rect-buffer offset, 32 clear sprite cache | ported; verified with their consumers (23, 33) |
| 17-19, 23, 30, 31, 38, 40 | ⬜ trap with the routine's name; none is reached (2026-11-02): no code calls 17-19, 30, 31 or 40, slot 23's only caller (POLYGON.ASM's `blit_rect_buf_dispatch`) has none, and slot 38's (RECTSPR.C's `blit_sprite_flipped_clipped`) is named only by a far pointer nothing reads (TIMER.ASM's `_dgroup_farptr_2c50`) |
| 9, 12, 16, 25, 26, 28, 41-48 | empty in the original: no-ops |

Slots the game calls with register arguments have native signatures in `port/drivers/vga.h` (`DrawLineFn`,
`FillSpansFn`). The routines the game swaps into them take the same form: VGABLIT.ASM's texture copiers
(dialog backgrounds and the sky), and slot 39, which `draw_polygon_textured` puts in slot 27's place
(POLYFILL.C): it reads each pixel under the spans through the read map select and writes it, translated
through `g_pTexturedSpanLut`, under the map mask, so it runs through the platform's graphics controller as
the original's does. `test_vga.py` checks it with the controller set at random in some cases, and the
registers after it.

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
| `ADL` AdLib (OPL2) | ✅ `port/drivers/adl.c`, translated register for register (a slot's AX and CX are what the original leaves there); `test_adl.py`: `ADL.EXE` and the translation on the same OPL2 model, generated banks, then thousands of random slot calls (notes past the nine voices, the reserved voices of controller 75, the sustain pedal, bends, the master volume and enable): AX and CX, the register writes and the whole chunk after each |
| `ASB` "CMS Sound Blaster", digital | ✅ `port/drivers/asb.c`, on the platform's card (`sbdsp.c`); `test_asb.py`: `ASB.EXE` and the translation on the same card, through the install (the DSP there or not, on IRQ 2, 3, 4, 5 or 7, versions 1.00 to 2.01 and 8001h, other bases, the CD-ROM check) and then hundreds of random blocks (looping or not, some across a 64 KB page or ending on it, rates under 16 Hz), polls, stops and their interrupts: AX, every port access, the arguments, the whole chunk, the card and the vectors after each. The game sends commands 0-5 only; 6, 7, 8, 12, 14 and 15 are as simple, and 9, 10, 11 and 13 (streaming from a file) trap. Natively it does not hook INT 10h, 0Dh and 74h, which serve only that streaming |
| `NLD`, `SBP`, `GMD`, `PRO`, `PS1`, `STD`; digital `APA`, `ADS` | ⬜ stop with "Not implemented yet: the SX.OVL sound driver \"<device name>\"" |

The MIDI the MT-32 driver sends goes to the emulated MPU-401 and no further yet. The 1.02 release's
music plays from the CD's tracks (see "The CD's music" below). The Sound Blaster setting's two
drivers run natively: ADL on the emulated OPL2 (music and effects; its tracks cover all 179 pieces)
and ASB on the emulated card (the 41 effects that exist only as digital samples). They are the
default (`audio.device = sb`, which overrides RESOURCE.CFG's `soundDrv` after the game reads it,
CFGPARSE.C): with the CD's tracks there and CD music on, as a game without a KRONDOR.CFG starts, the
music comes from the CD and the effects from the AdLib and the samples. MT-32 music through Munt
with user-supplied ROMs comes later.

### The CD's music

The 1.02 release plays its music from the CD when it finds a CD drive through MSCDEX and its
preferences have CD music on (KRONDOR.CFG's flag 10h; Preferences switches it, and a game without a
KRONDOR.CFG starts from CDEFAULT.DAT's, which has it on, where DEFAULT.DAT has not). A music track's
id less 999 is the CD's track, played from 75 frames before the track's start as MSCDEX reports it,
less the 114 the game keeps as the disc's first address: 39 frames into the gap before the track. The
length played comes from CD.DAT, a frame count for each track, 1.5 to 2 s shorter than the track. The
game never asks whether the CD still plays: a track plays once.

The port's drive (`port/platform/cdrom.c`) appears when the game folder holds the tracks as
`music\bakNN.ogg` (the GOG release's layout), NN from 02 (track 1 is the data). Its table of
contents is laid out as on a disc: the data track at 00:02:00, a minute long, then each audio track
after a two-second gap, as long as its file (read from the Ogg pages; the GOG copy's 62 tracks make a
disc of 54:44). It answers to the first drive letter the game asks about (DRIVE.CFG's). A play request is a
stretch of the disc: the track it starts in plays from there, after the part of the gap before it as
silence, for the frames asked. The track's file is read and opened when the play is asked for and
decoded (stb_vorbis, fetched by CMake at a pinned commit) as the mix asks. With the GOG copy and CD
music on, the intro plays track 24, the main menu 16, the book 11, the chapter's cutscene 31 and the
world's arrival 36. The GOG copy's tracks end in a few seconds of digital silence, where CD.DAT's
lengths cut them.

`bak_unittest` lays out a disc from synthetic Ogg pages (lengths, a track missing, one at another
rate, one past what a disc holds) and plays stretches of two synthetic tones
(`port/tools/gen_tone_ogg.py`, kept in `port/tests/unit/tone_ogg.h`) through the mix: the gap's
silence, the cut where the play ends, a file that ends first, a mono file on both sides, a stop.
The `music` and `music-menu` replays play the GOG copy's.

## What the port has taught us about the original

- **Flags after IDIV.** POLYRAST.ASM's edge stepper (`polyrast_edge_init_step`, and GOURAUD.ASM's
  twin) tests the quotient's sign with `JNS` right after `IDIV`, whose flags x86 leaves undefined.
  Unicorn leaves them as they were (`AND ECX,0FFFFh`'s: never negative), so the differential harness
  ran the routine as if every quotient were positive, and the port's translation copied that; DOSBox
  Staging sets them from the quotient (`set_cpu_test_flags_for_division`), so the game as it is played
  takes the path the code means: a negative quotient (a mirrored or flipped sprite, an edge walked
  backwards) starts its walk at the quotient's own low word, not its negation's. The Blue Wheel Inn's
  `inn` golden showed it (2026-11-02): its mirrored patrons, drawn shrunk, were sampled a column off in
  strips every six columns. The port follows the code's meaning and DOSBox, and the harness models
  DOSBox's flags within the stepper (`Machine.model_division_flags`). An audit of the assembly finds
  no other division whose flags a jump reads; GOURAUD's stepper (`edge_step_init`) has the same since 2026-11-02.
- **BC++ 2.0 promotes `unsigned short` to signed `int`.** `SHAPETBL.C` (one of the 13 modules built
  with BC++ 2.0) compiles `i < g_spriteBankCount` to a signed `JL` with `unsigned short`, but to `JB` with
  `unsigned int`. BC++ 3.0/3.1 treat the two alike. Promotion semantics therefore differ by compiler
  band (`X_*` bands in `bak/RECIPES.MAK`).
- **The original writes into string literals** (`CFGPARSE.C`'s `g_cfgResourceDrivePrefix = "x:"`;
  `resblit_load_asset_table` turns every `"name.bmp"` it is given into `"name.bmx"` in place). DOS had no
  memory protection and Borland's `-d` (merge duplicate strings) is off, so every literal is its own
  writable copy. The game library is compiled the same way (`/GF-`, `-fwritable-strings`).
- **A save sets the frame countdown nobody waits for.** `savegame_write` sets
  `g_nFrameTickCountdown` to 30000, about two minutes of the game's ticks. Every loop that waits
  for the countdown to reach 0 sets its own count first, and the inventory's drag clears it at its
  start (INVENTOR.C), so nothing stalls after a save.
- **Restore Game's folder list does not move by keys.** The dialog scrolls it on Up or Down with Ctrl
  held (`mainmenu_save_load_game_dialog`, `key_is_down(1Dh)`), the games list without; but
  `menupage_run` gives a page that asks for the arrows as keys (REQ_LOAD.DAT's first word, 2) only
  those pressed while Ctrl is up, and with it down moves the pointer between the page's buttons. The
  folders are chosen with the mouse (the `autosave` replay clicks).
- **The EMS globals were misnamed.** In `SYS/EMSDET.C`, `g_ems_free_pages` receives the page-frame segment
  (INT 67h AH=41h), `g_ems_total_pages` the free page count (AH=42h) and `g_ems_frame_segment` the handle
  (AH=43h). The module is gone natively (EMS is plain memory), and `g_free_memory_kb` counts 16 KB pages.
- **EMS chains are told from segments by value.** The image code (EMSIMG.C, TTM.C, RESBLIT.C) takes a
  record's first word under 300 for a page chain's number. Chains are numbered from their first page (up to
  512 with 8 MB), so a table whose chain starts past page 298 would be read as a segment.
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
- **The cast screen finds its caster's place through its neighbours too.** `cspell_cast_menu_loop`
  draws a highlight over the caster's portrait when a spell is cast in the world, at an x it reads
  82h past `__dat_15dc` and a y it reads 4 past `g_nSpellSymbolAnimFrame`. In DGROUP both land in
  the 12 bytes after them, the portraits' places by party slot. Natively the reads went to
  other globals: ASan reported the first cast by hand (the `cast-world` replay, 2026-11-02).
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
- **The AdLib driver's bank runs past its buffer.** Slot 1 copies 1507h (5383) bytes into a 5320-byte
  buffer of 190 patches of 28 bytes: programs 0-127, then the rhythm channel's notes 1Bh-58h as programs
  80h-BDh. The next 62 bytes land on the table after the buffer, which ADL.ASM calls a constant velocity
  table (all 3Ch in the file): they are the rhythm notes' pitches, which the render reads in its place.
  The last byte lands on the patch staging buffer, which the reset that follows overwrites. The game's
  bank, SX.OVL's `SSM:003:`, unpacks to 5384 bytes and is passed past a header of two bytes or more, so
  the copy reads at least a byte past it; natively that byte is in the DOS block's slack.
- **A high pitch keys an AdLib voice on.** The render puts the octave less one (a note times 4, bent,
  over 48) in register B0h's bits 2-4 by shifting it left by 2, so a pitch value from 432 up (note 6Bh
  bent up by four steps, or a rhythm note's pitch from 6Ch) reaches bit 5, the key-on: the voice plays
  eight octaves down, and a note-off does not release it while the pitch stays. Its test for a pitch of
  FFFFh, meant to give up on the note, never holds: the bend keeps the pitch at 1FCh or below.
- **The AdLib driver's reset swaps BX and CX.** It pushes BX then CX and pops BX then CX, so slot 2
  returns the caller's BX in CX; the sequencer reads neither (`DudeRegs` passes BX, as 0).
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
- **The rotated blit keeps the left edge's rows up to the screen's width.** `stretch_walk_x_clipped`
  records the quad's left edge row by row for the rows from 0 up to `g_wScreen_width` (320), not
  the height, and clamps each x at the clip rectangle's right only. Of two corners level at the
  top it takes the first in order, then the one before it at exactly 90 degrees, and the one after
  it when the bottom two lie level at an angle under 180 degrees. The translation keeps it.
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

- **The Sound Blaster driver's probe leaves IRQ 2, 3 and 5 unmasked.** ASB.ASM finds its IRQ by
  hooking lines 2, 3, 5 and 7 and playing a one-sample block. Each hook saves the 8259's mask before
  unmasking its line, over the save before, so the four unhooks all restore the mask the fourth hook
  found: lines 2, 3 and 5 stay unmasked, with their vectors back to what they were.
- **The Sound Blaster driver's probe needs its interrupt to come late.** It sends the one-sample
  block, then clears the byte its handlers set, then waits for it: an interrupt in between would be
  lost. On the card the sample takes 156 µs (time constant 64h), far longer.
- **A failed install of the Sound Blaster driver leaves the machine broken.** When slot 0 fails, the
  game shuts the driver down (SNDINST.C calls slot 2). That waits for the DSP to take its halt
  command with no limit, so with no card at the base the game hangs at start-up. With a card whose IRQ
  the probe missed (or a DSP older than 1.01), it restores line 0's vector, the timer's INT 08h, from
  a save slot nothing wrote, 0000:0000 (and with a too old DSP the 8259's mask from another, 00h), and
  it closes DOS handle 0: the file handle it keeps is set to FFFFh only at the end of a successful
  install. Natively the card is always there and answers.
- **The Sound Blaster driver adds to DL alone.** Most of its DSP ports are the base plus a register
  in DL (the carry lost; read data then the status port less 4, the borrow lost), some in DX. Bases
  from xF2h on reach other ports; the card's bases (210h-280h) do not.
- **The Sound Blaster driver's last byte is a CD-ROM check.** When the byte at 16FBh is CDh, slot 0
  refuses to install unless MSCDEX says the current drive is a CD; the shipped image has DCh.

## Black-box comparison with the original

`port/tools/dosbox_golden.py` runs the user's KRONDOR.EXE in DOSBox Staging (0.84 or later) and the port
on the same data, and compares the pictures they end on, pixel by pixel in DAC colours. DOSBox runs headless,
on its own settings and with sound off. The game folder is mounted under an overlay, so the original's
writes stay out of it. Keys go in through DOSBox's `AUTOTYPE`: a key takes a pace (0.5 s) and a comma
pauses two, as the BIOS keyboard buffer showed on 2026-11-02. Until then the tool counted a comma as one
pace, so every key but the first came late (N at 39 s for 28, the world scenario's 24 Spaces 7.5 s apart
from 132 s to 306 s), the last sixteen Spaces falling into the world and its screens, where a dialog
took them for its pages; these notes said AUTOTYPE stopped typing after three minutes, which it never
did. The keys after its line are put straight into the BIOS keyboard buffer through the API
(`DosBox.press`: the key, then the tail, moved only if the game left it where it was), where the game's
keyboard interrupt queues them and `kbhit_read` takes them. The API moves no mouse, and the game's records are written instead, found by
the reference build's map (`DosBox.symbol`, the publics' segments counted from the PSP): its pending
teleport (`Teleport`, 23h into `g_gameState`, as the port's scripted teleport fills it: the world loop
carries it out at its next pass, entering a town scene named first) and the mouse state its callback
latches (`Click`: MOUSE.ASM's position in mickeys, then the button down for half a second, then up;
`Move`: the position alone; `Drag`: the button held while the position moves), which its queries
read, and the game state's fields behind the events a scenario writes (`Event`: the chapter, the
world loop's exit request, as the port's `event:` step writes them). A key may wait for the
picture to change and be pressed again if it did not (`Key(changes=True)`), where the original
sets its moment (a step, a dialog's page). The picture is read through
DOSBox's HTTP API: a small routine hooked on IRQ 0 saves the VGA registers and the DAC, and selects each
plane in turn while the API reads A000h. Captures and diffs go to `work/golden/<scenario>/`.

The saves both sides leave are compared too (2026-11-02): TEMP.GAM, the working save, after every
scenario (the overlay holds the original's), and the save files a scenario names, byte for byte,
each run of differing bytes placed in TEMP.GAM's regions (GSTATE.H's offsets) and the records'
pointer fields set apart (the original writes its addresses there, the port 0: "TEMP.GAM and
saves"). So are the random bits of the encounter object states: when an encounter's actor is
first placed, `rgnenc_load_encounter_actors` puts `RND(3)`, and 4 on a second draw, into the
low byte of its state word, which nothing reads (every reader masks the word with FF08h and
takes its high byte). Where the original's generator stands then depends on its timing, since
its cosmetic draws share it, so the byte differs from run to run there; the port's cosmetic
draws have their own stream. The `fight-cast` scenario's fight saves one (2026-11-02). A scenario may have a prelude, a script the port runs first on the virtual clock from a
fresh user folder, whose files (a quicksave) both sides then start with: laid in the port's user
folder and in the overlay, where the original finds them as its own. A scenario may also run the
port with a diagnostic setting that makes a picture the original's where the game departs from
it: `BAK_GOURAUD_SKIP=original` keeps a clipped column's skip count for the columns after it in
GOURAUD.ASM's quads, as the original does and the game does not ("Bugs of the original, fixed").

| Scenario | Result |
|---|---|
| `main-menu`: Escape during the intro | ✅ identical (the original shows the page at 4000h) |
| `book`: N (Start New Game) after the intro, chapter 1's first page | ✅ identical, 640x350 in 16 colours (the original shows the page at 8000h) |
| `world`: N, Space through the book, the chapter cutscene played out: the 3-D world's first view | ✅ identical: the sky and its panorama, the hills, trees and road, a sprite, the compass and the party (in DOSBox the view comes after about 4 minutes; the capture waits 7) |
| `map`, `inventory`, `camp`, `options`: the world, then M, 1, E or O (in DOSBox at 240 s, in the keyboard buffer; the world comes near 175 s there, the Spaces past the book paging through the cutscene) | `map` ✅ identical: the zone's map, its terrain and the party's mark (2026-11-01). `inventory` ✅ identical: Locklear's, his sword, armour and pack, the party's gold. `options` ✅ identical: the Options menu over the world, Restore Game off (no save). `camp` ✅ identical: the clock's dial with its hand (VGA driver slot 39, ported for it the same day: until then the port stopped there, and the party could not camp), the party's health, stamina and rations |
| `save`: the world, then O and S: the Save Game dialog | ✅ identical but for the Games field's caret, which blinks on the game's timer, so each side shows its own phase (the scenario ignores its column, x 127, y 129-139: the field is empty, the caret at its start; until AUTOTYPE's timing was right, spare Spaces from the world's script had been typed into it, the caret at x 147). The SAVES folder the GOG copy's GAMES holds is listed on both sides (the port copied it into its user folder). A capture waits for two pictures that agree, a second apart and then a fifth of one: one second apart the caret never lets them |
| `town`, `shop`: the world, then a teleport record with a town scene and zone 255 (as TELEPORT.DAT's records for the scenes: the party stays), written into the original at 240 s and scripted on the port: LaMut's town scene (GDS1A.DAT), and the Fletcher's Post (GDS1C.DAT) with its purchasing counter clicked (at 255 s in DOSBox, 95 s on the port): the merchant's screen | ✅ identical (2026-11-02): the town's picture and its description; the shop's wares, their prices (the base prices times the shop's markup: the random roll is the haggle's), the party's portraits, gold and the cursor over the counter |
| `talk`: the Blue Wheel Inn (GDS1B.DAT), its dwarf clicked and his tale paged by three clicks on the parchment: Dubal An Loch drawn over the scene, his name and his words | ✅ identical (2026-11-02): Dubal An Loch's sprite over the inn with its anti-aliased edges (BLITAA, PALBLEND), his name's banner, his words on the parchment (this page draws its parchment from a fixed start, so the pin below was not needed), and the cursor where the last click was |
| `inn`: the Blue Wheel Inn (GDS1B.DAT) itself | ✅ identical (2026-11-02), once the rasterizer's edge stepper took the quotient's sign as DOSBox gives it (see "Flags after IDIV" under what the port has taught us): until then its three mirrored patrons, drawn shrunk by `polyrast_spr_scaled_blit_planar`, were sampled a column off in strips every six columns (596 pixels), their edges and the fourth, unmirrored patron right |
| `load`: the port's quicksave, made by the scenario's prelude (the world, two steps, a turn left, F5, on the virtual clock), laid in both folders as `GAMES\SAVES.G01\SAVE00.GAM`; from the main menu Restore Game (R), the Bookmark clicked in the Games list, Return: the full map with the chapter's caption while the zone loads, then the world | ✅ identical (2026-11-02): the world two steps south of the start, after one turn left (heading 8400h, a sixty-fourth of a circle off south), the party where the save was taken; both working saves are the quicksave's body |
| `savegame`: the world, then the Save Game dialog (O, S), "test" typed as the save's name, Tab, "one" as the folder's, Save clicked (242,158): the world again | ✅ identical (2026-11-02): the picture, and both `GAMES\ONE.G02\SAVE01.GAM` and the working saves but for the records' fourteen pointers ("TEMP.GAM and saves") |
| `fullmap`: the world, then F: the full map (FMAP.C), the pointer moved over Krondor (`Move`: the mouse's position poked, no button) | ✅ identical (2026-11-02, the screen's first native run): the map and Krondor's label over its hotspot. The party's marker cycles through its icons every few ticks, so its box is set apart, while the picture settles too |
| `rest`: the Blue Wheel Inn's stairs (GDS1B.DAT's actor 2) clicked: the nightmaster's price for the night over the camp's clock (`modalscreen_rest_until_time`) | ✅ identical (2026-11-02, the screen's first native run): the clock, the party's health, stamina and rations, its gold, the dialog with Yes and No |
| `bard`: the Blue Wheel Inn's lute (actor 3) clicked: the party's best at barding plays, Owyn, and is thrown out (`townscene_resolv_enc_outcome`: no random draw) | ✅ identical (2026-11-02, the first native run) |
| `cast`: the world, then C and 5: the cast screen's fifth symbol page (Scent of Sarig), the highlight over the caster's portrait | ✅ identical (2026-11-02): the highlight over Owyn's portrait, where the port drew it in other globals' places until the same day (see what the port has taught us) |
| `tavern`: town 2's Rainbow Parrot Inn (GDS2C.DAT), whose idle animation hangs a lute on the wall, a 39x22 sprite turned upright (ROTBLIT.ASM, VGA slot 36) | ✅ identical (2026-11-02): until ROTBLIT was in C the same day, entering the inn stopped the port |
| `lockpick`: the world, then a teleport record beside the locked house a tile east of the start (zone 1's tile 11,16), the house clicked in the view, and Y to the question whether to try its lock | ✅ identical (2026-11-02): the lock and the party's key ring (PICKLOCK.C) |
| `price`: the Fletcher's Post (GDS1C.DAT), its counter clicked, and its Quarrels dragged onto Locklear's portrait (`Drag`: the button pressed on the wares and held while the poked position moves to the portrait) | ✅ identical (2026-11-02): "That will be 13 sovereigns and 2 royals," demanded the shopkeeper, with Accept, Haggle and Decline; the dialog's parchment pinned to the original's start. The haggle after it draws from the game's generator, whose sequence the cosmetic stream changed: its outcome is no golden |
| `fight-cast`: chapter 1's first fight, as the `combat` replay enters it (a teleport two cells north of it, a turn, seven steps, the assassin's dialog paged: in DOSBox each key waits for the picture to change), Owyn's turn: C, the second symbol page (2), and the pointer over Despair Thy Eyes | ✅ identical (2026-11-02): the cast screen over the fight with the spell's cost, damage and line of sight; TEMP.GAM identical but for an encounter actor's random bits (above). The fight's own view is no golden: its people fidget at moments of the cosmetic stream |
| `labyrinth`: the world, then a teleport record into zone 10, a labyrinth: its corridor, the walls GOURAUD.ASM's textured quads, where the port stopped until the module was in C | ✅ identical (2026-11-02), the port drawing the quads the original's way for the skip count; TEMP.GAM identical |
| `road`: the world, then a teleport record onto the road south of the start (zone 1's tile 10,15, cell 18,30), facing 400h left of south: the original skips a teleport to another row of the party's tile column at the heading it keeps ("Bugs of the original, fixed"), and the trees there, among the scaled sprites, bring GOURAUD.ASM's quads | ✅ identical (2026-11-02), the port drawing the quads the original's way for the skip count; TEMP.GAM identical |

The eight scenarios recorded before AUTOTYPE's timing was right (`main-menu` to `save`) were run
again under it on 2026-11-02: all identical, `save` once its caret's column moved (above).

The capture also reads the attribute controller, and the decoder handles Mode Y and the 16-colour planar
modes.

## Replays

`port/tools/replay.py` runs a scenario's scripted input headless on the virtual clock, from a fresh user
folder, and compares three logs with a baseline recorded in `work/replay/<scenario>/`. The fresh user folder gets
the game's preferences the baselines were recorded with (KRONDOR.CFG: step and combat step speed 0,
detail 3, CD music off unless the scenario wants it), so the game folder's own file plays no part.
Autosaves are off unless a scenario turns them on (`saves.autosave=false`; their folder would show in
the save dialogs), and so they are in the DOSBox goldens' port runs. A scenario may also check what the
run left in its user folder (`SCENARIO_CHECKS`).

- **The virtual clock** (`BAK_CLOCK=virtual`, `port/platform/clock.c`): each poll costs 1 µs, and where the
  game waits (an idle poll, the retrace) the clock jumps to the next timer tick or look at the host's
  events. The game itself reads no wall clock: the PIT, the retrace, the scripted input and the frames
  shown are all that move with time, and they all follow this clock.
- **`BAK_FRAME_LOG`**: the time, size and hash of each picture shown that differs from the one before.
- **`BAK_STATE_LOG`**: written on each pass of the world loop when something changed. It holds hashes of
  the game state in TEMP.GAM's layout, of TEMP.GAM (read through the game's own stream) and of the random
  generator, and the party's position and heading.
- **`BAK_AUDIO_LOG`**: what the sound was told (the CD's plays, with the track and frame they start at,
  and its stops), and last the length and hash of the sound rendered into `BAK_DUMP_AUDIO`'s WAV file
  (kept as `audio.wav`).

| Scenario | Result |
|---|---|
| `main-menu`, `book`, `world` (the golden scenarios' scripts) | ✅ two runs give identical logs; each ends on its golden capture. The world tour takes about 19 s, against 90 s on the host's clock, because computing costs no game time |
| `map`, `inventory`, `camp`, `options`, `save` (the golden scenarios' scripts): the world, then M, 1, E, O, or O and S | ✅ identical logs, the ASan build's too (recorded 2026-11-02): the zone's map, Locklear's inventory, the camp's dial, the Options menu, the Save Game dialog |
| `fullmap`, `rest`, `bard` (the golden scenarios' scripts): the world, then F and the pointer over Krondor; the Blue Wheel Inn's stairs clicked; its lute clicked | ✅ identical logs, the ASan build's too (2026-11-02, the first time these screens ran natively): the full map with Krondor's label (LaMut's too, in a longer run), the nightmaster's price for the night over the camp's clock, and Owyn's failure at the lute. Played on: a night's rest (each member eats a ration, then the inn again) and the party thrown out into LaMut |
| `cast`, `tavern` (the golden scenarios' scripts): the world, then C and 5: the cast screen's fifth page with Scent of Sarig, Owyn's portrait highlighted; town 2's Rainbow Parrot Inn (GDS2C.DAT), whose idle animation hangs a lute on the wall, drawn rotated | ✅ identical logs, the ASan build's too (2026-11-02): until ROTBLIT was in C, entering the inn stopped the port |
| `price`, `fight-cast` (the golden scenarios' scripts): the Fletcher's Post's Quarrels dragged onto Locklear's portrait (a press on the wares, held while the pointer moves): the shopkeeper's price; chapter 1's first fight, Owyn's turn, C, 2 and the pointer over Despair Thy Eyes | ✅ identical logs, the ASan build's too (2026-11-02) |
| `finale`, `finale-paged`: the world, then the chapter set to 9 and the world loop's exit request to 2 (`event:` steps, as the story's last dialog writes them): chapter 9's last cutscene (C93.ADS), its first page (the golden scenario's script); and the cutscene paged through by Space every 4 s, then the world again, in chapter 9 | ✅ identical logs, the ASan build's too (2026-11-02): the cutscene's first native run. Each speaker's words come with the dialog's wipe, drawn over the last one's from the middle out |
| `sweep-01` to `sweep-12`, the zone sweeps: from the world, the party teleported to the middle of every tile of a zone that has a WLD file (351 in the twelve zones), at 8 headings 2000h apart, a teleport every 200 ms of game time; the script made from the game's archive and read from a file (`BAK_INPUT=@file`: a zone's is longer than the environment holds) | ✅ identical logs on two runs (2026-11-02): 5,414 to 21,038 pictures a zone, the software renderer's views as the baselines M7 will keep. A teleport sets off no encounter (steps do, and a dialog's action), so nothing waits for a key. Zone 9 stopped the port until the same day, at the records of noise in its tile 10,11 ("Bugs of the original, fixed"); every other zone ran clean |
| `walk`: the world, then three steps, a turn left, a step, two turns right, a step | ✅ identical logs; the state log shows each 400-unit step and each 1024-unit turn |
| `walk-instant`: the walk with `video.step_time=0`, compared with `walk`'s states alone (times set aside) | ✅ the same states: smooth steps are a picture only. When they came (2026-11-01) every scenario kept its states, times aside; in `hold-key` and `hold-button` the held key's next press waits during a step's frames, so the pass after a step takes the next one too, and the state between them (the step's time added) is no longer logged: their states are an ordered part of the earlier ones, the same steps to the same end |
| `combat`: the world, then a scripted teleport beside chapter 1's first fight (the assassin, zone 1's tile 10,14), seven steps into it, its dialog paged through, auto-combat (A) to the end, Space for the victory's dialog, two steps past the corpse | ✅ identical logs, the ASan build's too; the fight's result shows in TEMP.GAM and the random state after it. The AI's healing choices (Gift of Sung for a hurt ally) are checked by `test_cbtai.c`: in chapter 1's first fights damage takes stamina first, and no replay reaches a cast |
| `spawns`: the world, then a teleport into zone 4 (tile 10,14) among the sixteen stones chapter 1 spawns there, and a full turn about | ✅ identical logs, the ASan build's too; the stones live in the visible pool, past the zone pool's first 64 KB natively, and did not show before the visible list held pointers |
| `riddle`: the world, then the map (M) and its hidden riddle chest: the grave key held with the right Shift and the left Alt shows the Moredhel lock vision, whose dialog plays a sound and waits for its end, then the chest's word lock: its dialogs, the first dial turned, Exit, the two dialogs after it, back on the map | ✅ identical logs, the ASan build's too. Before the waits for a sound polled, the run stopped at the vision: the sound never ended |
| `music`: the world's script with CD music on | ✅ identical logs, the ASan build's too: tracks 24, 16, 11, 31 and 36 play, each from 39 frames before its start, and the sound renders the same every run. The world comes at 38 s, 3 s sooner than with the MT-32's music, whose changes of track wait for a fade |
| `music-menu`: the main menu with CD music on, a minute | ✅ identical logs, the ASan build's too: track 16 plays out its 3348 frames |
| `quicksave`: the world; the Save Game dialog (O, S, a folder and a save name typed, Save); F9, which finds no Bookmark; two steps, F5, two steps, F9, and Space through the load's map and caption | ✅ identical logs, the ASan build's too: the dialog writes `GAMES\one.G02\SAVE01.GAM` and F5 `SAVE00.GAM`, saving leaves the random state alone, and F9 puts the party back at F5's place, not the dialog's: TEMP.GAM's hash and the game state after the load are those right after F5 |
| `quicksave-first`: the world, F5 as the first save of a fresh user folder, two steps, F9 | ✅ identical logs, the ASan build's too: F5 makes `GAMES\SAVES.G01` for its Bookmark, and F9 brings the party back |
| `world-autosave`, `combat-autosave`, `spawns-autosave`: those scripts with autosaves on, one a minute while the game's time moves (`saves.autosave_minutes=1`), checked against their baselines | ✅ identical logs, the ASan build's too: autosaves leave the game where it was. The world saves the chapter's start on its first pass; the fight saves it too and, a minute on, the party past the fight (the corpse's tile, after the victory); the teleport saves on arriving in zone 4, among the spawned stones. Each `AUTOSAVE.G20` holds two, the newest first |
| `autosave`: the world (its chapter's start saved), two steps, a teleport into zone 4 (saved on arriving), F5 there; then Restore Game, the AUTOSAVE folder and its second save clicked, Return, Space through the load, a step | ✅ identical logs, the ASan build's too. The zone 4 autosave is byte for byte the Bookmark F5 saved at the same place, but for its title ("Autosave: day 3, 08:02"; checked after the run), and the load puts the party back at the chapter's start (669600,1064800). The dialog lists the folder after SAVES. The folder is clicked: its keys cannot reach the folder list (below) |
| `lockpick`: the world, then a teleport beside the locked house a tile east of the start (zone 1's tile 11,16), the house clicked, Y to the question whether to try its lock | ✅ identical logs, the ASan build's too (2026-11-02, the screen's first native run): the lock and the party's key ring (PICKLOCK.C) |
| `cast-world`, `cast-combat`: spells cast by hand. In the world C, the fifth symbol page and Scent of Sarig, Space past its message; in chapter 1's first fight (the `combat` scenario's way in), Owyn's turn (he is the fastest): C, the second page, Despair Thy Eyes, the moredhel's square | ✅ identical logs, the ASan build's too (2026-11-02, the first casts by hand). The world's cast screen highlighted its caster at places read past two other globals until ASan found it (see what the port has taught us); the fix changed its pictures, not its states. In the fight the cast ends Owyn's turn and Locklear's comes |
| `camp-rest`: the world, E, a click on the camp dial's 14:00: the party rests six hours from 08:00 and the camp ends by itself, then a step | ✅ identical logs, the ASan build's too: the clock's hand moves an hour at a time (slot 39 for each picture), and the game state after the camp has the six hours |
| `teleport`: the world, then a teleport two rows south in the party's tile column (zone 1's tile 10,14) at the heading kept from the zone's entry | ✅ identical logs, the ASan build's too; the state log shows the party at the destination (685600,916000) and the random state moved by the actors loaded around it. Without the fix of the teleport test the party stays at the start (669600,1064800) |
| `town`, `shop`, `inn`, `talk` (the golden scenarios' scripts): the world, then a teleport record naming a town scene with zone 255 (the scene only: the party stays): LaMut's town, the Fletcher's Post with its counter clicked (the merchant's screen), the Blue Wheel Inn, and the inn with its dwarf clicked and his tale paged by clicks 4 s apart (Dubal An Loch's portrait over the scene) | ✅ identical logs, the ASan build's too (2026-11-02). The scenes' pictures settle within a second of game time; the state log has the world's first pass and the scene's entry only. The inn's patrons are drawn stretched (VGA slot 37 for TVRN2.BMX's pieces, the rasterizer's scaled blit for the people: until the same day the inn stopped the port at slot 37). The rasterizer's stepper fix changed one picture of `combat` (a fight frame with a flipped sprite; its states stayed) and a frame of the shop scene's entry; both were recorded again |
| `savegame`, `load` (the golden scenarios' scripts): the Save Game dialog's save and the world again; the port's quicksave, made by the scenario's prelude, restored from the main menu | ✅ identical logs, the ASan build's too (2026-11-02): `savegame` leaves `GAMES\one.G02\SAVE01.GAM` titled "test" with the working save its body, and after `load` the working save is the quicksave's body (both checked after each run) |
| `road`: the world, then a teleport onto the road south of the start (zone 1's tile 10,15, cell 18,30, heading 8000h), two steps, a turn left, a step, two turns right, a step | ✅ identical logs, the ASan build's too (2026-11-02): the trees there are GOURAUD.ASM's textured quads, where the port stopped until the module was in C |
| `labyrinth`, `labyrinth-instant`, `labyrinth-autosave`: the world, then a teleport into zone 10, a labyrinth (mode 2), two steps, a turn left, a step, a turn right, a step; the same with the party's steps at once, and with autosaves on | ✅ identical logs, the ASan build's too (2026-11-02): the walls are GOURAUD's quads; the steps at once give the same states (smooth steps are a picture only in a labyrinth too), and the autosaves (one on the zone's entry) leave the game where it was |

On the virtual clock the game reaches the world sooner than on the host's (its last new picture comes at
41 s of game time), since decoding and drawing take no game time; the golden scripts still work.

When the dialogs' parchment moved to the cosmetic stream (2026-11-01) the scenarios without a
dialog before their game logic kept their states, the times too, and their pictures shifted where
the cosmetic stream's next draws did (the ground's pattern after a step). The quickloads' random
state after the load changed (its caption is a dialog), and `combat` fights with other rolls: the
assassin still falls, and its victory's dialog now waits for a key (dialogs leave the arrows alone),
so the script presses Space before the two steps past the corpse.

Since the Sound Blaster became the default (2026-11-01) the replays run on it. Every scenario kept
its states, the world's even their times, and its pictures in their order: only the first came a
frame later (the drivers' install at start-up); the audio logs' sound now holds the AdLib's music
and the effects.

The pictures have no cursor since the system cursor became the default (2026-11-01), and the baselines
were recorded again then: every state log stayed identical, and with `--set video.cursor=game` (the
tool passes `--set` on to the port) all seven frame logs matched the earlier baselines exactly.

## Platform services

| Service | Implementation |
|---|---|
| Interrupts | `port/platform/machine.c`: host events and a virtual 8253 become interrupts when the game polls (`bak_poll`: its wait loops, input and timer reads, retrace waits). Vectors 08h/09h/1Ch are a table the install routines replace and chain through; the BIOS INT 8 handler counts time and calls 1Ch. Reading the variables the timer advances (`g_nFrameTickCountdown`, `g_timer_ticks`, `g_dwSysTickCount`) polls first, so bare spins on them work, and so do the mouse queries (`mouse_button_pressed`, `mouse_get_position`): the book waits in a bare loop for the buttons' release. The waits for a sound to end poll too (`BAK_IDLE`: on the host's clock it sleeps a little when nothing was due) |
| INT 33h mouse | `port/platform/mouse.c`: the host pointer's position over the picture, in the ranges the game sets (four units per pixel); the driver calls the game's handler as an interrupt |
| Keyboard | `port/platform/scancode.c`: host keys as scan code set 1 bytes (E0h-prefixed for the 101-key additions), one IRQ 1 each, read by the game's handler at port 60h |
| BIOS data area | `bak_bios_data()`, also at segment 0040h: keyboard buffer and shift state, CRTC port, time of day |
| MSCDEX | `port/platform/cdrom.c`: absent (as without the disc) unless the game folder holds the CD's tracks (`music\bakNN.ogg`), whose drive then plays them (see "The CD's music"); the tests attach drives of their own. Drive check, IOCTL audio-track info, play (from an HSG sector), stop |
| Scripted input | `port/platform/inputscript.c`: `BAK_INPUT` lists timed key presses (held 100 ms, or as long as given), pointer moves, clicks and holds (in picture pixels), wheel notches, gamepad buttons, teleports, events and a quit, for runs without a user; times are on the platform's clock. `BAK_INPUT=@<file>` reads the steps from a file (line ends separate them too), for a script longer than the environment holds (32 KB on Windows). A teleport fills in the game's own pending teleport (`port/platform/teleport.c`), which the world loop carries out at its next pass, as for TELEPORT.DAT's destinations: the zone's actors and encounters are loaded around the new place. It may name a town scene (its kind and index) to enter first, and with zone 255 enters the scene and moves nobody, as TELEPORT.DAT's records for the scenes do. An event is written as a dialog's action writes it (`gstate_event_write`, `port/platform/gameevent.c`): a flag of the event bitmaps, or a game state field such as the chapter (7537h) or the world loop's exit request (7541h: 1 ends the chapter, 2 plays its third cutscene), which reaches what only the story's far end sets |
| Clock | `port/platform/clock.c`: the host's monotonic clock, or (`BAK_CLOCK=virtual`) one that moves only as the game runs, for replays (below) |
| Window | `port/platform/video.c`: the picture the CRTC scans out goes to the window through OpenGL 3.3 core (`port/platform/glpresent.c`, since 2026-11-02: the context the 3-D view will be drawn in at the display's resolution), or SDL's renderer (Direct3D 11 on Windows) where the driver offers no 3.3 context, under SDL's dummy video driver, or as `video.renderer` asks. Both scale it as the settings say: pixel art by default, which the shader does as SDL's renderer does (each window pixel blends the picture pixels its footprint covers, the linear filter between two at an edge), nearest or linear, letterboxed at 4:3 or square, by whole multiples if asked. The shaders keep to what GLSL 3.30 and GLSL ES 3.00 share, so that only their version line changes for the web. `bak_unittest` draws through it into an off-screen target and reads the pixels back (skipped without a 3.3 context, and under ASan, which stops at SDL's video naming its input devices: a move within one buffer, as in the gamepad mappings). Alt+Enter and F11 switch the borderless full screen. The window's place, size and state are saved when it closes. A picture goes to the window only when it differs from the one before, and at most once per refresh of the display, the latest one waiting until then (8 s of the intro: 309 pictures, 267 presented; every 70 Hz tick presented before, and a retrace wait a second time). Vsync stays off: the game paces its frames itself. `BAK_TRACE` names the video driver, the renderer and the scale mode, and counts the pictures and presents |
| Cursor | `port/platform/cursor.c`: the game's cursor as the system's (`video.cursor`). The image the game would draw is read in its driver's format (the VGA driver's bytes; the 16-colour driver's bit planes, for the books) and shown as an SDL colour cursor, scaled with the picture, its hot spot the game's. It takes the DAC's colours and is made again when they change, so it fades with the picture. It shows while the game shows its cursor (`g_cursor_shown`, and not shape 2); `screen_cursor_hide` only skips a background restore and hides nothing. Where the game moves the mouse (the menus' keys move it to a button), the host's pointer goes along |
| Files | `port/compat/bakrtl.c`: the game's DOS paths (any case, backslashes, a drive letter) are looked up in the user folder, then in the game folder; writes go to the user folder (see "Settings and folders"). `chdir` tells whether the folder is there, looked for as reads are: the game asks it whether `GAMES` exists (`mainmenu_save_games_dir_exists`), and the Save Game dialog makes one, with a first `SAVES.G01`, when not (`mainmenu_save_games_dir_create`). Until 2026-11-01 it answered yes to everything, so a first save failed in a user folder without `GAMES`. `findfirst` keeps a search in one of eight slots until it ends, where DOS keeps it in the caller's DTA, and the game leaves searches unfinished (`mainmenu_save_any_exists` stops at the first save, each time a menu opens): when none is free, the one stepped longest ago is dropped. Until 2026-11-01 the ninth search failed, so after a few visits to the menus every save list came up empty. Host paths are UTF-8, SDL's and the C runtime's alike: on Windows the executable's manifest (`port/platform/betterkrondor.manifest`) makes UTF-8 the process code page (Windows 10 1903 on) |
| Sound | `port/platform/audio.c`: sources (the CD's music, the synthesizer, the samples) each render their own format into an SDL audio stream, which converts it to the mix (48 kHz, stereo). What the game tells a source is an event stamped on the platform's clock: inside the timer interrupt, when its tick was due (`machine_event_ns`), so the bursts of late ticks the platform runs do not jitter. A source carries out each event between the frames of its moment, 50 ms later. On the host's clock SDL's audio thread renders the mix as the device asks, and the epoch follows the device's drift once it passes 20 ms; on the virtual clock the mix is rendered as the clock moves into the WAV file `BAK_DUMP_AUDIO` names, the same every run. Nothing the game reads depends on the rendering |
| OPL2 | `port/platform/opl.c`, ports 388h/389h: each register written becomes an event of the synthesizer's audio source, which renders the chip through ymfm (`port/platform/ymfm_opl2.cpp`, ymfm fetched by CMake at a pinned commit, built as a C++ library of its own) at the AdLib's 3579545 Hz over 72. The chip and its source are made at the first write. The status port reads 06h, an idle OPL2's: the AdLib driver starts no timer. `bak_opl_set_sink` gives the tests every register written, in order |
| Sound Blaster | `port/platform/sbdsp.c`: the DSP at 220h, a Sound Blaster 2.0's (version 2.01: reset, identification E0h, version E1h, time constant 40h, single-cycle 8-bit DMA output 14h, hold D0h and go on D4h, speaker D1h/D3h), the 8237's channel 1 (ports 02h, 03h, 0Ah, 0Bh, 0Ch, 83h) and IRQ 7. A block lasts its samples times 256 − tc microseconds from the moment it started on the platform's clock; its interrupt is delivered at the first poll from then on (`machine_raise_irq`), stamped with that moment, so a block its handler starts follows without a gap, and the DMA's count counts down with it. A masked channel holds a block back until it is unmasked; a reset ends it without its interrupt. The block's bytes are read through the 8237 when it starts: a 20-bit address is a paragraph of the segment table (a DOS block's) and wraps within its 64 KB page as the 8237's does; they become an event of the card's audio source, played at the block's rate with linear interpolation, muted by the speaker switch. `bak_port_set_trace` gives the tests every port access |
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
- **ROTBLIT past its tables.** A table read past `jmp_via_2f48`'s frame (into its caller's) and a
  source table read or written past its 1040 words (into other DGROUP data) stop with a message.
  A quad whose corners all lie at row 32000 or below draws nothing: the original takes its top
  corner from whatever DX held. Within the frame the port's reads find what the original's find.

Placeholder values to revisit: the remaining ASM data tables (zero), the emulated DOS free memory
(640 KB) and the EMS pages counted (8 MB).
