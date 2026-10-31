/*
 * The INT 33h mouse driver, emulated over SDL.
 *
 * The game talks to a DOS mouse driver: it sets the cursor's coordinate ranges (in its
 * own units, four per pixel), and the driver calls the game's event handler with the
 * position and buttons whenever the mouse moves or a button changes. Here the host
 * pointer's position over the picture is mapped into those ranges, and the handler is
 * called from the event pump: at the points where the game waits, as the interrupt
 * would have arrived while it did.
 */
#include <SDL3/SDL.h>

#include "bak_port.h"
#include "platform.h"

#define EVENT_MOVED 0x01
#define EVENT_LEFT_DOWN 0x02
#define EVENT_LEFT_UP 0x04
#define EVENT_RIGHT_DOWN 0x08
#define EVENT_RIGHT_UP 0x10

static struct {
    int16_t x, y;
    int16_t xmin, xmax, ymin, ymax;
    uint16_t buttons; /* bit 0 left, bit 1 right */
    uint16_t eventMask;
    BakMouseHandler *handler;
    int cursorLevel; /* the driver's own cursor shows at 0; the game always keeps it hidden */
} s_mouse;

void (*g_int33Trace)(uint16_t ax, uint16_t cx, uint16_t dx);

static void trace(uint16_t ax, uint16_t cx, uint16_t dx) {
    if (g_int33Trace != NULL)
        g_int33Trace(ax, cx, dx);
}

static int16_t clamp(int16_t v, int16_t lo, int16_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Mode 13h defaults: a 640x200 virtual screen, cursor centred and hidden, no handler. */
uint16_t bak_int33_reset(void) {
    trace(0x00, 0, 0);
    s_mouse.xmin = 0;
    s_mouse.xmax = 639;
    s_mouse.ymin = 0;
    s_mouse.ymax = 199;
    s_mouse.x = 320;
    s_mouse.y = 100;
    s_mouse.cursorLevel = -1;
    s_mouse.eventMask = 0;
    s_mouse.handler = NULL;
    return 0xFFFF; /* driver present */
}

void bak_int33_show_cursor(void) {
    trace(0x01, 0, 0);
    if (s_mouse.cursorLevel < 0)
        s_mouse.cursorLevel++;
}

void bak_int33_hide_cursor(void) {
    trace(0x02, 0, 0);
    s_mouse.cursorLevel--;
}

void bak_int33_set_position(int16_t x, int16_t y) {
    trace(0x04, (uint16_t)x, (uint16_t)y);
    s_mouse.x = clamp(x, s_mouse.xmin, s_mouse.xmax);
    s_mouse.y = clamp(y, s_mouse.ymin, s_mouse.ymax);
}

void bak_int33_set_x_range(int16_t min, int16_t max) {
    trace(0x07, (uint16_t)min, (uint16_t)max);
    s_mouse.xmin = min < max ? min : max;
    s_mouse.xmax = min < max ? max : min;
    s_mouse.x = clamp(s_mouse.x, s_mouse.xmin, s_mouse.xmax);
}

void bak_int33_set_y_range(int16_t min, int16_t max) {
    trace(0x08, (uint16_t)min, (uint16_t)max);
    s_mouse.ymin = min < max ? min : max;
    s_mouse.ymax = min < max ? max : min;
    s_mouse.y = clamp(s_mouse.y, s_mouse.ymin, s_mouse.ymax);
}

void bak_int33_set_handler(uint16_t eventMask, BakMouseHandler *handler) {
    trace(0x0C, eventMask, handler != NULL);
    s_mouse.eventMask = eventMask;
    s_mouse.handler = handler;
}

/* Mickey/pixel ratio: motion is absolute here, so it has nothing to scale. */
void bak_int33_set_ratio(int16_t horizontal, int16_t vertical) {
    trace(0x0F, (uint16_t)horizontal, (uint16_t)vertical);
}

static void dispatch(uint16_t events) {
    if (s_mouse.handler != NULL && (events & s_mouse.eventMask) != 0)
        s_mouse.handler((uint16_t)s_mouse.x, (uint16_t)s_mouse.y, s_mouse.buttons);
}

/* The host pointer over the 320x200 picture, as a position in the driver's ranges: the
 * game sets them to four units per pixel. */
static void move_to(float px, float py) {
    int16_t x = (int16_t)SDL_clamp((int)(px * 4.0f), 0, 0x7FFF);
    int16_t y = (int16_t)SDL_clamp((int)(py * 4.0f), 0, 0x7FFF);
    x = clamp(x, s_mouse.xmin, s_mouse.xmax);
    y = clamp(y, s_mouse.ymin, s_mouse.ymax);
    if (x != s_mouse.x || y != s_mouse.y) {
        s_mouse.x = x;
        s_mouse.y = y;
        dispatch(EVENT_MOVED);
    }
}

void mouse_handle_event(const SDL_Event *e) {
    float px, py;
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (video_window_to_picture(e->motion.x, e->motion.y, &px, &py))
            move_to(px, py);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        uint16_t bit, event;
        if (e->button.button == SDL_BUTTON_LEFT) {
            bit = 1;
            event = down ? EVENT_LEFT_DOWN : EVENT_LEFT_UP;
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            bit = 2;
            event = down ? EVENT_RIGHT_DOWN : EVENT_RIGHT_UP;
        } else {
            break;
        }
        if (video_window_to_picture(e->button.x, e->button.y, &px, &py))
            move_to(px, py);
        s_mouse.buttons =
            down ? (uint16_t)(s_mouse.buttons | bit) : (uint16_t)(s_mouse.buttons & ~bit);
        dispatch(event);
        break;
    }
    default:
        break;
    }
}
