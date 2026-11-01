/* The built-in video drivers, by VMCODE.OVL chunk index (see bak_video_driver_init), and
 * the sound drivers, by the device name in an SX.OVL chunk's DUDE header (dude.h). */
#include <stdio.h>
#include <string.h>

#include "dude.h"
#include "heg.h"
#include "vga.h"

const void *bak_video_driver_init(int16_t chunk) {
    switch (chunk) {
    case 8: /* "VGA:" */
        return vga_driver_init();
    case 0xB: /* "HEG:" */
        return heg_driver_init();
    default:
        return NULL;
    }
}

/*
 * The ROM 8x8 font video_init installs as font slot 1. The game switches to its own
 * font before drawing any text, so the glyphs are left blank rather than copying a
 * BIOS font; the slot only has to be occupied for font slot numbering to match.
 */
unsigned char *bak_rom_font_8x8(void) {
    static unsigned char font[128 * 8];
    return font;
}

DudeEntry *g_dudeTestSynth;

static const struct {
    const char *name;
    DudeEntry *entry;
} s_synths[] = {
    {"Roland MT32 with MPU interface", m32_dispatch},
    {"AdLib Music Synthesizer Card", adl_dispatch},
};

/* The DUDE header: entry jump (3 bytes), a pad byte, magic 87654321h, the interface
 * version (2 bytes), a tag ("dude" in version 1, 8 bytes in version 2), then the device
 * name as a Pascal string. Returns the name's offset, 0 for no DUDE header. */
static int dude_name_offset(const unsigned char *chunk) {
    static const unsigned char magic[4] = {0x21, 0x43, 0x65, 0x87};
    if (memcmp(chunk + 4, magic, 4) != 0)
        return 0;
    if (chunk[8] == 1 && memcmp(chunk + 0x0a, "dude", 4) == 0)
        return 0x0e;
    return chunk[8] == 2 ? 0x12 : 0;
}

void dude_device_name(const unsigned char *chunk, char *out, int size) {
    int at = dude_name_offset(chunk);
    int n = at != 0 ? chunk[at] : 0;
    if (n > size - 1)
        n = size - 1;
    memcpy(out, chunk + at + 1, (size_t)n);
    out[n] = '\0';
}

DudeDigitalEntry *g_dudeTestDigital;

DudeDigitalEntry *dude_digital_entry(const unsigned char *chunk) {
    char name[64];
    if (dude_name_offset(chunk) != 0x12)
        return NULL;
    dude_device_name(chunk, name, sizeof name);
    if (g_dudeTestDigital != NULL && strcmp(name, "difftest") == 0)
        return g_dudeTestDigital;
    if (strcmp(name, "CMS Sound Blaster") == 0)
        return asb_dispatch;
    return NULL;
}

uint16_t dude_digital_call(unsigned char *chunk, uint16_t cmd, void *args) {
    DudeDigitalEntry *entry = chunk != NULL ? dude_digital_entry(chunk) : NULL;
    if (entry == NULL) {
        char name[64] = "", what[128];
        if (chunk != NULL)
            dude_device_name(chunk, name, sizeof name);
        snprintf(what, sizeof what, "the SX.OVL sound driver \"%s\"", name);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    return entry(chunk, cmd, args);
}

DudeEntry *dude_synth_entry(const unsigned char *chunk) {
    char name[64];
    if (dude_name_offset(chunk) != 0x0e)
        return NULL;
    dude_device_name(chunk, name, sizeof name);
    if (g_dudeTestSynth != NULL && strcmp(name, "difftest") == 0)
        return g_dudeTestSynth;
    for (size_t i = 0; i < sizeof s_synths / sizeof s_synths[0]; i++)
        if (strcmp(name, s_synths[i].name) == 0)
            return s_synths[i].entry;
    return NULL;
}
