/*
 * CDAUDIO.ASM in C: the 1.02 release's CD audio through MSCDEX (INT 2Fh).
 *
 * The state lives in GMAIN.C's g_cdaudio_state at the offsets the original addresses,
 * including the MSCDEX device request header at +19h that the requests are built in.
 * Without MSCDEX (port/platform/cdrom.c) INT 2Fh changes nothing, so no drive is found
 * and requests fail on the status the header already holds, as on DOS. A far pointer
 * the original stores into the header (the IOCTL control block's address) holds the
 * bits of a host pointer natively.
 */
#include <string.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/CDAUDIO.H"
#include "bak_game_end.h"

extern unsigned char g_cdaudio_state[70];

#define ST g_cdaudio_state
#define LBA_BASE 0x00   /* word */
#define DRIVE_NO 0x02   /* word: 0-based drive letter, FFFFh: none */
#define IN_A 0x04       /* dword: play length (frames), 0 means 50000h */
#define IN_B 0x08       /* dword: start bias */
#define LBA 0x0C        /* dword: the play start sent */
#define BYTEOFF 0x10    /* dword: the cached track's start, in frames */
#define LAST_TRACK 0x14 /* word: the track BYTEOFF belongs to */
#define PRESENT 0x16
#define PLAYING 0x18 /* 0 while playing, 1 once stopped */
#define REQ 0x19     /* the request header: length, unit, command, status */
#define REQ_STATUS_HI 0x1D
#define REQ_MEDIA 0x26   /* header +0Dh */
#define REQ_ADDRESS 0x27 /* header +0Eh: dword */
#define REQ_COUNT 0x2B   /* header +12h: dword */
#define IOCTL_BUF 0x33   /* control block: code, track, start (frame, sec, min, 0), info */
#define IOCTL_TRACK 0x34
#define CURRENT_DRIVE 2 /* INT 21h AH=19h: the game runs from C: */

static u16 rd16(unsigned off) {
    return (u16)(ST[off] | ST[off + 1] << 8);
}

static void wr16(unsigned off, u16 v) {
    ST[off] = (unsigned char)v;
    ST[off + 1] = (unsigned char)(v >> 8);
}

static u32 rd32(unsigned off) {
    return (u32)rd16(off) | (u32)rd16(off + 2) << 16;
}

static void wr32(unsigned off, u32 v) {
    wr16(off, (u16)v);
    wr16(off + 2, (u16)(v >> 16));
}

s16 v102_cdaudio_mscdex_request(void) {
    if (bak_mscdex_installed())
        bak_mscdex_request(rd16(DRIVE_NO), ST + REQ);
    return (ST[REQ_STATUS_HI] & 0x80) ? -1 : 0;
}

void v102_cdaudio_set_track_msf(s16 track, s32 msf_bias, s32 frame_offset) {
    wr32(IN_B, (u32)msf_bias);
    wr32(IN_A, (u32)frame_offset);
    if ((u16)track == rd16(LAST_TRACK))
        return;
    ST[IOCTL_TRACK] = (unsigned char)track;
    ST[REQ + 2] = 0x03; /* IOCTL input: audio track info */
    ST[REQ] = 0x1a;
    wr16(REQ_COUNT, 7);
    unsigned char *block = ST + IOCTL_BUF;
    u32 address;
    memcpy(&address, &block, sizeof address);
    wr32(REQ_ADDRESS, address);
    if (v102_cdaudio_mscdex_request() != 0)
        return;
    wr16(LAST_TRACK, (u16)track);
    /* Minutes, seconds, frames to frames, with the original's register widths: the
     * minute byte is added to AL alone, and the * 75 drops the high word before it. */
    u16 ax = (u16)(ST[IOCTL_BUF + 5] * 60);
    ax = (u16)((ax & 0xff00) | (unsigned char)(ax + ST[IOCTL_BUF + 4]));
    ax = (u16)(ax * 60u);
    ax = (u16)(ax + ST[IOCTL_BUF + 3]);
    u32 frames = (u32)ax * 75u + ST[IOCTL_BUF + 2];
    wr32(BYTEOFF, frames);
}

void v102_cdaudio_play(void) {
    ST[PLAYING] = 0;
    ST[REQ + 2] = 0x84;
    ST[REQ] = 0x16;
    u32 length = rd32(IN_A);
    wr32(REQ_COUNT, length != 0 ? length : 0x50000u);
    u32 start = rd32(IN_B) + rd32(BYTEOFF) - rd16(LBA_BASE);
    wr32(REQ_ADDRESS, start);
    wr32(LBA, start);
    v102_cdaudio_mscdex_request();
}

void v102_cdaudio_stop(void) {
    ST[REQ + 2] = 0x85;
    ST[REQ] = 0x0d;
    v102_cdaudio_mscdex_request();
    ST[PLAYING] = 1;
}

s16 v102_cddrive_detect(char drive_letter) {
    unsigned char letter = (unsigned char)drive_letter & 0x5f;
    u16 drive = letter != 0 ? (unsigned char)(letter - 'A') : CURRENT_DRIVE;
    u16 ax = 0x150b, bx = 0;
    wr16(DRIVE_NO, drive);
    if (bak_mscdex_installed())
        ax = bak_mscdex_drive_check(drive, &bx);
    if (bx != 0xadad || ax == 0) {
        wr16(DRIVE_NO, 0xffff);
        return 0;
    }
    ST[REQ + 1] = 0;
    ST[REQ_MEDIA] = 0;
    ST[REQ + 2] = 0;
    ST[REQ] = 0x0d;
    ST[PRESENT] = 1;
    return 1;
}
