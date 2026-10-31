/*
 * bak_port.h - services the native platform layer provides to the game code.
 *
 * Shared by game translation units (through bak_native.h) and port code. It
 * declares no structures, so it is safe to include with or without packing.
 */
#ifndef BAK_PORT_H
#define BAK_PORT_H

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
uint16_t bak_seg_register(void *base, uint32_t size);
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
 * LIM EMS 4.0 (INT 67h), emulated exactly: a 64 KB page frame registered at a
 * fixed segment, 16 KB logical pages held on the host, and mapping copies
 * pages in and out of the frame as the hardware's bank switching would appear.
 */
uint16_t bak_ems_frame_segment(void);                      /* AH=41h, BX */
uint16_t bak_ems_unallocated_pages(void);                  /* AH=42h, BX */
uint16_t bak_ems_allocate(uint16_t pages);                 /* AH=43h, DX = handle */
void bak_ems_release(uint16_t handle);                     /* AH=45h */
void bak_ems_map_multiple(uint16_t handle, const int16_t *pairs, int count); /* AX=5000h */

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

/* I/O ports (VGA sequencer/CRTC/attribute registers, ...), emulated by the platform. */
uint8_t bak_port_in8(uint16_t port);
void bak_port_out8(uint16_t port, uint8_t value);
void bak_port_out16(uint16_t port, uint16_t value);

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

/* The BIOS data area (0040:0000), also reachable as MK_FP(0x40, off). */
unsigned char *bak_bios_data(void);

/*
 * DIV and IDIV of the byte forms as they behave under the game's divide-error handler
 * (port/asm/int00.c): an overflowing quotient is retried with the dividend halved; a
 * division the handler cannot resolve stops with a message (the original hangs).
 */
uint8_t bak_div8(uint16_t ax, uint8_t divisor, uint8_t *remainder);
int8_t bak_idiv8(int16_t ax, int8_t divisor, int8_t *remainder);

/* Abort with a message naming a routine the port does not implement yet. */
_Noreturn void bak_unimplemented(const char *what, const char *file, int line);
#define BAK_UNIMPLEMENTED(what) bak_unimplemented((what), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif
