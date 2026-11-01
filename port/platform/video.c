/*
 * Emulated VGA display memory and presentation.
 *
 * Display memory is the chunky 256 KB model of bak_video.h. Presenting reads the
 * picture the CRTC scans out, as the registers describe it: 256 colours with four
 * pixels to a planar byte (Mode Y), or 16 colours with eight pixels to a byte across the
 * four planes, mapped through the attribute controller's palette. It goes through the
 * emulated DAC and is shown scaled to a 4:3 picture, as on a VGA monitor (or with square
 * pixels), the scaling as the settings ask. The window keeps its place and size between runs;
 * Alt+Enter and F11 switch it to and from the full screen. A picture goes to the window only
 * when it differs from the last, and at most once per refresh of the display.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
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
static int s_shownHeight; /* the height the picture is shown at, in its pixels' widths */
static uint32_t s_picture[MAX_WIDTH * MAX_HEIGHT];
static bool s_offscreen; /* SDL's dummy video driver: nobody sees the window */
static SDL_Rect s_windowed; /* the window's place and size when last neither full nor maximized */
static bool s_placed;       /* s_windowed's place is known */
static bool s_keyEaten[SDL_SCANCODE_COUNT]; /* pressed for the window: its release is too */
static uint32_t s_last[MAX_WIDTH * MAX_HEIGHT]; /* the last picture scanned out that differed */
static int s_lastW, s_lastH;                    /* its size; 0 before the first */
static bool s_stale = true;    /* the window lacks the last picture (or was uncovered, resized) */
static uint64_t s_presentedNs; /* on the host's clock, when the window last got a picture */
static uint64_t s_refreshNs;   /* the display's refresh period; 0 when it does not say */
static unsigned s_presents, s_pictures; /* for BAK_TRACE */

uint8_t *bak_video_page(uint16_t pageSeg) {
    if (pageSeg < 0xA000) {
        char what[48];
        snprintf(what, sizeof what, "video page at segment %04X", pageSeg);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    return &g_bakVram[(uint32_t)(pageSeg - 0xA000u) * 64u];
}

static SDL_ScaleMode scale_mode(void) {
    switch (settings_get_choice(SETTING_VIDEO_SCALING)) {
    case SCALING_NEAREST:
        return SDL_SCALEMODE_NEAREST;
    case SCALING_LINEAR:
        return SDL_SCALEMODE_LINEAR;
    default:
        return SDL_SCALEMODE_PIXELART;
    }
}

/* A texture and logical size for a w x h picture: at 4:3 the logical size is w x 3w/4, the
 * picture stretched vertically; with square pixels it is w x h. */
static bool set_picture_size(int w, int h) {
    SDL_DestroyTexture(s_texture);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, w, h);
    if (s_texture == NULL) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_SetTextureScaleMode(s_texture, scale_mode())) /* a renderer without pixel art */
        SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    s_width = w;
    s_height = h;
    s_shownHeight = settings_get_choice(SETTING_VIDEO_ASPECT) == ASPECT_SQUARE ? h : w * 3 / 4;
    SDL_SetRenderLogicalPresentation(s_renderer, w, s_shownHeight,
                                     settings_get_bool(SETTING_VIDEO_INTEGER)
                                         ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                         : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return true;
}

/* Whether a window there would show on a display: a good part of its top on one. */
static bool on_a_display(int x, int y, int w) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    bool seen = false;
    for (int i = 0; i < count && !seen; i++) {
        SDL_Rect bounds, top = {x, y, w, 32}, both;
        seen = SDL_GetDisplayUsableBounds(displays[i], &bounds) &&
               SDL_GetRectIntersection(&top, &bounds, &both) && both.w >= 100 && both.h >= 16;
    }
    SDL_free(displays);
    return seen;
}

/* Where the window is when it is neither in the full screen nor maximized. */
static void note_windowed(void) {
    SDL_WindowFlags flags = SDL_GetWindowFlags(s_window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
        return;
    SDL_GetWindowSize(s_window, &s_windowed.w, &s_windowed.h);
    s_placed = SDL_GetWindowPosition(s_window, &s_windowed.x, &s_windowed.y);
}

/* The window as it is, for the next run (not under the dummy driver: there is none). */
static void save_window(void) {
    if (s_offscreen)
        return;
    note_windowed();
    settings_hold();
    settings_set_int(SETTING_WINDOW_WIDTH, s_windowed.w);
    settings_set_int(SETTING_WINDOW_HEIGHT, s_windowed.h);
    if (s_placed) {
        settings_set_int(SETTING_WINDOW_X, s_windowed.x);
        settings_set_int(SETTING_WINDOW_Y, s_windowed.y);
    }
    settings_set_bool(SETTING_WINDOW_MAXIMIZED,
                      (SDL_GetWindowFlags(s_window) & SDL_WINDOW_MAXIMIZED) != 0);
    settings_release();
}

void video_toggle_fullscreen(void) {
    bool full = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) == 0;
    note_windowed();
    if (SDL_SetWindowFullscreen(s_window, full))
        settings_set_bool(SETTING_VIDEO_FULLSCREEN, full);
}

