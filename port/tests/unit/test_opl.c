/* The AdLib's OPL2: the ports reach the tests' sink and the chip, whose notes sound at the
 * moments they were written. */
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/audio.h"
#include "../../platform/platform.h"
#include "bak_port.h"

#define T0 1000000000ull
#define MS 1000000ull

static uint8_t s_seen[64][2];
static int s_seenCount;

static void sink(uint8_t reg, uint8_t value) {
    if (s_seenCount < 64) {
        s_seen[s_seenCount][0] = reg;
        s_seen[s_seenCount][1] = value;
    }
    s_seenCount++;
}

static void opl_write(uint8_t reg, uint8_t value) {
    bak_port_out8(0x388, reg);
    bak_port_out8(0x389, value);
}

/* Voice 0 (operators 0 and 3) as a sustained sine: the modulator silent, the carrier at full
 * level, the fastest attack and release, and 440 Hz (F-number 244h in block 4) not yet keyed
 * on. */
static void sine_patch(void) {
    static const uint8_t regs[][2] = {
        {0x20, 0x21}, {0x23, 0x21}, {0x40, 0x3f}, {0x43, 0x00}, {0x60, 0xf0}, {0x63, 0xf0},
        {0x80, 0x0f}, {0x83, 0x0f}, {0xc0, 0x00}, {0xa0, 0x44}, {0xb0, 0x12},
    };
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++)
        opl_write(regs[i][0], regs[i][1]);
}

void unit_opl_ports_reach_the_sink(void) {
    uint8_t status = 0;
    s_seenCount = 0;
    bak_opl_set_sink(sink);
    opl_write(0x20, 0x01);
    bak_port_out8(0x388, 0xa0); /* an address alone writes nothing */
    opl_write(0xb0, 0x32);
    bak_opl_set_sink(NULL);
    opl_write(0xb0, 0x12);
    UNIT_CHECK(s_seenCount == 2);
    UNIT_CHECK(s_seen[0][0] == 0x20 && s_seen[0][1] == 0x01);
    UNIT_CHECK(s_seen[1][0] == 0xb0 && s_seen[1][1] == 0x32);
    UNIT_CHECK(opl_port_in8(0x388, &status) && status == 0x06); /* idle, no timer started */
    UNIT_CHECK(!opl_port_in8(0x38a, &status));
    bak_opl_set_sink(sink);
    UNIT_CHECK(!opl_port_out8(0x38a, 0x55)); /* not the AdLib's: left to the others */
    bak_opl_set_sink(NULL);
    UNIT_CHECK(s_seenCount == 2);
    audio_close(); /* the source the writes made */
    opl_close();
}

void unit_opl_notes_sound_at_their_moment(void) {
    static float mix[19200 * 2];
    clock_use_virtual(T0);
    audio_open_dump(NULL, T0);
    sine_patch(); /* at T0: the chip and its source start here */
    clock_spend(10 * MS);
    opl_write(0xb0, 0x32); /* key on at 10 ms: heard from 60 ms, frame 2880 */
    clock_spend(190 * MS);
    opl_write(0xb0, 0x12); /* key off at 200 ms: released from 250 ms, frame 12000 */
    audio_mix(mix, 19200); /* 400 ms */
    int first = -1, last = -1;
    double sum = 0;
    for (int i = 0; i < 19200; i++) {
        if (mix[2 * i] != 0.0f) {
            if (first < 0)
                first = i;
            last = i;
        }
        if (i >= 4000 && i < 11000)
            sum += (double)mix[2 * i] * mix[2 * i];
    }
    UNIT_CHECK(first >= 2880 - 30 && first <= 2880 + 60);
    UNIT_CHECK(SDL_sqrt(sum / 7000) > 0.05);               /* a tone */
    UNIT_CHECK(last >= 12000 - 30 && last < 12000 + 2400); /* released within 50 ms */
    UNIT_CHECK(mix[2 * 5000] == mix[2 * 5000 + 1]);        /* the chip is mono: both sides */
    audio_close();
    opl_close();
}
