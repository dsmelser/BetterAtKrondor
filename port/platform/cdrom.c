/*
 * MSCDEX, the DOS CD-ROM extension (INT 2Fh, AH=15h), and the CD's music, for the 1.02
 * release's CD audio.
 *
 * Without a drive MSCDEX is absent: INT 2Fh leaves the registers as they were, as on a DOS
 * machine without it, and the game plays its music on the sound card. The drive appears when
 * the game folder holds the CD's audio tracks as music\bakNN.ogg (the GOG release's), NN the
 * track from 02 (track 1 is the data). It answers to the first drive letter the game asks about
 * (DRIVE.CFG's), and its table of contents is laid out as on a disc: the data track at 00:02:00,
 * then each audio track after a two-second gap, as long as its file. (The tests attach a drive
 * with a table of their own.)
 *
 * Device requests are carried out on the request header the game builds: status word at +3;
 * IOCTL input with the control block's address at +0Eh; play with the start at +0Eh (an HSG
 * sector: the game's addressing mode) and the length in frames at +12h. A play is a
 * stretch of the disc: the track it starts in plays from there, after what lies in the gap
 * before it as silence, for the frames asked (the game starts each track 39 frames early, in
 * the gap, and plays cd.dat's length of it). The CD's audio source decodes the track's file
 * (stb_vorbis) as the mix asks for it; the file is read and opened when the play is asked for.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "audio.h"
#include "bak_port.h"
#include "platform.h"

#define STATUS_ERROR 0x8000
#define STATUS_BUSY 0x0200
#define STATUS_DONE 0x0100

#define CD_ANY_DRIVE -2
#define FRAME_SAMPLES 588   /* a CD frame (1/75 s) at 44100 Hz */
#define LEAD_IN 150         /* MSF 00:02:00 is HSG sector 0 */
#define GAP 150             /* before each audio track */
#define DATA_FRAMES 4500    /* the data track: a minute */
#define DISC_FRAMES 449999  /* 99:59:74, the last position MSF can name */
#define CD_RATE 44100

static int s_drive = -1; /* 0-based drive letter, -1: MSCDEX absent, CD_ANY_DRIVE: not asked yet */
static int s_firstTrack, s_lastTrack;
static uint32_t s_trackStart[100]; /* in frames from 00:00:00 (MSF), where index 1 begins */
static char *s_trackFile[100];     /* the audio track's file; NULL: none (silence) */
static uint32_t s_playStart, s_playLength;
static int s_playing;
static AudioSource *s_source;

/* ---- the music's files -------------------------------------------------------------------- */

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The offset of the last page in the n bytes p that end an Ogg stream: the page whose header
 * and segments end where the bytes do (a page's data can look like a page's start). SIZE_MAX:
 * none. */
static size_t last_page(const unsigned char *p, size_t n) {
    for (size_t i = n >= 27 ? n - 26 : 0; i-- > 0;) {
        size_t end = i + 27 + p[i + 26];
        if (memcmp(p + i, "OggS", 4) != 0 || p[i + 4] != 0 || end > n)
            continue;
        for (int s = 0; s < p[i + 26]; s++)
            end += p[i + 27 + s];
        if (end == n)
            return i;
    }
    return SIZE_MAX;
}

bool cdrom_ogg_info(const char *path, int *rate, int *channels, uint64_t *samples) {
    unsigned char head[64];
    unsigned char tail[65536];
    bool ok = false;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (io == NULL)
        return false;
    Sint64 size = SDL_GetIOSize(io);
    /* The first page holds the identification header alone: page header, one segment. */
    if (size > 64 && SDL_ReadIO(io, head, sizeof head) == sizeof head &&
        memcmp(head, "OggS", 4) == 0 && head[26] == 1 && memcmp(head + 28, "\x01vorbis", 7) == 0) {
        *channels = head[39];
        *rate = (int)rd32(head + 40);
        /* The last page's granule position is the stream's length in samples. */
        Sint64 from = size > (Sint64)sizeof tail ? size - (Sint64)sizeof tail : 0;
        size_t n = (size_t)(size - from), page;
        if (SDL_SeekIO(io, from, SDL_IO_SEEK_SET) == from && SDL_ReadIO(io, tail, n) == n &&
            (page = last_page(tail, n)) != SIZE_MAX) {
            *samples = (uint64_t)rd32(tail + page + 6) | (uint64_t)rd32(tail + page + 10) << 32;
            ok = *samples != UINT64_MAX && *channels >= 1 && *rate > 0;
        }
    }
    SDL_CloseIO(io);
    return ok;
}

