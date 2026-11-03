/*
 * bak_port.h - services the native platform layer provides to the game code.
 *
 * Shared by game translation units (through bak_native.h) and port code. It
 * declares no structures, so it is safe to include with or without packing.
 */
#ifndef BAK_PORT_H
#define BAK_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Segment emulation. The game builds far pointers from 16-bit DOS segments
 * (VGA pages at A000-AC00, the EMS page frame, paragraph-aligned DOS
 * allocations) and splits them again with FP_SEG/FP_OFF. Natively each
 * segment value names a registered host block: MK_FP(seg, off) is that block's
 * base plus off, and FP_SEG/FP_OFF of a pointer find the block containing it.
 * Segment 0 maps to address 0, so MK_FP(0, n) sentinels keep their values.
 */
void *bak_seg_ptr(uint16_t seg);
uint16_t bak_ptr_seg(const void *p);
uint16_t bak_ptr_off(const void *p);
/* The segment of p as a normalized far pointer (offset under 16): the paragraph it is in,
 * where FP_SEG gives its block's. */
uint16_t bak_ptr_seg_norm(const void *p);

/* base plus a normalized far pointer's distance from it, paragraphs in the high word and
 * bytes in the low one: what the original gets adding it to a block's seg:0000 as a long. */
static inline void *bak_far_add(void *base, uint32_t distance) {
    return (char *)base + (distance >> 16) * 16 + (uint16_t)distance;
}
uint16_t bak_seg_register(void *base, uint32_t size);
/* The bytes from seg:0000 to the end of its block: 0 for an unmapped segment. */
uint32_t bak_seg_extent(uint16_t seg);
void bak_seg_register_at(uint16_t seg, void *base, uint32_t size);
void bak_seg_unregister(uint16_t seg);

#define MK_FP(seg, off) ((void *)((char *)bak_seg_ptr((uint16_t)(seg)) + (uint16_t)(off)))
#define FP_SEG(p) bak_ptr_seg((const void *)(p))
#define FP_OFF(p) bak_ptr_off((const void *)(p))

/*
 * DOS memory (INT 21h AH=48h/49h). Blocks are host allocations registered in the
 * segment table at a paragraph boundary, so a block's far pointer is seg:0000.
 */
void *bak_dos_alloc(uint32_t size, int zeroFill);
int16_t bak_dos_free(void *block);
uint32_t bak_dos_largest_free_block(void);

/*
 * Plain memory for data no segment needs to reach (the images' pixels, what the original kept
 * in EMS): zeroed, with a DOS block's slack after it, outside the segment table and its 650 KB.
 */
void *bak_plain_alloc(uint32_t size);
void bak_plain_free(void *block);

/*
 * The built-in video driver for a VMCODE.OVL chunk index (8 = "VGA:"), initialized as the
 * chunk's init entry would be, returned as its 50-slot table (a RendererVtable); NULL
 * when the port has no driver for that adapter. Replaces loading and calling the chunk.
 */
const void *bak_video_driver_init(int16_t chunk);

/* INT 10h AX=1130h BH=3: the ROM's 8x8 font, 128 glyphs of 8 bytes. */
unsigned char *bak_rom_font_8x8(void);

/*
 * The INT 33h mouse driver (port/platform/mouse.c), one function per service the game
 * uses: 00h reset (returns FFFFh, present), 01h/02h show/hide the driver's cursor, 04h set
 * position, 07h/08h set the x/y range, 0Ch install the event handler, 0Fh mickey ratio.
 * The handler gets what the driver passes in CX, DX and BX: position and buttons.
 */
typedef void BakMouseHandler(uint16_t x, uint16_t y, uint16_t buttons);
uint16_t bak_int33_reset(void);
void bak_int33_show_cursor(void);
void bak_int33_hide_cursor(void);
void bak_int33_set_position(int16_t x, int16_t y);
void bak_int33_set_x_range(int16_t min, int16_t max);
void bak_int33_set_y_range(int16_t min, int16_t max);
void bak_int33_set_handler(uint16_t eventMask, BakMouseHandler *handler);
void bak_int33_set_ratio(int16_t horizontal, int16_t vertical);
/* Tests: called with AX, CX, DX for every service call. */
extern void (*g_int33Trace)(uint16_t ax, uint16_t cx, uint16_t dx);

