/*
 * The game's mouse cursor as the system's (video.cursor = system, the default). Where the game
 * would draw its cursor into the picture it hands the image over (bak_cursor_system, from
 * SCREEN.C), and the window shows it as an SDL colour cursor, at the picture's scale and in the
 * DAC's colours, moving with the host's pointer rather than with the game's frames. With
 * video.cursor = game the game draws it, as the original did; the DOSBox goldens compare those
 * pictures.
 */
#include <string.h>

#include <SDL3/SDL.h>

#include "platform.h"
#include "vgaregs.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

typedef struct CursorImage {
    int w, h, hotX, hotY; /* w = 0: no cursor */
    uint16_t px[CURSOR_MAX * CURSOR_MAX];
} CursorImage;

static CursorImage s_wanted; /* what the game shows */
static CursorImage s_built;  /* what s_cursor shows */
static uint32_t s_builtRgb[CURSOR_MAX * CURSOR_MAX];
static float s_builtSx, s_builtSy;
static SDL_Cursor *s_cursor;
static bool s_shown;

static uint8_t image_byte(const uint8_t *seg, uint32_t extent, uint32_t at) {
    return at < extent ? seg[at] : 0;
}

/*
 * The VGA driver's images (vga_blit_sprite_master): row-major bytes, colour 0 transparent, rows
 * width & ~3 bytes long (the last width & 3 pixels of a row are never drawn), from the record's
 * offset rounded down to a multiple of 4. The 16-colour driver's (heg_blit_sprite_master): four
 * bit planes one after the other, each width / 8 bytes a row, the leftmost pixel in the top bit;
 * a pixel is colour 0, transparent, in all four.
 */
bool cursor_decode(const struct ImageRecord *image, bool colors256, int *w, int *h, uint16_t *px) {
    const uint8_t *seg = image->pImageBase;
    uint32_t extent = image->nImageExtent;
    int width = colors256 ? image->nWidth & ~3 : image->nWidth, height = image->nHeight;
    if (width <= 0 || height <= 0 || width > CURSOR_MAX || height > CURSOR_MAX)
        return false;
    uint32_t stride = (uint32_t)(image->nWidth >> 3);
    uint32_t planeSize = (uint16_t)(image->wUnk4 - image->wImageOff) >> 2;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned value = 0;
            if (colors256) {
                value =
                    image_byte(seg, extent, (image->wImageOff & ~3u) + (uint32_t)(y * width + x));
            } else {
                uint32_t at = image->wImageOff + (uint32_t)y * stride + (uint32_t)(x >> 3);
                for (unsigned plane = 0; plane < 4; plane++)
                    value |= (image_byte(seg, extent, at + plane * planeSize) >> (7 - (x & 7)) & 1u)
                             << plane;
            }
            px[y * width + x] = value == 0  ? CURSOR_CLEAR
                                : colors256 ? (uint16_t)value
                                            : video_attribute_color(value);
        }
    }
    *w = width;
    *h = height;
    return true;
}

_Bool bak_cursor_system(const struct ImageRecord *image, int16_t hotX, int16_t hotY) {
    if (settings_get_choice(SETTING_VIDEO_CURSOR) == CURSOR_GAME)
        return false;
    s_wanted.w = 0;
    bool colors256 = (g_vga.gc[5] & 0x40) != 0;
    if (image != NULL && cursor_decode(image, colors256, &s_wanted.w, &s_wanted.h, s_wanted.px)) {
        s_wanted.hotX = hotX;
        s_wanted.hotY = hotY;
    }
    return true;
}

bool cursor_shown(void) {
    return s_shown;
}

static uint32_t dac_rgb(uint16_t index) {
    const uint8_t *c = g_vga.dac[index & g_vga.pelMask];
    /* 6-bit DAC components to 8 bits. */
    return 0xFF000000u | (uint32_t)(c[0] << 2 | c[0] >> 4) << 16 |
           (uint32_t)(c[1] << 2 | c[1] >> 4) << 8 | (uint32_t)(c[2] << 2 | c[2] >> 4);
}

/* Whether s_cursor shows the wanted image as the DAC colours it now, at this scale. */
static bool built_is_wanted(float sx, float sy) {
    const CursorImage *a = &s_wanted, *b = &s_built;
    if (s_cursor == NULL || sx != s_builtSx || sy != s_builtSy || a->w != b->w || a->h != b->h ||
        a->hotX != b->hotX || a->hotY != b->hotY)
        return false;
    int n = a->w * a->h;
    for (int i = 0; i < n; i++) {
        if (a->px[i] != b->px[i])
            return false;
        if (a->px[i] != CURSOR_CLEAR && dac_rgb(a->px[i]) != s_builtRgb[i])
            return false;
    }
    return true;
}

/* The wanted image scaled up (nearest), its hot spot in the middle of the hot pixel. */
static void build(float sx, float sy) {
    const CursorImage *c = &s_wanted;
    int ow = SDL_max(1, (int)(c->w * sx + 0.5f)), oh = SDL_max(1, (int)(c->h * sy + 0.5f));
    SDL_Surface *surface = SDL_CreateSurface(ow, oh, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL)
        return;
    for (int i = 0; i < c->w * c->h; i++)
        s_builtRgb[i] = c->px[i] == CURSOR_CLEAR ? 0 : dac_rgb(c->px[i]);
    for (int y = 0; y < oh; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels + y * surface->pitch);
        int from = SDL_min(c->h - 1, (int)((float)y / sy));
        for (int x = 0; x < ow; x++)
            row[x] = s_builtRgb[from * c->w + SDL_min(c->w - 1, (int)((float)x / sx))];
    }
    int hx = SDL_clamp((int)((float)c->hotX * sx + sx / 2), 0, ow - 1);
    int hy = SDL_clamp((int)((float)c->hotY * sy + sy / 2), 0, oh - 1);
    const char *dump = SDL_getenv("BAK_DUMP_CURSOR"); /* each one made, for a look */
    if (dump != NULL)
        SDL_SaveBMP(surface, dump);
    SDL_Cursor *cursor = SDL_CreateColorCursor(surface, hx, hy);
    SDL_DestroySurface(surface);
    if (cursor == NULL)
        return;
    SDL_SetCursor(cursor);
    SDL_DestroyCursor(s_cursor);
    s_cursor = cursor;
    s_built = *c;
    s_builtSx = sx;
    s_builtSy = sy;
}

void cursor_update(float sx, float sy) {
    if (settings_get_choice(SETTING_VIDEO_CURSOR) == CURSOR_GAME)
        return;
    if (s_wanted.w != 0 && !built_is_wanted(sx, sy))
        build(sx, sy);
    bool show = s_wanted.w != 0 && s_cursor != NULL;
    if (show != s_shown) {
        if (show)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
        s_shown = show;
    }
}
