/*
 * Emulated VGA display memory and presentation.
 *
 * Display memory is the chunky 256 KB model of bak_video.h. Presenting converts the
 * 320x200 window the CRTC start address selects through the emulated DAC palette and
 * shows it scaled to a 4:3 picture, as on a VGA monitor.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "bak_video.h"
#include "platform.h"
#include "vgaregs.h"

uint8_t g_bakVram[BAK_VRAM_SIZE];

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;

uint8_t *bak_video_page(uint16_t pageSeg) {
    if (pageSeg < 0xA000) {
        char what[48];
        snprintf(what, sizeof what, "video page at segment %04X", pageSeg);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    return &g_bakVram[(uint32_t)(pageSeg - 0xA000u) * 64u];
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
    /* 320x200 pixels shown as a 4:3 picture: logical size 320x240, stretched vertically. */
    SDL_SetRenderLogicalPresentation(s_renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, BAK_VGA_WIDTH, BAK_VGA_HEIGHT);
    if (s_texture == NULL) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    SDL_HideCursor(); /* the game draws its own */
    return true;
}

bool video_window_to_picture(float wx, float wy, float *px, float *py) {
    float lx, ly;
    if (s_renderer == NULL || !SDL_RenderCoordinatesFromWindow(s_renderer, wx, wy, &lx, &ly))
        return false;
    /* Logical 320x240 back to the 320x200 the 4:3 picture stretches. */
    *px = lx;
    *py = ly * (float)BAK_VGA_HEIGHT / 240.0f;
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

/* Show the window the CRTC is scanning out. */
void video_present(void) {
    if (s_texture == NULL)
        return;
    /* The start address counts planar bytes: four chunky pixels each. */
    uint32_t start = (uint32_t)vga_crtc_start_address() * 4u;
    void *pixels;
    int pitch;
    if (!SDL_LockTexture(s_texture, NULL, &pixels, &pitch))
        return;
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = g_vga.dac[i][0], g = g_vga.dac[i][1], b = g_vga.dac[i][2];
        /* 6-bit DAC components to 8 bits. */
        pal[i] = (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
    }
    for (int y = 0; y < BAK_VGA_HEIGHT; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
        uint32_t src = start + (uint32_t)y * BAK_VGA_STRIDE;
        for (int x = 0; x < BAK_VGA_WIDTH; x++)
            row[x] = pal[g_bakVram[(src + (uint32_t)x) & (BAK_VRAM_SIZE - 1)]];
    }
    SDL_UnlockTexture(s_texture);
    SDL_RenderClear(s_renderer);
    SDL_FRect dst = {0, 0, 320, 240};
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
}