/*
 * The game's mouse cursor as the system's (port/platform/cursor.c): the image the game would
 * draw now and its hot spot, or NULL when it shows none. True when the system cursor shows it
 * (the video.cursor setting), and the game then draws nothing.
 */
struct ImageRecord;
_Bool bak_cursor_system(const struct ImageRecord *image, int16_t hotX, int16_t hotY);

/*
 * The page the game polls for input (menupage_run; port/platform/input.c). On the world's and
 * the map's, where the party moves, the movement bindings act (W/A/S/D as the arrows).
 */
enum { BAK_PAGE_OTHER, BAK_PAGE_WORLD, BAK_PAGE_MAP };
void bak_input_page_polled(int16_t kind);
/* A movement button held down with the mouse (the world's and the map's arrows): start timing
 * the hold, then ask on each pass whether a step is due, as for a held movement key. */
void bak_input_hold_start(void);
_Bool bak_input_hold_step(void);
/* The platform's clock in ms, for a hold the game times itself; it polls. */
uint32_t bak_clock_ms(void);
/* Smooth steps (WORLDMOV.C): the frames a step or a turn of the party takes, its in-between
 * places drawn on all but the last (video.step_time at the VGA's 70 Hz); below 2, none. */
int bak_smooth_frames(void);
/* Where a dialog box's parchment pattern starts (DIALOG.C): the cosmetic stream's draw (drawn),
 * or BAK_PARCHMENT's value instead, which the goldens set to the original's own draw. */
int16_t bak_parchment_base(int16_t drawn);
/* Whether GOURAUD.ASM's quads keep a column's top-clip skip count for the columns after it,
 * as the original does (BAK_GOURAUD_SKIP=original, for the goldens); the game resets it. */
bool bak_gouraud_skip_as_original(void);
/* The sound driver the game installs (a SoundDriverId of AUDRESIN.H): the audio.device setting's,
 * or for "game" the one RESOURCE.CFG named (configured). */
int16_t bak_sound_driver(int16_t configured);

/*
 * Autosaves (port/platform/autosave.c; the saves.autosave settings), written by MAINMENU.C.
 * One is asked for when the party enters a zone (zone_load: a chapter's start too) and on the
 * platform's clock while the game's time moves; the world loop asks whether one is due at a pass
 * where the player could save (WORLDLP.C), and the save says when it is done. Starting the world
 * again (GMAIN.C) forgets what was asked.
 */
void bak_autosave_reset(void);
void bak_autosave_zone_entered(void);
_Bool bak_autosave_due(uint32_t gameTime);
void bak_autosave_done(_Bool written, uint32_t gameTime);
/* How many autosaves are kept, the newest first. */
int bak_autosave_slots(void);

/*
 * The 3-D scene as the software pass draws it (port/platform/scene.c; the scene list in
 * scene.h), for the GPU's view (M7). A pass begins where r3d_camera_setup_view sets the camera
 * up; actorrender_entity places each object; R3D's producers hand over each face before the
 * near clip (its corners in camera space: x right, y depth, z up, in the units of the object's
 * level of detail, 2^unitShift world units), then the polygon its sink receives, then what
 * drew it: an image and a remap (fog) table for a textured quad, the ground's strip for a flat
 * face keyed to it. Billboards, circles, and the sky's and the ground's bands come from their
 * own renderers. Remap tables come as the blits' offsets (g_polyRasterState.nRemapTableOff, 0
 * for none), screen positions in pixels. Nothing is kept unless asked (scene_enable, or
 * BAK_SCENE_CHECK, which checks what it is handed and reports at exit).
 */
