/*
 * KEYBOARD.ASM in C: the keyboard interrupt handler and the key queries.
 *
 * The handler keeps a down/up bit per scancode in g_key_state and, for make codes,
 * turns the scancode into an ASCII code (with the shift, ctrl and caps-lock state the
 * BIOS keeps at 0040:0017) and queues scancode:ASCII in the BIOS keyboard buffer, where
 * kbhit_read takes it. Keys whose state byte carries a channel (bits 3-7) also record
 * the last such key per channel in g_input_mode_lut. Ctrl-Esc and Ctrl-Alt-Del end the
 * game. On a Tandy (bBiosCompatible) the scancodes of its keyboard are remapped first.
 *
 * Natively the handler is IRQ 1 of the emulated machine: it reads the byte the host
 * keyboard delivered from port 60h. keyboard_install puts it in the vector table and,
 * when asked, input_axis_accumulate_isr on the BIOS user tick (1Ch).
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/INPUT/KEYBOARD.H"
#include "SRC/SYS/BIOSCHK.H"
#include "SRC/SYS/BOOT.H"
#include "bak_game_end.h"

#define BDA_SHIFT 0x17 /* shift/ctrl/alt/lock state */
#define BDA_HEAD 0x1A  /* keyboard buffer: next to read */
#define BDA_TAIL 0x1C  /* keyboard buffer: next to write */
#define BDA_START 0x80
#define BDA_END 0x82
#define ASCII_AT 0x48   /* in g_bUnused2cfa: scancode -> ASCII */
#define SHIFTED_AT 0xA1 /* in g_bUnused2cfa: scancode -> shifted ASCII */

static BakIsr *s_savedInt9;
static BakIsr *s_savedInt1c;
/* The immediates of the handler's two lock-key masks, which keyboard_install patches. */
static unsigned char s_lockMask1 = 0xff;
static unsigned char s_lockMask2 = 0xff;

static u16 bda_w16(unsigned off) {
    unsigned char *bda = bak_bios_data();
    return (u16)(bda[off] | bda[off + 1] << 8);
}

static void bda_set16(unsigned off, u16 v) {
    unsigned char *bda = bak_bios_data();
    bda[off] = (unsigned char)v;
    bda[off + 1] = (unsigned char)(v >> 8);
}

s16 keyboard_install(s16 install_timer_int) {
    unsigned char *bda = bak_bios_data();
    if (g_keyboard_installed == 0) {
        s_savedInt9 = bak_get_vector(0x09);
        s_savedInt1c = bak_get_vector(0x1C);
        bak_set_vector(0x09, isr_keyboard);
        if (install_timer_int != 0)
            bak_set_vector(0x1C, input_axis_accumulate_isr);
        g_bEnhancedKeyboard = 0;
        if (bios_compat_check() != 0) {
            /* The Tandy keyboard probe (INT 15h): the call fails on every native host, and
             * the BIOS data area then says whether an enhanced keyboard is present. */
            if (bda[0x96] == 0x10) {
                g_bEnhancedKeyboard = 1;
            } else {
                s_lockMask1 = 0xfe;
                s_lockMask2 = 0xfe;
                g_key_state[0x29] = g_key_state[0x48];
                g_key_state[0x2b] = g_key_state[0x4b];
                g_key_state[0x4e] = g_key_state[0x4d];
                g_key_state[0x4a] = g_key_state[0x50];
            }
        }
        g_keyboard_installed = 1;
    }
    bda[BDA_SHIFT] &= 0xdf;
    if (g_bCapsLockEnabled != 0)
        bda[BDA_SHIFT] |= 0x40;
    return g_keyboard_installed;
}

unsigned char keyboard_restore_handler(void) {
    if (g_keyboard_installed == 0)
        return 0;
    g_keyboard_installed = 0;
    bda_set16(BDA_HEAD, bda_w16(BDA_TAIL));
    bak_set_vector(0x09, s_savedInt9);
    bak_set_vector(0x1C, s_savedInt1c);
    return 1;
}