bool video_handle_key(const SDL_Event *e) {
    SDL_Scancode sc = e->key.scancode;
    if (sc >= SDL_SCANCODE_COUNT)
        return false;
    if (e->type == SDL_EVENT_KEY_UP) {
        bool eaten = s_keyEaten[sc];
        s_keyEaten[sc] = false;
        return eaten;
    }
    bool toggle = sc == SDL_SCANCODE_F11 ||
                  ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) &&
                   (e->key.mod & SDL_KMOD_ALT) != 0);
    if (!toggle)
        return false;
    s_keyEaten[sc] = true;
    if (!e->key.repeat)
        video_toggle_fullscreen();
    return true;
}

/* The refresh period of the display the window is on. */
static void note_display(void) {
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
    s_refreshNs = mode != NULL && mode->refresh_rate > 0.0f
                      ? (uint64_t)(1e9 / (double)mode->refresh_rate)
                      : 0;
}

void video_handle_window_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
        note_windowed();
        break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        note_display();
        break;
    default:
        break;
    }
    s_stale = true; /* uncovered, resized or moved: draw it again */
}

bool video_open(void) {
    s_windowed = (SDL_Rect){0, 0, 960, 720};
    settings_get_int(SETTING_WINDOW_WIDTH, &s_windowed.w);
    settings_get_int(SETTING_WINDOW_HEIGHT, &s_windowed.h);
    s_placed = settings_get_int(SETTING_WINDOW_X, &s_windowed.x) &&
               settings_get_int(SETTING_WINDOW_Y, &s_windowed.y) &&
               on_a_display(s_windowed.x, s_windowed.y, s_windowed.w);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "BetterAtKrondor");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, s_windowed.w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, s_windowed.h);
    if (s_placed) {
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, s_windowed.x);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, s_windowed.y);
    }
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN,
                           settings_get_bool(SETTING_WINDOW_MAXIMIZED));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
                           settings_get_bool(SETTING_VIDEO_FULLSCREEN));
    s_window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
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
    SDL_HideCursor(); /* until the game shows its own (cursor.c) */
    const char *driver = SDL_GetCurrentVideoDriver();
    s_offscreen = driver != NULL && SDL_strcmp(driver, "dummy") == 0;
    note_display();
    if (SDL_getenv("BAK_TRACE") != NULL) {
        SDL_ScaleMode mode = SDL_SCALEMODE_INVALID;
        SDL_GetTextureScaleMode(s_texture, &mode);
        fprintf(stderr, "trace: video driver %s, renderer %s, scale mode %d (asked %d)\n",
                driver != NULL ? driver : "?", SDL_GetRendererName(s_renderer), (int)mode,
                (int)scale_mode());
    }
    return true;
}

bool video_window_to_picture(float wx, float wy, float *px, float *py) {
    float lx, ly;
    if (s_renderer == NULL || !SDL_RenderCoordinatesFromWindow(s_renderer, wx, wy, &lx, &ly))
        return false;
    /* The logical height back to the picture's (at 4:3, 3w/4 stretched from h). */
    *px = lx;
    *py = ly * (float)s_height / (float)s_shownHeight;
    return true;
}

void video_close(void) {
    if (s_window != NULL)
        save_window();
    if (SDL_getenv("BAK_TRACE") != NULL)
        fprintf(stderr, "trace: %u pictures, %u of them presented\n", s_pictures, s_presents);
    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    s_texture = NULL;
    s_renderer = NULL;
    s_window = NULL;
}

/*
 * Headless checks, each written when the picture shown differs from the one before it:
 *
 *   BAK_DUMP_FRAME=<file.bmp>  the picture, so the file ends up holding the last one shown;
 *   BAK_DUMP_FRAMES=<dir>      every picture, numbered from 00001.bmp, as the frame log's lines;
 *   BAK_FRAME_LOG=<file>       a line per picture: the clock's time in ms, the size and the
 *                              hash of the pixels. On the virtual clock (BAK_CLOCK=virtual) a
 *                              scripted run writes the same log every time, so two builds can be
 *                              compared frame by frame.
 */
