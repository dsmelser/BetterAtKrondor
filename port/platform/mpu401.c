/*
 * The MPU-401 MIDI interface at 330h (data) and 331h (status and commands), in UART mode
 * as the MT-32 and General MIDI drivers use it.
 *
 * The interface is always ready for output (status bit 6 clear). Every command is
 * acknowledged with FEh on the data port (status bit 7 clear until it is read): 3Fh
 * enters UART mode, FFh (reset) leaves it. Real cards and clones differ on whether a
 * command in UART mode is acknowledged; the drivers only wait for the reply with a
 * bounded poll, so the choice changes nothing they send. In UART mode each byte written
 * to the data port goes to the MIDI sink: the synthesizer. Without one it is dropped.
 *
 * BAK_DUMP_MIDI=<file>: without a sink, the MIDI bytes are written to that file, a raw
 * stream to check or play back elsewhere.
 */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "platform.h"

#define MPU_DATA 0x330
#define MPU_STATUS 0x331
#define MPU_ACK 0xfe

static bool s_uart;
static bool s_ackPending;
static BakMidiSink *s_sink;

void bak_midi_set_sink(BakMidiSink *sink) {
    s_sink = sink;
}

static void dump_midi(uint8_t byte) {
    static FILE *f;
    static bool checked;
    if (!checked) {
        checked = true;
        const char *path = SDL_getenv("BAK_DUMP_MIDI");
        if (path != NULL && (f = fopen(path, "wb")) != NULL)
            setvbuf(f, NULL, _IONBF, 0);
    }
    if (f != NULL)
        fputc(byte, f);
}

void bak_mpu401_reset(void) {
    s_uart = false;
    s_ackPending = false;
}

bool mpu401_port_in8(uint16_t port, uint8_t *value) {
    switch (port) {
    case MPU_STATUS:
        *value = s_ackPending ? 0x3f : 0xbf;
        return true;
    case MPU_DATA:
        *value = s_ackPending ? MPU_ACK : 0xff;
        s_ackPending = false;
        return true;
    default:
        return false;
    }
}

bool mpu401_port_out8(uint16_t port, uint8_t value) {
    switch (port) {
    case MPU_STATUS:
        if (value == 0x3f)
            s_uart = true;
        else if (value == 0xff)
            s_uart = false;
        s_ackPending = true;
        return true;
    case MPU_DATA:
        if (s_uart)
            (s_sink != NULL ? s_sink : dump_midi)(value);
        return true;
    default:
        return false;
    }
}
