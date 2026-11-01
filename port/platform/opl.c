/*
 * The AdLib's OPL2 (YM3812) at ports 388h (a register's number; read: the status) and 389h (its
 * value), for the AdLib driver. A register written goes to the tests' sink, if any, and becomes
 * an event of the synthesizer's audio source, stamped as audio events are (audio.h); the source
 * renders the chip through ymfm (ymfm_opl2.cpp) at its own rate, the AdLib's crystal over 72.
 * The chip and its source are made at the first write. The status port reads 06h, an idle
 * OPL2's: the driver never starts the chip's timers.
 */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "audio.h"
#include "bak_port.h"
#include "platform.h"
#include "ymfm_opl2.h"

#define OPL_CLOCK 3579545u /* Hz: the AdLib's crystal */
#define OPL_ADDRESS 0x388
#define OPL_DATA 0x389

static uint8_t s_address;
static YmfmOpl2 *s_chip;
static AudioSource *s_source;
static BakOplSink *s_sink;
static bool s_failed;

void bak_opl_set_sink(BakOplSink *sink) {
    s_sink = sink;
}

static void opl_render(void *user, void *out, int frames) {
    (void)user;
    ymfm_opl2_generate(s_chip, out, frames);
}

static void opl_apply(void *user, const AudioEvent *e) {
    (void)user;
    ymfm_opl2_write(s_chip, (uint8_t)e->a, (uint8_t)e->b);
}

static bool opl_start(void) {
    if (s_source != NULL || s_failed)
        return s_source != NULL;
    AudioSourceDesc desc = {.name = "OPL2",
                            .format = SDL_AUDIO_S16,
                            .channels = 1,
                            .rate = (int)ymfm_opl2_rate(OPL_CLOCK),
                            .volume = AUDIO_VOLUME_EFFECTS,
                            .gain = 1.0f,
                            .render = opl_render,
                            .apply = opl_apply};
    s_chip = ymfm_opl2_create();
    if (s_chip == NULL || (s_source = audio_source_add(&desc)) == NULL) {
        fprintf(stderr, "no OPL2: the AdLib stays silent\n");
        ymfm_opl2_destroy(s_chip);
        s_chip = NULL;
        s_failed = true;
        return false;
    }
    return true;
}

void opl_close(void) {
    ymfm_opl2_destroy(s_chip);
    s_chip = NULL;
    s_source = NULL;
    s_failed = false;
    s_address = 0;
}

bool opl_port_out8(uint16_t port, uint8_t value) {
    if (port == OPL_ADDRESS) {
        s_address = value;
        return true;
    }
    if (port != OPL_DATA)
        return false;
    if (s_sink != NULL)
        s_sink(s_address, value);
    if (opl_start())
        audio_post(s_source, s_address, value, NULL);
    return true;
}

bool opl_port_in8(uint16_t port, uint8_t *value) {
    if (port == OPL_ADDRESS) {
        *value = 0x06;
        return true;
    }
    if (port == OPL_DATA) {
        *value = 0xFF; /* the data port is write-only */
        return true;
    }
    return false;
}