/* ---- the drive ------------------------------------------------------------------------------ */

void bak_cdrom_attach(int drive, int firstTrack, int lastTrack, const unsigned char (*starts)[3]) {
    s_drive = drive;
    s_firstTrack = firstTrack;
    s_lastTrack = lastTrack;
    memset(s_trackStart, 0, sizeof s_trackStart);
    if (drive >= 0)
        for (int t = firstTrack; t <= lastTrack; t++) {
            const unsigned char *msf = starts[t - firstTrack];
            s_trackStart[t] = ((uint32_t)msf[0] * 60 + msf[1]) * 75 + msf[2];
        }
    s_playing = 0;
}

int bak_mscdex_installed(void) {
    return s_drive != -1;
}

uint16_t bak_mscdex_drive_check(uint16_t drive, uint16_t *bx) {
    if (s_drive == CD_ANY_DRIVE)
        s_drive = drive;
    *bx = 0xADAD;
    return drive == (uint16_t)s_drive ? 0x5AD8 : 0;
}

static void set_status(unsigned char *hdr, uint16_t status) {
    hdr[3] = (unsigned char)status;
    hdr[4] = (unsigned char)(status >> 8);
}

/* ---- playing -------------------------------------------------------------------------------- */

typedef struct CdPlay {
    void *file; /* the track's file, which the decoder reads */
    stb_vorbis *vorbis;
    int channels;
    uint64_t silence; /* samples of the gap before the track */
    uint64_t left;    /* samples to play, the gap's included */
} CdPlay;

static CdPlay *s_now; /* what the source plays; the audio side's */

static void cdplay_free(CdPlay *p) {
    if (p == NULL)
        return;
    if (p->vorbis != NULL)
        stb_vorbis_close(p->vorbis);
    SDL_free(p->file);
    SDL_free(p);
}

static void cd_render(void *user, void *out, int frames) {
    float *o = out;
    (void)user;
    while (frames > 0) {
        CdPlay *p = s_now;
        int n = frames;
        if (p == NULL || p->left == 0) {
            memset(o, 0, (size_t)frames * 2 * sizeof *o);
            break;
        }
        if ((uint64_t)n > p->left)
            n = (int)p->left;
        if (p->silence > 0) {
            if ((uint64_t)n > p->silence)
                n = (int)p->silence;
            memset(o, 0, (size_t)n * 2 * sizeof *o);
            p->silence -= (uint64_t)n;
        } else {
            int got = stb_vorbis_get_samples_float_interleaved(p->vorbis, 2, o, n * 2);
            if (p->channels == 1)
                for (int i = 0; i < got; i++)
                    o[2 * i + 1] = o[2 * i];
            if (got < n) { /* the file ends before the play does */
                memset(o + 2 * got, 0, (size_t)(n - got) * 2 * sizeof *o);
                p->left = (uint64_t)n;
            }
        }
        p->left -= (uint64_t)n;
        o += 2 * n;
        frames -= n;
    }
    if (s_now != NULL && s_now->left == 0) {
        cdplay_free(s_now);
        s_now = NULL;
    }
}

/* p: the CdPlay to play now, or NULL: stop. */
static void cd_apply(void *user, const AudioEvent *e) {
    (void)user;
    cdplay_free(s_now);
    s_now = e->p;
}

int cdrom_track_at(uint32_t sector, int64_t *from) {
    uint32_t msf = sector + LEAD_IN;
    for (int t = s_lastTrack; t >= s_firstTrack && t >= 2; t--)
        if (s_trackStart[t] != 0 && msf + GAP >= s_trackStart[t]) {
            *from = (int64_t)msf - s_trackStart[t];
            return t;
        }
    return 0;
}

