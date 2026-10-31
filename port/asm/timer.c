/*
 * TIMER.ASM in C: the IRQ 0 driver and its callback registry.
 *
 * timer_install speeds the 8253 up to 0FFFFh / frequency counts per tick (236.7 Hz for
 * the game's 13) and takes over IRQ 0. Each tick counts the frame countdown down to
 * zero, counts down every registered callback's interval, calling it and reloading
 * when it runs out, and every `frequency` ticks passes the tick on to the BIOS handler,
 * whose clock thus still runs at 18.2 Hz. Natively the ticks come from the virtual
 * timer (port/platform/machine.c) wherever the game polls.
 *
 * A registered callback is stored in its TimerCallback (offset and segment words in the
 * original) as the bits of a native function pointer; the port is 32-bit.
 */
#include <string.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/INPUT/TIMER.H"
#include "SRC/SYS/BIOSCHK.H"
#include "bak_game_end.h"

_Static_assert(sizeof(FARFN) == sizeof(TimerCallback), "a callback fits its registry slot");

static BakIsr *s_savedTimerVec;

s16 registry_slot_alloc(FARFN callback, s16 interval) {
    u16 mask, bit;
    unsigned slot;
    if (g_timer_installed == 0)
        return 0;
    mask = g_registry_slot_mask;
    if (mask == 0xffff)
        return 0;
    for (slot = 0, bit = 1; mask & 1; slot++) {
        mask >>= 1;
        bit <<= 1;
    }
    g_aTimerSched[slot].wReload = (u16)interval;
    g_aTimerSched[slot].wCountdown = (u16)interval;
    memcpy(&g_aTimerCallback[slot], &callback, sizeof callback);
    g_registry_slot_mask |= bit;
    return (s16)(slot + 1);
}

/* Only the low byte of slot - 1 is range-checked, as in the original. */
s16 registry_slot_free(s16 slot) {
    unsigned char index = (unsigned char)(slot - 1);
    if (index & 0xf0)
        return 0;
    g_registry_slot_mask &= (u16) ~(1u << index);
    return 1;
}

void timer_install(u16 frequency) {
    if (g_timer_installed != 0)
        return;
    g_registry_slot_mask = 0;
    bios_compat_check();
    s_savedTimerVec = bak_get_vector(0x08);
    if ((s16)frequency > 0xff || frequency == 0)
        return;
    g_nTimerBiosChainReload = frequency;
    g_nTimerBiosChainCountdown = frequency;
    g_nTimerPitDivisor = (u16)(0xffffu / frequency);
    bak_port_out8(0x43, 0x36);
    bak_port_out8(0x40, (unsigned char)g_nTimerPitDivisor);
    bak_port_out8(0x40, (unsigned char)(g_nTimerPitDivisor >> 8));
    bak_port_out8(0x21, bak_port_in8(0x21) & 0xfc);
    bak_set_vector(0x08, timer_irq0_dispatch);
    g_timer_installed = 1;
}

s16 timer_restore_default(void) {
    if (g_timer_installed == 0)
        return 0;
    bak_port_out8(0x43, 0x36);
    bak_port_out8(0x40, 0);
    bak_port_out8(0x40, 0);
    bak_port_out8(0x21, bak_port_in8(0x21) & 0xfc);
    bak_set_vector(0x08, s_savedTimerVec);
    g_timer_installed = 0;
    return 1;
}

void timer_irq0_dispatch(void) {
    u16 mask = g_registry_slot_mask;
    u16 countdown = (u16)(g_nFrameTickCountdownValue - 1);
    if ((s16)countdown < 0)
        countdown = (u16)~countdown; /* stops at zero */
    g_nFrameTickCountdownValue = (s16)countdown;

    for (unsigned slot = 0;; slot++) {
        unsigned bit = mask & 1;
        mask >>= 1;
        if (!bit) {
            if (mask == 0)
                break;
            continue;
        }
        u16 left = (u16)(g_aTimerSched[slot].wCountdown - 1);
        if (left == 0) {
            FARFN callback;
            memcpy(&callback, &g_aTimerCallback[slot], sizeof callback);
            callback();
            left = g_aTimerSched[slot].wReload;
        }
        g_aTimerSched[slot].wCountdown = left;
    }

    if (--g_nTimerBiosChainCountdown == 0) {
        g_nTimerBiosChainCountdown = g_nTimerBiosChainReload;
        if (s_savedTimerVec != NULL)
            s_savedTimerVec();
    }
}
