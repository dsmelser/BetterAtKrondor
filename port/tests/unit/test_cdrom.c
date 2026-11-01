/* The CD drive the music's files make: their lengths from the Ogg pages, the disc laid out from
 * them, and a play request's stretch of the disc taken to a track. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/audio.h"
#include "../../platform/platform.h"
#include "bak_port.h"
#include "tone_ogg.h"

#define T0 1000000000ull
#define MS 1000000ull

static void put32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; i++)
        p[i] = (unsigned char)(v >> 8 * i);
}

/* An Ogg Vorbis stream's first page (the identification header) and its last page (granule:
 * the length in samples), with a decoy page header in the last page's data. */
static void write_ogg(const char *path, int channels, uint32_t rate, uint32_t samples) {
    unsigned char f[189];
    memset(f, 0, sizeof f);
    memcpy(f, "OggS", 4);
    f[5] = 2; /* beginning of stream */
    f[26] = 1;
    f[27] = 30;
    f[28] = 1;
    memcpy(f + 29, "vorbis", 6);
    f[39] = (unsigned char)channels;
    put32(f + 40, rate);
    f[56] = 0xb8;
    f[57] = 1;
    memcpy(f + 100, "OggS", 4); /* the last page: 27 bytes, two segments of 30, 30 bytes */
    f[105] = 4;                 /* end of stream */
    put32(f + 106, samples);
    f[126] = 2;
    f[127] = 30;
    f[128] = 30;
    memcpy(f + 150, "OggS", 4); /* data that looks like a page's start */
    UNIT_CHECK(SDL_SaveFile(path, f, sizeof f));
}

void unit_cdrom_reads_an_ogg_files_length(void) {
    const char *dir = unit_temp_dir("cdrom-ogg");
    char path[512];
    int rate = 0, channels = 0;
    uint64_t samples = 0;
    SDL_snprintf(path, sizeof path, "%s/a.ogg", dir);
    write_ogg(path, 2, 44100, 441000);
    UNIT_CHECK(cdrom_ogg_info(path, &rate, &channels, &samples));
    UNIT_CHECK(rate == 44100 && channels == 2 && samples == 441000);
    SDL_snprintf(path, sizeof path, "%s/b.ogg", dir);
    static const char notOgg[] = "not an Ogg stream, though longer than a page header is, by far";
    UNIT_CHECK(SDL_SaveFile(path, notOgg, sizeof notOgg));
    UNIT_CHECK(!cdrom_ogg_info(path, &rate, &channels, &samples));
    SDL_snprintf(path, sizeof path, "%s/none.ogg", dir);
    UNIT_CHECK(!cdrom_ogg_info(path, &rate, &channels, &samples));
    unit_remove_tree(dir);
}

/* Track info for track t through an IOCTL request as the game builds it: minute, second,
 * frame of its start, or -1 when the drive turns it down. */
static int track_msf(int t) {
    unsigned char hdr[0x1a] = {0x1a, 0, 0x03};
    unsigned char cb[7] = {0x0b, (unsigned char)t};
    unsigned char *p = cb;
    uint32_t address;
    memcpy(&address, &p, sizeof address);
    put32(hdr + 0x0e, address);
    bak_mscdex_request(3, hdr);
    if (hdr[4] & 0x80)
        return -1;
    return cb[4] << 16 | cb[3] << 8 | cb[2];
}

void unit_cdrom_lays_out_the_disc_from_the_music(void) {
    const char *dir = unit_temp_dir("cdrom-disc");
    char path[512];
    uint16_t bx = 0;
    int64_t from = 0;
    SDL_snprintf(path, sizeof path, "%s/music", dir);
    SDL_CreateDirectory(path);
    UNIT_CHECK(!cdrom_open_music(dir)); /* no tracks, no drive */
    UNIT_CHECK(!bak_mscdex_installed());
    SDL_snprintf(path, sizeof path, "%s/music/bak02.ogg", dir);
    write_ogg(path, 2, 44100, 441000); /* 10 s */
    SDL_snprintf(path, sizeof path, "%s/music/bak04.ogg", dir);
    write_ogg(path, 1, 44100, 220500); /* 5 s; no track 3 */
    SDL_snprintf(path, sizeof path, "%s/music/bak05.ogg", dir);
    write_ogg(path, 2, 22050, 22050); /* not a CD's rate: left out */
    SDL_snprintf(path, sizeof path, "%s/music/bak06.ogg", dir);
    write_ogg(path, 2, 44100, 132300); /* 3 s */
    SDL_snprintf(path, sizeof path, "%s/music/bak09.ogg", dir);
    write_ogg(path, 2, 44100, 44100u * 6000u); /* 100 minutes: past what a disc holds */
    UNIT_CHECK(cdrom_open_music(dir));
    UNIT_CHECK(bak_mscdex_installed());
    UNIT_CHECK(bak_mscdex_drive_check(3, &bx) == 0x5ad8 && bx == 0xadad); /* D:, asked first */
    UNIT_CHECK(bak_mscdex_drive_check(4, &bx) == 0);
    UNIT_CHECK(track_msf(1) == 0x000200); /* the data track at 00:02:00 */
    UNIT_CHECK(track_msf(2) == 0x010400); /* after a minute of data and a 2 s gap */
    UNIT_CHECK(track_msf(3) == 0x011000); /* 10 s on, and a gap: a second of silence */
    UNIT_CHECK(track_msf(4) == 0x011300);
    UNIT_CHECK(track_msf(5) == 0x011a00); /* left out: a second of silence too */
    UNIT_CHECK(track_msf(6) == 0x011d00);
    UNIT_CHECK(track_msf(7) == 0x012200); /* 3 s on */
    UNIT_CHECK(track_msf(8) == 0x012500);
    UNIT_CHECK(track_msf(9) == -1); /* not on the disc */
    /* The game starts a track 39 frames early, in the gap before it (HSG = MSF - 150). */
    UNIT_CHECK(cdrom_track_at(4800 - 150 - 39, &from) == 2 && from == -39);
    UNIT_CHECK(cdrom_track_at(4800 - 150 + 100, &from) == 2 && from == 100);
    UNIT_CHECK(cdrom_track_at(5700 - 150 - 150, &from) == 3 && from == -150);
    UNIT_CHECK(cdrom_track_at(5925 - 150 + 7, &from) == 4 && from == 7);
    UNIT_CHECK(cdrom_track_at(1000, &from) == 0); /* the data track */
    audio_close();
    bak_cdrom_attach(-1, 0, 0, NULL);
    UNIT_CHECK(!bak_mscdex_installed());
    unit_remove_tree(dir);
}

/* A play request as the game builds it: from an HSG sector, for a number of frames. */
static void request_play(uint32_t sector, uint32_t frames) {
    unsigned char hdr[0x16] = {0x16, 0, 0x84};
    put32(hdr + 0x0e, sector);
    put32(hdr + 0x12, frames);
    bak_mscdex_request(3, hdr);
}

static void request_stop(void) {
    unsigned char hdr[0x0d] = {0x0d, 0, 0x85};
    bak_mscdex_request(3, hdr);
}

/* The mix's left (ch 0) or right channel over its frames a to b: its RMS level, and whether it
 * is all silence. */
static double level(const float *mix, int a, int b, int ch) {
    double sum = 0;
    for (int i = a; i < b; i++)
        sum += (double)mix[2 * i + ch] * mix[2 * i + ch];
    return SDL_sqrt(sum / (b - a));
}

static bool silent(const float *mix, int a, int b) {
    for (int i = 2 * a; i < 2 * b; i++)
        if (mix[i] != 0.0f)
            return false;
    return true;
}

static bool near(double v, double want) {
    return v > want * 0.85 && v < want * 1.15;
}

/* The tones: half a second each, a stereo one as track 2 (MSF 4800: HSG 4650) and a mono one as
 * track 3 (4800 + 38 frames + the gap: HSG 4838). The mix is 48 kHz, an event heard 50 ms after
 * its moment; around a change SDL's converter spreads over a few frames. */
void unit_cdrom_plays_a_stretch_of_a_track(void) {
    static float mix[48000 * 2];
    const char *dir = unit_temp_dir("cdrom-play");
    char path[512];
    uint16_t bx;
    SDL_snprintf(path, sizeof path, "%s/music", dir);
    SDL_CreateDirectory(path);
    SDL_snprintf(path, sizeof path, "%s/music/bak02.ogg", dir);
    SDL_SaveFile(path, s_toneStereo, sizeof s_toneStereo);
    SDL_snprintf(path, sizeof path, "%s/music/bak03.ogg", dir);
    SDL_SaveFile(path, s_toneMono, sizeof s_toneMono);
    clock_use_virtual(T0);
    UNIT_CHECK(cdrom_open_music(dir));
    bak_mscdex_drive_check(3, &bx);
    audio_open_dump(NULL, T0);

    /* 10 frames into the gap before track 2, for 20: from 50 ms, 10 frames (133 ms) of silence,
     * then 10 of the tone, cut where the play ends (317 ms: mix frame 15200). */
    request_play(4650 - 10, 20);
    audio_mix(mix, 24000);
    UNIT_CHECK(silent(mix, 0, 8800 - 30));
    UNIT_CHECK(near(level(mix, 9000, 15000, 0), 0.5 / SDL_sqrt(2)));
    UNIT_CHECK(near(level(mix, 9000, 15000, 1), 0.25 / SDL_sqrt(2)));
    UNIT_CHECK(silent(mix, 15200 + 30, 24000));

    /* Track 3 from its start for 100 frames: the mono file, on both sides, ends first. */
    clock_spend(500 * MS);
    request_play(4838, 100);
    audio_mix(mix, 48000); /* from 0.5 s: heard at mix frame 2400, for half a second */
    UNIT_CHECK(silent(mix, 0, 2400 - 30));
    UNIT_CHECK(near(level(mix, 2600, 26200, 0), 0.5 / SDL_sqrt(2)));
    bool sameSides = true;
    for (int i = 2400; i < 26400; i++)
        sameSides &= mix[2 * i] == mix[2 * i + 1];
    UNIT_CHECK(sameSides);
    UNIT_CHECK(silent(mix, 26400 + 30, 48000));

    /* Again, stopped 200 ms in. */
    clock_spend(1000 * MS);
    request_play(4838, 100);
    clock_spend(200 * MS);
    request_stop();
    audio_mix(mix, 48000); /* from 1.5 s */
    UNIT_CHECK(near(level(mix, 2600, 11800, 0), 0.5 / SDL_sqrt(2)));
    UNIT_CHECK(silent(mix, 12000 + 30, 48000));

    audio_close();
    bak_cdrom_attach(-1, 0, 0, NULL);
    unit_remove_tree(dir);
}