static void music_play(uint32_t sector, uint32_t frames) {
    int64_t from = 0;
    int t = cdrom_track_at(sector, &from);
    CdPlay *p = NULL;
    audio_log("cd play %u %u: track %d from frame %lld", (unsigned)sector, (unsigned)frames, t,
              (long long)from);
    if (t != 0 && s_trackFile[t] != NULL && (p = SDL_calloc(1, sizeof *p)) != NULL) {
        size_t size = 0;
        int error = 0;
        p->file = SDL_LoadFile(s_trackFile[t], &size);
        if (p->file != NULL)
            p->vorbis = stb_vorbis_open_memory(p->file, (int)size, &error, NULL);
        if (p->vorbis == NULL || (from > 0 &&
                                  !stb_vorbis_seek(p->vorbis, (unsigned)from * FRAME_SAMPLES))) {
            fprintf(stderr, "%s: cannot be played (%d)\n", s_trackFile[t], error);
            cdplay_free(p);
            p = NULL;
        } else {
            p->channels = stb_vorbis_get_info(p->vorbis).channels;
            p->silence = from < 0 ? (uint64_t)-from * FRAME_SAMPLES : 0;
            p->left = (uint64_t)frames * FRAME_SAMPLES;
        }
    }
    audio_post(s_source, 0, 0, p);
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
            uint32_t start = s_trackStart[cb[1]];
            cb[2] = (unsigned char)(start % 75);
            cb[3] = (unsigned char)(start / 75 % 60);
            cb[4] = (unsigned char)(start / 75 / 60);
            cb[5] = 0;
            cb[6] = 0x01;
            set_status(hdr, STATUS_DONE);
        } else {
            set_status(hdr, STATUS_ERROR | STATUS_DONE | 0x03); /* unknown command */
        }
        break;
    }
    case 0x84: /* play audio, from an HSG sector (the game's addressing mode, 0) */
        s_playStart = rd32(hdr + 0x0E);
        s_playLength = rd32(hdr + 0x12);
        s_playing = 1;
        music_play(s_playStart, s_playLength);
        set_status(hdr, STATUS_DONE | STATUS_BUSY);
        break;
    case 0x85: /* stop audio */
        s_playing = 0;
        audio_log("cd stop");
        audio_post(s_source, 0, 0, NULL);
        set_status(hdr, STATUS_DONE);
        break;
    default:
        set_status(hdr, STATUS_ERROR | STATUS_DONE | 0x03);
        break;
    }
}

/* ---- the drive from the game folder's music ------------------------------------------------- */

bool cdrom_open_music(const char *dataDir) {
    static const AudioSourceDesc desc = {"CD music", SDL_AUDIO_F32, 2, CD_RATE, AUDIO_VOLUME_MUSIC,
                                         1.0f, cd_render, cd_apply, NULL};
    uint64_t samples[100];
    uint32_t at = LEAD_IN + DATA_FRAMES;
    int found = 0, last = 1;
    memset(s_trackStart, 0, sizeof s_trackStart);
    for (int t = 2; t <= 99; t++) {
        char path[1024];
        int rate = 0, channels = 0;
        SDL_free(s_trackFile[t]);
        s_trackFile[t] = NULL;
        SDL_snprintf(path, sizeof path, "%s/music/bak%02d.ogg", dataDir, t);
        if (!cdrom_ogg_info(path, &rate, &channels, &samples[t])) {
            samples[t] = CD_RATE; /* a track without a file: a second of silence */
            continue;
        }
        if (rate != CD_RATE) {
            fprintf(stderr, "%s: %d Hz, where the CD's tracks are %d Hz; left out\n", path, rate,
                    CD_RATE);
            samples[t] = CD_RATE;
            continue;
        }
        s_trackFile[t] = SDL_strdup(path);
        found++;
        last = t;
    }
    if (found == 0)
        return false;
    s_trackStart[1] = LEAD_IN;
    for (int t = 2; t <= last; t++) {
        uint32_t frames = (uint32_t)((samples[t] + FRAME_SAMPLES - 1) / FRAME_SAMPLES);
        at += GAP;
        if (at + frames > DISC_FRAMES) {
            last = t - 1; /* past what a disc holds */
            break;
        }
        s_trackStart[t] = at;
        at += frames;
    }
    s_drive = CD_ANY_DRIVE;
    s_firstTrack = 1;
    s_lastTrack = last;
    s_source = audio_source_add(&desc);
    if (SDL_getenv("BAK_TRACE") != NULL)
        fprintf(stderr, "trace: the CD's music: %d tracks up to %d, %u:%02u long\n", found, last,
                (unsigned)(at / 4500), (unsigned)(at / 75 % 60));
    return true;
}
