/*
 * Emulated VGA display memory and presentation.
 *
 * The original keeps four Mode-Y pages in VRAM, named by the segment of their
 * planar base: A000, A400, A800 and the AC00 scratch page. Natively each is a
 * chunky 320x200 buffer of palette indices. The page the CRTC start address
 * selects is converted through the emulated DAC palette and shown in a window
 * scaled to a 4:3 picture, as on a VGA monitor.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "bak_video.h"
#include "platform.h"
#include "vgaregs.h"

#define PAGE_COUNT 4
#define PAGE_BYTES (BAK_VGA_WIDTH * BAK_VGA_HEIGHT)

static uint8_t s_pages[PAGE_COUNT][PAGE_BYTES];
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;

static int page_index(uint16_t pageSeg) {
    switch (pageSeg) {
    case 0xA000:
        return 0;
    case 0xA400:
        return 1;
    case 0xA800:
        return 2;
    case 0xAC00:
        return 3;
    default:
        return -1;
    }
}

uint8_t *bak_video_page(uint16_t pageSeg) {
    int i = page_index(pageSeg);
    if (i < 0) {
        char what[48];
        snprintf(what, sizeof what, "video page at segment %04X", pageSeg);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    return s_pages[i];
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

/* Show the page the CRTC is scanning out. */
void video_present(void) {
    if (s_texture == NULL)
        return;
    /* In Mode Y the start address counts bytes per plane: page bases differ by 4000h. */
    int index = (vga_crtc_start_address() / 0x4000) & 3;
    const uint8_t *src = s_pages[index];
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
        for (int x = 0; x < BAK_VGA_WIDTH; x++)
            row[x] = pal[src[y * BAK_VGA_WIDTH + x]];
    }
    SDL_UnlockTexture(s_texture);
    SDL_RenderClear(s_renderer);
    SDL_FRect dst = {0, 0, 320, 240};
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
}