/* A key's channel (state bits 4-7) and which of the two channels it belongs to (bit 3). */
static void channel_of(unsigned char state, unsigned *index, unsigned char *channel) {
    *index = (state >> 3) & 1;
    *channel = (unsigned char)(state >> 4);
}

void isr_keyboard(void) {
    unsigned char *bda = bak_bios_data();
    unsigned char raw = bak_port_in8(0x60);
    unsigned char ack = bak_port_in8(0x61);
    bak_port_out8(0x61, ack | 0x80);
    bak_port_out8(0x61, ack);

    unsigned char al = raw & 0x7f;
    unsigned char brk = raw & 0x80;
    if (g_graphics_context.bBiosCompatible == 1) {
        if (g_bEnhancedKeyboard == 1) {
            if (al == 0x29)
                al = 0x48;
            if (al == 0x2b)
                al = 0x4b;
        } else {
            for (unsigned k = 0; k < 11; k++) {
                if (g_ext_scancode_from[k] == al) {
                    al = g_ext_scancode_to[k];
                    break;
                }
            }
        }
    }
    al |= brk;
    unsigned char key = al & 0x7f;
    if ((signed char)key >= 0x59)
        goto eoi;

    /* Bit 0 of the key's state: set on a make, kept on a break only if it was clear. */
    unsigned char old = g_key_state[key];
    g_key_state[key] = (unsigned char)(((brk ? 0xffu : 0xfeu) & old) ^ 1);
    unsigned char dl = old;
    if (g_bEnhancedKeyboard == 1) {
        if (key == 0x3a) {
            al = key;
            dl &= s_lockMask1;
        } else if (key == 0x45) {
            al = key;
            dl &= s_lockMask2;
        }
    }

    if (al & 0x80) {
        /* Release: the key's channel forgets it, a shift key's BIOS bit clears. */
        if (dl & 0xf8) {
            unsigned index;
            unsigned char channel;
            channel_of(dl, &index, &channel);
            if (g_input_mode_lut[index] == channel)
                g_input_mode_lut[index] = 0;
        }
        g_wIsrLastKeyEventDead = 0;
        unsigned char code = g_bUnused2cfa[ASCII_AT + (al & 0x7f)];
        if ((code & 0x80) && !(code & 0x70))
            bda[BDA_SHIFT] &= code ^ 0x7f;
        goto eoi;
    }

    /* Press. */
    if (dl & 0xf8) {
        unsigned index;
        unsigned char channel;
        channel_of(dl, &index, &channel);
        g_input_mode_lut[index] = channel;
    }
    unsigned char scancode = al;
    unsigned char ascii = g_bUnused2cfa[ASCII_AT + al];
    if (ascii & 0x80) {
        ascii &= 0x7f;
        if (ascii & 0x70) {
            /* A lock key toggles on its first make, not on repeats; caps lock can be off. */
            if ((ascii & 0x40) && g_bCapsLockEnabled != 0)
                goto eoi;
            if (dl & 1)
                goto eoi;
            bda[BDA_SHIFT] ^= ascii;
        } else {
            bda[BDA_SHIFT] |= ascii;
        }
        goto eoi;
    }
    if (bda[BDA_SHIFT] & 0x04) {
        ascii |= 0x80;
        if (dl & 0x04)
            ascii -= 0x20;
    } else if ((bda[BDA_SHIFT] & 0x40) && (dl & 0x04)) {
        ascii -= 0x20;
    } else if (bda[BDA_SHIFT] & 0x03) {
        ascii = g_bUnused2cfa[SHIFTED_AT + scancode];
    }
    u16 ax = (u16)(scancode << 8 | ascii);
    g_wIsrLastKeyEventDead = ax;

    /* Queue scancode:ASCII. */
    u16 head = bda_w16(BDA_HEAD);
    u16 di = bda_w16(BDA_TAIL);
    int full;
    if (head == 0x3c)
        full = di == 0x1e;
    else
        full = (u16)(head + 2) == di;
    if (!full) {
        bda_set16(di, ax);
        if (di == 0x3c)
            di = 0x1c;
        di = (u16)(di + 2);
        bda_set16(BDA_TAIL, di);
    }

    if (scancode == 0x20 && (bda[BDA_SHIFT] & 0x04)) {
        bak_port_out8(0x20, 0x20); /* Ctrl-D */
        return;
    }
    s16 code;
    if (ax == 0x019b) {
        code = 0; /* Ctrl-Esc */
    } else if (ax == 0x5380 && (bda[BDA_SHIFT] & 0x08)) {
        code = 1; /* Ctrl-Alt-Del */
    } else {
        goto eoi;
    }
    if (!(bda[BDA_SHIFT] & 0x04))
        goto eoi;
    di = (u16)(di - 2);
    if (di == 0x1c)
        di = 0x3c;
    bda_set16(di, 0);
    bda_set16(BDA_HEAD, bda_w16(BDA_TAIL));
    bak_port_out8(0x20, 0x20);
    boot_engine_exit(code);
    return;
eoi:
    bak_port_out8(0x20, 0x20);
}

