/*
 * The rest of the PC the game programs directly: the 8253 timer, the 8259 interrupt
 * mask, the keyboard controller, the BIOS data area and the BIOS's own timer tick.
 *
 * Interrupts are delivered where the game looks for them. bak_poll, called from the
 * points where the game waits or reads its input and timer state, pumps host events
 * (a key becomes an IRQ 1, the mouse driver calls its handler) and runs IRQ 0 once for
 * every period of the programmed timer rate that has passed in real time. Vectors
 * 08h, 09h and 1Ch are a small table the game's install routines write, so the BIOS
 * handlers they replace and chain to exist too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "bak_video.h"
#include "platform.h"

#define PIT_HZ 1193182ull
#define MAX_CATCH_UP 64 /* timer ticks run at once; a longer stall drops the rest */
#define PUMP_NS 1000000ull
#define REFRESH_NS 14268000ull /* the VGA's 70 Hz, for showing frames while the game waits */

static uint16_t s_pitDivisor; /* 0 counts 65536 */
static int s_pitLowByteNext = 1;
static uint64_t s_tickNs;
static uint64_t s_nextTickNs;
static uint64_t s_lastPumpNs;
static uint64_t s_lastFrame;
static unsigned s_idlePolls;
static uint8_t s_picMask = 0xB8; /* IRQs 0, 1, 2 and 6 enabled, as DOS leaves it */
static uint8_t s_kbdData;
static uint8_t s_portB;
static bool s_inIsr;
static uint8_t s_bda[256];
static bool s_bdaReady;

static void bios_int8(void);
static BakIsr *s_vectors[0x20] = {[0x08] = bios_int8};

/* ---- BIOS data area ------------------------------------------------------------- */

static void bda_w16(unsigned off, uint16_t v) {
    s_bda[off] = (uint8_t)v;
    s_bda[off + 1] = (uint8_t)(v >> 8);
}

uint8_t *bak_bios_data(void) {
    if (!s_bdaReady) {
        s_bdaReady = true;
        bda_w16(0x1A, 0x1E); /* keyboard buffer head and tail: empty */
        bda_w16(0x1C, 0x1E);
        bda_w16(0x80, 0x1E); /* the buffer: 0040:001E..003D */
        bda_w16(0x82, 0x3E);
        s_bda[0x49] = 0x03;   /* video mode: 80x25 colour text */
        bda_w16(0x63, 0x3D4); /* CRTC base port: colour */
        s_bda[0x87] = 0x60;   /* EGA/VGA active, not monochrome */
        s_bda[0x96] = 0x10;   /* an enhanced (101-key) keyboard */
        bak_seg_register_at(0x0040, s_bda, sizeof s_bda);
    }
    return s_bda;
}

/* The BIOS IRQ 0 handler: counts the time of day and calls the user tick (INT 1Ch). */
static void bios_int8(void) {
    uint8_t *bda = bak_bios_data();
    uint32_t ticks = (uint32_t)bda[0x6C] | (uint32_t)bda[0x6D] << 8 | (uint32_t)bda[0x6E] << 16 |
                     (uint32_t)bda[0x6F] << 24;
    if (++ticks >= 0x1800B0u) { /* midnight */
        ticks = 0;
        bda[0x70] = 1;
    }
    bda_w16(0x6C, (uint16_t)ticks);
    bda_w16(0x6E, (uint16_t)(ticks >> 16));
    if (s_vectors[0x1C] != NULL)
        s_vectors[0x1C]();
}

/* ---- vectors ------------------------------------------------------------------- */

BakIsr *bak_get_vector(uint8_t n) {
    return n < 0x20 ? s_vectors[n] : NULL;
}

void bak_set_vector(uint8_t n, BakIsr *isr) {
    if (n >= 0x20)
        bak_unimplemented("an interrupt vector above 1Fh", __FILE__, __LINE__);
    s_vectors[n] = isr;
}

void bak_run_isr(BakIsr *isr) {
    bool nested = s_inIsr;
    s_inIsr = true;
    isr();
    s_inIsr = nested;
}

bool bak_in_isr(void) {
    return s_inIsr;
}

/* ---- ports --------------------------------------------------------------------- */

static void pit_reprogrammed(void) {
    uint32_t count = s_pitDivisor ? s_pitDivisor : 0x10000u;
    s_tickNs = count * 1000000000ull / PIT_HZ;
    s_nextTickNs = SDL_GetTicksNS() + s_tickNs;
}

bool machine_port_in8(uint16_t port, uint8_t *value) {
    switch (port) {
    case 0x21:
        *value = s_picMask;
        return true;
    case 0x60:
        *value = s_kbdData;
        return true;
    case 0x61:
        *value = s_portB;
        return true;
    default:
        return false;
    }
}

bool machine_port_out8(uint16_t port, uint8_t value) {
    switch (port) {
    case 0x20: /* end of interrupt: nothing is in service natively */
        return true;
    case 0x21:
        s_picMask = value;
        return true;
    case 0x40: /* channel 0 count, low byte then high byte (mode 36h) */
        if (s_pitLowByteNext) {
            s_pitDivisor = (uint16_t)((s_pitDivisor & 0xFF00) | value);
        } else {
            s_pitDivisor = (uint16_t)((s_pitDivisor & 0x00FF) | value << 8);
            pit_reprogrammed();
        }
        s_pitLowByteNext ^= 1;
        return true;
    case 0x43:
        s_pitLowByteNext = 1;
        return true;
    case 0x61: /* the keyboard acknowledge pulses bit 7 */
        s_portB = value;
        return true;
    default:
        return false;
    }
}

/* ---- delivering interrupts ------------------------------------------------------- */

void bak_keyboard_irq(uint8_t scancode) {
    s_kbdData = scancode;
    if ((s_picMask & 0x02) == 0 && s_vectors[0x09] != NULL)
        bak_run_isr(s_vectors[0x09]);
}

/* Runs the timer interrupts that are due, pumps host events now and then, and shows a
 * frame at the display's refresh rate. Returns whether anything happened. */
static bool poll(void) {
    uint64_t now = SDL_GetTicksNS();
    bool busy = false;
    if (now - s_lastPumpNs >= PUMP_NS) {
        s_lastPumpNs = now;
        if (!platform_pump_events()) {
            platform_shutdown();
            exit(0);
        }
        busy = true;
    }
    if (s_tickNs == 0)
        pit_reprogrammed();
    for (int n = 0; now >= s_nextTickNs; n++) {
        if (n == MAX_CATCH_UP) {
            s_nextTickNs = now + s_tickNs;
            break;
        }
        s_nextTickNs += s_tickNs;
        if ((s_picMask & 0x01) == 0 && s_vectors[0x08] != NULL)
            bak_run_isr(s_vectors[0x08]);
        busy = true;
    }
    uint64_t frame = now / REFRESH_NS;
    if (frame != s_lastFrame) {
        s_lastFrame = frame;
        video_present();
    }
    return busy;
}

void bak_poll(void) {
    if (s_inIsr || g_platformFreeRunning)
        return;
    /* A loop that only polls (a bare wait on a timer variable) yields the CPU. */
    if (poll())
        s_idlePolls = 0;
    else if (++s_idlePolls >= 64)
        SDL_DelayPrecise(100000);
}

void bak_idle(void) {
    if (s_inIsr || g_platformFreeRunning)
        return;
    if (!poll())
        SDL_DelayPrecise(250000);
}
