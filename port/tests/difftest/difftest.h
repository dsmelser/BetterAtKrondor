/*
 * difftest.h - the native side of the differential tests.
 *
 * bak_difftest runs C translations of original routines on request from the Python
 * harness (port/tests/harness/native.py), which runs the original routines in Unicorn
 * and compares. Requests and replies are length-prefixed frames on stdin/stdout:
 *
 *   request:  u32 size, then size bytes: command name, NUL, payload
 *   reply:    u32 status (0 ok, 1 error), u32 size, then size bytes
 *
 * All integers are little-endian. Each test module exports a table of commands and of
 * named globals; the generic "peek"/"poke" commands read and write those globals, so
 * the harness can copy state between the original and the translation.
 */
#ifndef BAK_DIFFTEST_H
#define BAK_DIFFTEST_H

#include <stddef.h>
#include <stdint.h>

typedef struct DtIn {
    const uint8_t *data;
    size_t size;
    size_t pos;
} DtIn;

typedef struct DtOut {
    uint8_t *data;
    size_t size;
    size_t capacity;
} DtOut;

/* Payload readers; reading past the end fails the request. */
uint8_t dt_u8(DtIn *in);
uint16_t dt_u16(DtIn *in);
uint32_t dt_u32(DtIn *in);
const uint8_t *dt_bytes(DtIn *in, size_t n);
size_t dt_remaining(const DtIn *in);

void dt_put_u8(DtOut *out, uint8_t v);
void dt_put_u16(DtOut *out, uint16_t v);
void dt_put_u32(DtOut *out, uint32_t v);
void dt_put_bytes(DtOut *out, const void *p, size_t n);

/* Fail the current request with a message (the harness raises it as an error). */
_Noreturn void dt_fail(const char *fmt, ...);

typedef void DtHandler(DtIn *in, DtOut *out);

/*
 * One entry of a module table: a command (handler set) or a named global (address
 * and size set). Tables end with an entry whose name is NULL.
 */
typedef struct DtEntry {
    const char *name;
    DtHandler *handler;
    void *address;
    size_t size;
} DtEntry;

#define DT_COMMAND(name, fn) {(name), (fn), NULL, 0}
#define DT_GLOBAL(sym) {#sym, NULL, (void *)&(sym), sizeof(sym)}
#define DT_END {NULL, NULL, NULL, 0}

/* The module tables (one per t_*.c file). */
extern const DtEntry dt_rand_entries[];
extern const DtEntry dt_vga_entries[];
extern const DtEntry dt_viddet_entries[];
extern const DtEntry dt_pixel_entries[];
extern const DtEntry dt_farptr_entries[];
extern const DtEntry dt_mouse_entries[];
extern const DtEntry dt_lzw_entries[];
extern const DtEntry dt_timer_entries[];
extern const DtEntry dt_keyboard_entries[];
extern const DtEntry dt_int00_entries[];
extern const DtEntry dt_cdaudio_entries[];
extern const DtEntry dt_m32_entries[];
extern const DtEntry dt_midi_entries[];
extern const DtEntry dt_snddrv_entries[];
extern const DtEntry dt_polyrast_entries[];
extern const DtEntry dt_drawline_entries[];
extern const DtEntry dt_r3dtrig_entries[];
extern const DtEntry dt_vgablit_entries[];
extern const DtEntry dt_heg_entries[];
extern const DtEntry dt_gstate_entries[];
extern const DtEntry dt_rtl_entries[];
extern const DtEntry dt_dosrw_entries[];
extern const DtEntry dt_r3d_entries[];

#endif