struct ImageRecord;
#define BAK_SCENE_CORNERS 20
typedef struct BakSceneCamera {
    int32_t pos[3];         /* the camera's world position */
    int16_t angles[3];      /* its angles as the view matrix takes them */
    int16_t clip[4];        /* the viewport: left, top, right, bottom */
    int16_t centerX, centerY;
    uint8_t screenShift;    /* the focal length's log2 (the zone's zoom) */
} BakSceneCamera;
typedef struct BakSceneFace {
    int32_t cam[BAK_SCENE_CORNERS][3]; /* the corners, as the producer holds them */
    uint8_t corners;                   /* how many */
    uint8_t textured;                  /* a textured quad's face: texture is its image's word */
    uint8_t nearClipped;               /* a corner lies before the view plane */
    uint8_t viewportClipped;           /* the object reaches past the viewport: clipped to it */
    uint32_t frontMask;                /* the corners projected (bit i: corner i) */
    int16_t clip[4];                   /* the clip rectangle: left, top, right, bottom */
    uint8_t filled, fill, outline;     /* the colours resolve_shade_color left */
    int16_t texture;
    int16_t centerX, centerY;          /* the viewport's centre */
    uint8_t screenShift;               /* the focal length's log2 (the zone's zoom) */
    uint8_t lod;                       /* the producer's level of detail */
    uint8_t flags;                     /* the face record's flags */
    const void *id;                    /* the face record, for telling faces apart */
} BakSceneFace;
_Bool bak_scene_capturing(void);
void bak_scene_begin(const BakSceneCamera *camera);
/* The world's view is drawn from here to bak_scene_end (WORLDHIT.C, WORLDFRM.C): its pixels
   carry the frame's tag in display memory (bak_video.h), for the GPU's view in the window. */
void bak_scene_view(void);
/* The world's passes end here (WORLDHIT.C, WORLDFRM.C): their frame is finished before any 2-D
   overlay is drawn over it. The other passes end at the next one or at their page's flip. */
void bak_scene_end(void);
/* An object: its origin in camera space and its bounding half extent, both in world units, the
   unit shift of its faces' corners, its shape's draw priority (vislist_sort's), and the shape. */
void bak_scene_object(int32_t x, int32_t y, int32_t z, int32_t halfExtent, int unitShift,
                      uint8_t priority, const void *shape);
void bak_scene_face(const BakSceneFace *face);
/* The polygon the last face's sink received, in screen pixels (after clipping): the textured
   quads' sink, or the flat one with its colours. */
void bak_scene_polygon(unsigned n, const int16_t *xs, const int16_t *ys, _Bool textured,
                       uint8_t filled, uint8_t fill, uint8_t outline);
/* The last face's image and remap table as the textured quad's sink is about to draw them onto
   the polygon xs, ys (before it puts the corners in its order). */
void bak_scene_texture(const struct ImageRecord *image, int16_t remap, const int16_t *xs,
                       const int16_t *ys);
/* The last face filled from the ground's strip texture (VGA page AC00h) rather than its colour:
   the texture rows as vgablit's spans walk them (vgablit.c). */
void bak_scene_strip(uint16_t siBase, int16_t stride, uint16_t rowSub, uint16_t rowCount);
/* A billboard: its anchor in camera space (2^unitShift world units) and where it projects, and
   the sprite's rectangle on screen as drawn, its flip bits (1 vertical, 2 horizontal) and remap
   table. */
void bak_scene_billboard(const struct ImageRecord *image, const int16_t anchor[3], int unitShift,
                         int16_t anchorX, int16_t anchorY, int16_t x, int16_t y, int16_t w,
                         int16_t h, uint16_t flip, int16_t remap);
/* A circle: its centre in camera space (2^unitShift world units), where it projects, its
   radius on screen and its colour. */
void bak_scene_circle(const int16_t centre[3], int unitShift, int16_t x, int16_t y,
                      int16_t radius, uint8_t colour);
/* The sky and the ground behind the objects, in screen pixels: the horizon line (clipped to
   the view, or reject's outcode bits when it misses it) and the camera's heading; a band of
   rows in one colour across pixels 13-306 (vgafill.c); the band's rows shaded from its ramp,
   the last row's offset given (detail 0's sky); the panorama's images; a rectangle in one
   colour; a rectangle filled from the ground's strip texture. */
void bak_scene_horizon(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t reject,
                       uint16_t heading);
void bak_scene_sky_band(int16_t y, int16_t rows, uint8_t colour);
void bak_scene_sky_shaded(int16_t lastRowOffset, int16_t rows);
void bak_scene_sky_sprite(const struct ImageRecord *image, int16_t x, int16_t y);
void bak_scene_sky_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t colour);
void bak_scene_ground(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t siBase,
                      int16_t stride, uint16_t rowSub, uint16_t rowCount);
/* A table of images is about to be freed (free_image_record): its records are forgotten. */
void bak_scene_images_freed(struct ImageRecord *const *table);
/* The machine stands still between these: polls do nothing, so no time passes and no interrupt
 * runs (an autosave writes meanwhile). */
void bak_poll_hold(void);
void bak_poll_release(void);

/*
 * MSCDEX (INT 2Fh AH=15h, port/platform/cdrom.c): absent unless a CD drive is attached.
 * 150Bh drive check (AX result, BX = ADADh) and 1510h device request on a request
 * header; attaching gives a drive letter and its audio tracks' start positions.
 */
int bak_mscdex_installed(void);
uint16_t bak_mscdex_drive_check(uint16_t drive, uint16_t *bx);
void bak_mscdex_request(uint16_t drive, unsigned char *header);
void bak_cdrom_attach(int drive, int firstTrack, int lastTrack, const unsigned char (*starts)[3]);

/*
 * The MPU-401 MIDI interface (ports 330h/331h, port/platform/mpu401.c), UART mode: the
 * MIDI bytes the drivers send go to the sink, in order (none set: dropped, or written to
 * the file BAK_DUMP_MIDI names). Reset puts the interface back in its power-on state.
 */
typedef void BakMidiSink(uint8_t byte);
void bak_midi_set_sink(BakMidiSink *sink);

/* The AdLib's OPL2 (ports 388h/389h, port/platform/opl.c): each register written, in order,
 * also goes to a sink (the tests'). */
typedef void BakOplSink(uint8_t reg, uint8_t value);
void bak_opl_set_sink(BakOplSink *sink);
void bak_mpu401_reset(void);

/* I/O ports (VGA sequencer/CRTC/attribute registers, ...), emulated by the platform. */
uint8_t bak_port_in8(uint16_t port);
void bak_port_out8(uint16_t port, uint8_t value);
void bak_port_out16(uint16_t port, uint16_t value);
/* Every port access, in order (the tests'): a value read (isWrite 0) or written (1). */
typedef void BakPortTrace(uint16_t port, uint8_t value, int isWrite);
void bak_port_set_trace(BakPortTrace *trace);

/*
 * Interrupts (port/platform/machine.c). Host events and the virtual timer become the
 * PC's interrupts when the game polls: bak_poll runs what is due (timer ticks, key and
 * mouse events) and shows a frame at the display rate; bak_idle does the same from a
 * wait loop and sleeps when nothing was due. Both do nothing inside an interrupt.
 * Vectors 08h (IRQ 0), 09h (IRQ 1) and 1Ch (the BIOS user tick) are a table the game's
 * install routines replace and chain through.
 */
typedef void BakIsr(void);
void bak_poll(void);
void bak_idle(void);
#define BAK_IDLE() bak_idle()
BakIsr *bak_get_vector(uint8_t n);
void bak_set_vector(uint8_t n, BakIsr *isr);
void bak_run_isr(BakIsr *isr);
_Bool bak_in_isr(void);
/* A variable an interrupt updates, read where the game waits for it: polls first. */
#define BAK_POLLED(var) (*(bak_poll(), &(var)))

/* Called once per pass of the world loop: logs the game's state for replays when
 * BAK_STATE_LOG names a file (port/platform/statelog.c). */
void bak_state_log_pass(void);

/* The BIOS data area (0040:0000), also reachable as MK_FP(0x40, off). */
unsigned char *bak_bios_data(void);

/*
 * DIV and IDIV of the byte forms as they behave under the game's divide-error handler
 * (port/asm/int00.c): an overflowing quotient is retried with the dividend halved; a
 * division the handler cannot resolve stops with a message (the original hangs).
 */
uint8_t bak_div8(uint16_t ax, uint8_t divisor, uint8_t *remainder);
int8_t bak_idiv8(int16_t ax, int8_t divisor, int8_t *remainder);
/* IDIV r16: DX:AX by divisor, as the handler resolves it (DX:AX halved arithmetically). */
int16_t bak_idiv16(int32_t dxax, int16_t divisor, int16_t *remainder);

/* Abort with a message naming a routine the port does not implement yet. */
_Noreturn void bak_unimplemented(const char *what, const char *file, int line);
#define BAK_UNIMPLEMENTED(what) bak_unimplemented((what), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif
