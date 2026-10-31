/*
 * MSCDEX, the DOS CD-ROM extension (INT 2Fh, AH=15h), for the 1.02 release's CD audio.
 *
 * Without a drive attached MSCDEX is absent: INT 2Fh leaves the registers as they were,
 * as on a DOS machine without it, and the game plays its music on the sound card. A
 * drive is a table of audio track start positions; device requests are carried out on
 * the request header the game builds (status word at +3; IOCTL input with the control
 * block address at +0Eh; play with the start at +0Eh and the length at +12h). Playing
 * the GOG release's OGG tracks belongs to the audio work; here a play request only
 * records what was asked.
 */
#include <string.h>

#include "bak_port.h"

#define STATUS_ERROR 0x8000
#define STATUS_BUSY 0x0200
#define STATUS_DONE 0x0100

static int s_drive = -1; /* 0-based drive letter of the CD, -1: MSCDEX absent */
static int s_firstTrack, s_lastTrack;
static unsigned char s_trackStart[100][3]; /* minute, second, frame */
static uint32_t s_playStart, s_playLength;
static int s_playing;

void bak_cdrom_attach(int drive, int firstTrack, int lastTrack, const unsigned char (*starts)[3]) {
    s_drive = drive;
    s_firstTrack = firstTrack;
    s_lastTrack = lastTrack;
    memset(s_trackStart, 0, sizeof s_trackStart);
    if (drive >= 0)
        memcpy(s_trackStart[firstTrack], starts, (size_t)(lastTrack - firstTrack + 1) * 3);
    s_playing = 0;
}

int bak_mscdex_installed(void) {
    return s_drive >= 0;
}

uint16_t bak_mscdex_drive_check(uint16_t drive, uint16_t *bx) {
    *bx = 0xADAD;
    return drive == (uint16_t)s_drive ? 0x5AD8 : 0;
}

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void set_status(unsigned char *hdr, uint16_t status) {
    hdr[3] = (unsigned char)status;
    hdr[4] = (unsigned char)(status >> 8);
}

void bak_mscdex_request(uint16_t drive, unsigned char *hdr) {
    if (drive != (uint16_t)s_drive) {
        set_status(hdr, STATUS_ERROR | STATUS_DONE | 0x01); /* unknown unit */
        return;
    }
    switch (hdr[2]) {
    case 0x03: { /* IOCTL input: the control block's address, a far pointer as stored */
        unsigned char *cb;
        uint32_t address = rd32(hdr + 0x0E);
        memcpy(&cb, &address, sizeof cb);
        if (cb[0] == 0x0B && cb[1] >= s_firstTrack && cb[1] <= s_lastTrack) {
            /* Audio track info: start as Red Book frame, second, minute, 0; control. */
            cb[2] = s_trackStart[cb[1]][2];
            cb[3] = s_trackStart[cb[1]][1];
            cb[4] = s_trackStart[cb[1]][0];
            cb[5] = 0;
            cb[6] = 0x01;
            set_status(hdr, STATUS_DONE);
        } else {
            set_status(hdr, STATUS_ERROR | STATUS_DONE | 0x03); /* unknown command */
        }
        break;
    }
    case 0x84: /* play audio */
        s_playStart = rd32(hdr + 0x0E);
        s_playLength = rd32(hdr + 0x12);
        s_playing = 1;
        set_status(hdr, STATUS_DONE | STATUS_BUSY);
        break;
    case 0x85: /* stop audio */
        s_playing = 0;
        set_status(hdr, STATUS_DONE);
        break;
    default:
        set_status(hdr, STATUS_ERROR | STATUS_DONE | 0x03);
        break;
    }
}
