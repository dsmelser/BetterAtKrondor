/*
 * bak_difftest: serves the differential-test harness (see difftest.h for the protocol).
 *
 * Commands "peek <name>" and "poke <name> <bytes>" read and write the globals the module
 * tables name; every other command is a module handler. The process stays up for a
 * whole test session, so translations keep their state between requests exactly as
 * they would in the game.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "difftest.h"
#include "../../platform/platform.h"

static const DtEntry *const s_modules[] = {
    dt_rand_entries, dt_vga_entries, dt_viddet_entries, dt_pixel_entries,
    dt_farptr_entries, dt_mouse_entries, dt_lzw_entries,
    dt_timer_entries, dt_keyboard_entries, dt_int00_entries,
    dt_cdaudio_entries, dt_m32_entries, dt_midi_entries, dt_snddrv_entries,
    dt_polyrast_entries, dt_drawline_entries, dt_r3dtrig_entries, dt_vgablit_entries,
    dt_heg_entries, dt_gstate_entries, dt_rtl_entries, dt_dosrw_entries, dt_r3d_entries, dt_decomp_entries, dt_distdir_entries, dt_vgafill_entries, dt_r3dclip_entries, dt_r3dmesh_entries, dt_polygon_entries, dt_palblend_entries, dt_cspell_entries, NULL,
};

static jmp_buf s_failJump;
static char s_failMessage[512];

_Noreturn void dt_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_failMessage, sizeof s_failMessage, fmt, ap);
    va_end(ap);
    longjmp(s_failJump, 1);
}

size_t dt_remaining(const DtIn *in) {
    return in->size - in->pos;
}

const uint8_t *dt_bytes(DtIn *in, size_t n) {
    if (dt_remaining(in) < n)
        dt_fail("request payload too short (%zu more bytes wanted, %zu left)", n, dt_remaining(in));
    const uint8_t *p = in->data + in->pos;
    in->pos += n;
    return p;
}

uint8_t dt_u8(DtIn *in) {
    return *dt_bytes(in, 1);
}

uint16_t dt_u16(DtIn *in) {
    const uint8_t *p = dt_bytes(in, 2);
    return (uint16_t)(p[0] | p[1] << 8);
}

uint32_t dt_u32(DtIn *in) {
    const uint8_t *p = dt_bytes(in, 4);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void dt_put_bytes(DtOut *out, const void *p, size_t n) {
    if (out->size + n > out->capacity) {
        size_t cap = out->capacity ? out->capacity : 256;
        while (cap < out->size + n)
            cap *= 2;
        uint8_t *grown = realloc(out->data, cap);
        if (grown == NULL) {
            fprintf(stderr, "bak_difftest: out of memory\n");
            exit(2);
        }
        out->data = grown;
        out->capacity = cap;
    }
    memcpy(out->data + out->size, p, n);
    out->size += n;
}

void dt_put_u8(DtOut *out, uint8_t v) {
    dt_put_bytes(out, &v, 1);
}

void dt_put_u16(DtOut *out, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    dt_put_bytes(out, b, 2);
}

void dt_put_u32(DtOut *out, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    dt_put_bytes(out, b, 4);
}

static const DtEntry *find_entry(const char *name) {
    for (int m = 0; s_modules[m] != NULL; m++)
        for (const DtEntry *e = s_modules[m]; e->name != NULL; e++)
            if (strcmp(e->name, name) == 0)
                return e;
    return NULL;
}

static const DtEntry *find_global(DtIn *in) {
    const char *name = (const char *)in->data + in->pos;
    size_t len = strnlen(name, dt_remaining(in));
    if (len == dt_remaining(in))
        dt_fail("global name is not NUL-terminated");
    in->pos += len + 1;
    const DtEntry *e = find_entry(name);
    if (e == NULL || e->address == NULL)
        dt_fail("no global named '%s' in the test tables", name);
    return e;
}

static void cmd_peek(DtIn *in, DtOut *out) {
    const DtEntry *e = find_global(in);
    dt_put_bytes(out, e->address, e->size);
}

static void cmd_poke(DtIn *in, DtOut *out) {
    (void)out;
    const DtEntry *e = find_global(in);
    if (dt_remaining(in) != e->size)
        dt_fail("poke %s: %zu bytes given, the global has %zu", e->name, dt_remaining(in), e->size);
    memcpy(e->address, dt_bytes(in, e->size), e->size);
}

static int read_exact(void *p, size_t n) {
    return fread(p, 1, n, stdin) == n;
}

static void write_reply(uint32_t status, const void *p, size_t n) {
    uint32_t head[2] = {status, (uint32_t)n};
    fwrite(head, sizeof head, 1, stdout);
    if (n)
        fwrite(p, 1, n, stdout);
    fflush(stdout);
}

int main(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    crash_reporter_install();
    g_platformFreeRunning = true;

    uint8_t *request = NULL;
    size_t requestCap = 0;
    DtOut out = {0};
    for (;;) {
        uint32_t size;
        if (!read_exact(&size, sizeof size))
            break; /* harness closed the pipe */
        if (size > requestCap) {
            free(request);
            request = malloc(size);
            requestCap = size;
            if (request == NULL) {
                fprintf(stderr, "bak_difftest: request of %u bytes\n", size);
                return 2;
            }
        }
        if (!read_exact(request, size))
            break;

        out.size = 0;
        DtIn in = {request, size, 0};
        if (setjmp(s_failJump) == 0) {
            const char *name = (const char *)request;
            size_t len = strnlen(name, size);
            if (len == size)
                dt_fail("command name is not NUL-terminated");
            in.pos = len + 1;
            if (strcmp(name, "peek") == 0) {
                cmd_peek(&in, &out);
            } else if (strcmp(name, "poke") == 0) {
                cmd_poke(&in, &out);
            } else {
                const DtEntry *e = find_entry(name);
                if (e == NULL || e->handler == NULL)
                    dt_fail("unknown command '%s'", name);
                e->handler(&in, &out);
            }
            write_reply(0, out.data, out.size);
        } else {
            write_reply(1, s_failMessage, strlen(s_failMessage));
        }
    }
    free(request);
    free(out.data);
    return 0;
}
