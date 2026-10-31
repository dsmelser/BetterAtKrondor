/*
 * Emulated VGA display memory and presentation.
 *
 * Display memory is the chunky 256 KB model of bak_video.h. Presenting reads the
 * picture the CRTC scans out, as the registers describe it: 256 colours with four
 * pixels to a planar byte (Mode Y), or 16 colours with eight pixels to a byte across the
 * four planes, mapped through the attribute controller's palette. It goes through the
 * emulated DAC and is shown scaled to a 4:3 picture, as on a VGA monitor.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "bak_video.h"
#include "platform.h"
#include "vgaregs.h"

#define MAX_WIDTH 640
#define MAX_HEIGHT 480

uint8_t g_bakVram[BAK_VRAM_SIZE];

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static int s_width = BAK_VGA_WIDTH, s_height = BAK_VGA_HEIGHT; /* the picture's pixels */
static uint32_t s_picture[MAX_WIDTH * MAX_HEIGHT];

uint8_t *bak_video_page(uint16_t pageSeg) {
    if (pageSeg < 0xA000) {
        char what[48];
        snprintf(what, sizeof what, "video page at segment %04X", pageSeg);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    return &g_bakVram[(uint32_t)(pageSeg - 0xA000u) * 64u];
}

/* A texture and logical size for a w x h picture shown at 4:3. */
static bool set_picture_size(int w, int h) {
    SDL_DestroyTexture(s_texture);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, w, h);
    if (s_texture == NULL) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    /* w x h pixels shown as a 4:3 picture: logical size w x 3w/4, stretched vertically. */
    SDL_SetRenderLogicalPresentation(s_renderer, w, w * 3 / 4, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s_width = w;
    s_height = h;
    return true;
}

bool video_open(void) {
    s_window = SDL_CreateWindow("BetterAtKrondor", 960, 720, SDL_WINDOW_RESIZABLE);
    if (s_window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    s_renderer = SDL_CreateRenderer(s_window, NULL);
    if (s_renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    if (!set_picture_size(BAK_VGA_WIDTH, BAK_VGA_HEIGHT))
        return false;
    SDL_HideCursor(); /* the game draws its own */
    return true;
}

bool video_window_to_picture(float wx, float wy, float *px, float *py) {
    float lx, ly;
    if (s_renderer == NULL || !SDL_RenderCoordinatesFromWindow(s_renderer, wx, wy, &lx, &ly))
        return false;
    /* The logical w x 3w/4 back to the w x h the 4:3 picture stretches. */
    *px = lx;
    *py = ly * (float)s_height / (float)(s_width * 3 / 4);
    return true;
}

void video_close(void) {
    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    s_texture = NULL;
    s_renderer = NULL;
    s_window = NULL;
}

/* BAK_DUMP_FRAME=<file.bmp>: write every presented frame there too (headless checks). */
static void dump_frame(void) {
    static const char *path;
    static bool checked;
    if (!checked) {
        checked = true;
        path = SDL_getenv("BAK_DUMP_FRAME");
    }
    if (path == NULL)
        return;
    SDL_Surface *s = SDL_CreateSurfaceFrom(s_width, s_height, SDL_PIXELFORMAT_XRGB8888, s_picture,
                                           s_width * 4);
    if (s == NULL)
        return;
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

/*
 * The picture's size from the CRTC: the horizontal display end in character clocks (four
 * pixels each at 256 colours, eight at 16), the vertical display end in scan lines over
 * the lines each row is repeated.
 */
static void picture_size(bool colors256, int *w, int *h) {
    const uint8_t *c = g_vga.crtc;
    int lines = (c[0x12] | (c[0x07] & 0x02) << 7 | (c[0x07] & 0x40) << 3) + 1;
    int perRow = ((c[0x09] & 0x1F) + 1) * (c[0x09] & 0x80 ? 2 : 1);
    *w = (c[0x01] + 1) * (colors256 ? 4 : 8);
    *h = lines / perRow;
    if (*w > MAX_WIDTH)
        *w = MAX_WIDTH;
    if (*h > MAX_HEIGHT)
        *h = MAX_HEIGHT;
}

/* The DAC index of a 16-colour pixel: the attribute controller's palette register,
 * with bits 4-7 from the colour select register as its paging mode says. */
static uint8_t attribute_color(unsigned nibble) {
    const uint8_t *a = g_vga.attr;
    uint8_t v = a[nibble & a[0x12] & 0x0F];
    if (a[0x10] & 0x80)
        return (uint8_t)((v & 0x0F) | (a[0x14] & 0x0F) << 4);
    return (uint8_t)((v & 0x3F) | (a[0x14] & 0x0C) << 4);
}

/* Read the picture the CRTC is scanning out into s_picture. */
static void scan_out(const uint32_t *pal) {
    bool colors256 = (g_vga.gc[5] & 0x40) != 0;
    int w, h;
    picture_size(colors256, &w, &h);
    if ((w != s_width || h != s_height) && !set_picture_size(w, h))
        return;
    /* The start address and the row pitch count planar bytes (byte mode: twice the
     * offset register). */
    uint32_t start = vga_crtc_start_address();
    uint32_t pitch = g_vga.crtc[0x13] * 2u;
    for (int y = 0; y < h; y++) {
        uint32_t *row = &s_picture[y * w];
        uint32_t o = start + (uint32_t)y * pitch;
        if (colors256) {
            for (int x = 0; x < w; x++)
                row[x] = pal[g_bakVram[(o * 4u + (uint32_t)x) & (BAK_VRAM_SIZE - 1)] &
                             g_vga.pelMask];
            continue;
        }
        for (int x = 0; x < w; x += 8) {
            const uint8_t *p = &g_bakVram[((o + (uint32_t)(x >> 3)) * 4u) & (BAK_VRAM_SIZE - 1)];
            for (int b = 0; b < 8; b++) {
                unsigned bit = 7u - (unsigned)b, nibble = 0;
                for (unsigned plane = 0; plane < 4; plane++)
                    nibble |= ((p[plane] >> bit) & 1u) << plane;
                row[x + b] = pal[attribute_color(nibble) & g_vga.pelMask];
            }
        }
    }
}

/* Show the picture the CRTC is scanning out. */
void video_present(void) {
    if (s_texture == NULL)
        return;
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = g_vga.dac[i][0], g = g_vga.dac[i][1], b = g_vga.dac[i][2];
        /* 6-bit DAC components to 8 bits. */
        pal[i] = (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
    }
    scan_out(pal);
    dump_frame();
    SDL_UpdateTexture(s_texture, NULL, s_picture, s_width * 4);
    SDL_RenderClear(s_renderer);
    SDL_FRect dst = {0, 0, (float)s_width, (float)(s_width * 3 / 4)};
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
}