static void save_bmp(const char *path) {
    SDL_Surface *s =
        SDL_CreateSurfaceFrom(s_width, s_height, SDL_PIXELFORMAT_XRGB8888, s_picture, s_width * 4);
    if (s != NULL) {
        SDL_SaveBMP(s, path);
        SDL_DestroySurface(s);
    }
}

/* A picture that differs from the one before it. */
static void record_frame(void) {
    static const char *dumpPath, *dumpDir;
    static FILE *log;
    static bool checked;
    static unsigned count;
    if (!checked) {
        checked = true;
        dumpPath = SDL_getenv("BAK_DUMP_FRAME");
        dumpDir = SDL_getenv("BAK_DUMP_FRAMES");
        const char *logPath = SDL_getenv("BAK_FRAME_LOG");
        if (logPath != NULL && (log = fopen(logPath, "w")) == NULL) {
            fprintf(stderr, "BAK_FRAME_LOG: cannot write %s\n", logPath);
            exit(2);
        }
    }
    if (dumpPath == NULL && dumpDir == NULL && log == NULL)
        return;
    size_t bytes = (size_t)s_width * (size_t)s_height * sizeof s_picture[0];
    uint32_t hash = platform_hash(0, s_picture, bytes);
    count++;
    if (dumpPath != NULL)
        save_bmp(dumpPath);
    if (dumpDir != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%05u.bmp", dumpDir, count);
        save_bmp(path);
    }
    if (log != NULL) {
        uint64_t ns = clock_now_ns();
        fprintf(log, "%" PRIu64 ".%03" PRIu64 " %dx%d %08" PRIx32 "\n", ns / 1000000,
                ns / 1000 % 1000, s_width, s_height, hash);
        fflush(log);
    }
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
uint8_t video_attribute_color(unsigned nibble) {
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
                row[x + b] = pal[video_attribute_color(nibble) & g_vga.pelMask];
            }
        }
    }
}

void video_flush(void) {
    if (!s_stale || s_offscreen || s_texture == NULL || s_lastW == 0)
        return;
    uint64_t now = SDL_GetTicksNS();
    if (s_presentedNs != 0 && now - s_presentedNs < s_refreshNs)
        return; /* the display has not shown the last one yet */
    s_presentedNs = now;
    s_stale = false;
    s_presents++;
    SDL_UpdateTexture(s_texture, NULL, s_last, s_lastW * 4);
    SDL_RenderClear(s_renderer);
    SDL_FRect dst = {0, 0, (float)s_lastW, (float)s_shownHeight};
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
}

void video_warp_pointer(float px, float py) {
    float wx, wy;
    if (s_offscreen || s_renderer == NULL || !cursor_shown() ||
        (SDL_GetMouseFocus() != s_window && SDL_GetKeyboardFocus() != s_window))
        return;
    /* The middle of the pixel, so the pointer maps back to it. */
    if (SDL_RenderCoordinatesToWindow(s_renderer, px + 0.5f,
                                      (py + 0.5f) * (float)s_shownHeight / (float)s_height, &wx,
                                      &wy))
        SDL_WarpMouseInWindow(s_window, wx, wy);
}

/* The system cursor at the picture's scale: window pixels per picture pixel. */
static void update_cursor(void) {
    SDL_FRect r;
    if (!s_offscreen && SDL_GetRenderLogicalPresentationRect(s_renderer, &r) && r.w > 0.0f)
        cursor_update(r.w / (float)s_width, r.h / (float)s_height);
}

/* Show the picture the CRTC is scanning out. */
void video_present(void) {
    if (s_texture == NULL)
        return;
    update_cursor();
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = g_vga.dac[i][0], g = g_vga.dac[i][1], b = g_vga.dac[i][2];
        /* 6-bit DAC components to 8 bits. */
        pal[i] = (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
    }
    scan_out(pal);
    size_t bytes = (size_t)s_width * (size_t)s_height * sizeof s_picture[0];
    if (s_width != s_lastW || s_height != s_lastH || memcmp(s_last, s_picture, bytes) != 0) {
        memcpy(s_last, s_picture, bytes);
        s_lastW = s_width;
        s_lastH = s_height;
        s_stale = true;
        s_pictures++;
        record_frame();
    }
    video_flush();
}