/* An axis value moved by the per-tick magnitude in the given direction, kept within
 * -7Fh..7Fh, the sign handled as sign and magnitude as in the original. */
static s16 step_axis(s16 axis, int negative, u16 magnitude) {
    u16 sign = axis < 0 ? 0xffff : 0;
    u16 mag = (u16)((axis ^ sign) - sign);
    u16 dir = (u16)((negative ? 0xffff : 0) ^ sign);
    u16 v = (u16)((u16)((magnitude ^ dir) - dir) + mag);
    if ((s16)v > 0x7f)
        v = 0x7f;
    return (s16)((v ^ sign) - sign);
}

/*
 * The per-tick axis update for the two channels. A channel's mode m (its lut entry, 1
 * to 80h, minus one) selects a flag byte read as a bit stream, most significant bit
 * first: for each axis an enable bit, followed, when set, by the direction bit.
 */
void input_axis_accumulate_isr(void) {
    u16 magnitude = (u16)(g_key_state[0x65] | g_key_state[0x66] << 8);
    for (int si = 2; si >= 0; si -= 2) {
        s16 *axis1 = (s16 *)&g_key_state[0x59 + si];
        s16 *axis2 = (s16 *)&g_key_state[0x5d + si];
        u16 gate = (u16)(g_key_state[0x61 + si] | g_key_state[0x62 + si] << 8);
        unsigned char m = (unsigned char)(g_input_mode_lut[si >> 1] - 1);
        if ((signed char)m < 0) {
            if (gate == 0)
                continue;
            *axis1 = 0;
            *axis2 = 0;
            continue;
        }
        if (m == 8) {
            *axis1 = 0;
            *axis2 = 0;
            continue;
        }
        if (0x67 + m >= (int)sizeof g_key_state)
            BAK_UNIMPLEMENTED("input_axis_accumulate_isr: a mode past the key-state table");
        unsigned char bits = g_key_state[0x67 + m];
        int enabled = bits >> 7;
        bits = (unsigned char)(bits << 1);
        if (enabled) {
            *axis1 = step_axis(*axis1, bits >> 7, magnitude);
            bits = (unsigned char)(bits << 1);
        }
        enabled = bits >> 7;
        bits = (unsigned char)(bits << 1);
        if (enabled)
            *axis2 = step_axis(*axis2, bits >> 7, magnitude);
    }
}

/* Take one scancode:ASCII from the BIOS keyboard buffer, 0 if it is empty. */
u16 kbhit_read(void) {
    bak_poll();
    u16 head = bda_w16(BDA_HEAD);
    if (head == bda_w16(BDA_TAIL))
        return 0;
    u16 ax = bda_w16(head);
    head = (u16)(head + 2);
    if (head == bda_w16(BDA_END))
        head = bda_w16(BDA_START);
    bda_set16(BDA_HEAD, head);
    return ax;
}

s16 key_state_get_via_2cf8_table(s16 mode) {
    unsigned char slot = g_input_mode_lut[mode != 0];
    return g_key_state[0x6f + slot];
}

s16 key_is_down(s16 scancode) {
    bak_poll();
    return g_key_state[(u16)scancode] & 1;
}
