/*
 * The keyboard through the bindings. A host key goes to the game as the PC key it is
 * (scancode.c), except on the pages where the party moves, the world's and the map's (the game
 * reports the page it polls, bak_input_page_polled): there a key bound to a movement
 * (controls.forward, back, left and right: W, S, A and D) is that movement's arrow. What a
 * press sends is kept until its release, so the game sees the release of the key it saw pressed.
 *
 * There too a held movement key, an arrow or a bound one, repeats on the platform's clock: after
 * controls.repeat_delay ms, controls.repeat_rate times a second, and only once the game has
 * taken the press before from its keyboard buffer, so that steps never queue up. The host's own
 * repeats of it are left out: their rate is the system's setting, and on the virtual clock a
 * scripted run repeats the same way every time. Elsewhere keys repeat as the host repeats them,
 * as typing wants.
 */
#include <SDL3/SDL.h>

#include "bak_port.h"
#include "platform.h"

static const SettingId s_moveSettings[4] = {SETTING_CONTROLS_FORWARD, SETTING_CONTROLS_BACK,
                                            SETTING_CONTROLS_LEFT, SETTING_CONTROLS_RIGHT};
static const SDL_Scancode s_arrows[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
                                         SDL_SCANCODE_RIGHT};

typedef struct Held {
    bool down;
    SDL_Scancode sends; /* what its press sent */
    uint64_t sinceNs;   /* pressed then */
    uint64_t lastNs;    /* sent last then */
} Held;

static int16_t s_page; /* BAK_PAGE_* */
static Held s_held[SDL_SCANCODE_COUNT];

void bak_input_page_polled(int16_t kind) {
    s_page = kind;
}

static int16_t s_stick[SDL_GAMEPAD_AXIS_COUNT]; /* the gamepads' sticks, as they are */
static bool s_stickDir[4]; /* the left stick's up, down, left, right, as keys */

void input_reset(void) {
    SDL_memset(s_held, 0, sizeof s_held);
    SDL_memset(s_stick, 0, sizeof s_stick);
    SDL_memset(s_stickDir, 0, sizeof s_stickDir);
    s_page = BAK_PAGE_OTHER;
}

static bool party_moves(void) {
    return s_page == BAK_PAGE_WORLD || s_page == BAK_PAGE_MAP;
}

static bool is_movement(SDL_Scancode key) {
    for (int i = 0; i < 4; i++)
        if (s_arrows[i] == key)
            return true;
    return false;
}

/* What a press of key sends now. */
static SDL_Scancode sends_for(SDL_Scancode key) {
    if (!party_moves())
        return key;
    for (int i = 0; i < 4; i++) {
        const char *name = settings_get_string(s_moveSettings[i]);
        if (name[0] != '\0' && SDL_GetScancodeFromName(name) == key)
            return s_arrows[i];
    }
    return key;
}

void input_handle_key(const SDL_Event *e) {
    SDL_Scancode key = e->key.scancode;
    if ((unsigned)key >= SDL_SCANCODE_COUNT)
        return;
    Held *h = &s_held[key];
    if (e->type == SDL_EVENT_KEY_UP) {
        keyboard_send(h->down ? h->sends : key, false);
        h->down = false;
        return;
    }
    if (h->down && e->key.repeat) {
        if (party_moves() && is_movement(h->sends))
            return; /* it repeats on the clock */
        keyboard_send(h->sends, true);
        return;
    }
    h->down = true;
    h->sends = sends_for(key);
    h->sinceNs = h->lastNs = clock_now_ns();
    keyboard_send(h->sends, true);
}

/* Whether a hold that began at since, last acted on at last, is due again: after the delay, then
 * at the rate (controls.repeat_delay, repeat_rate). */
static bool repeat_due(uint64_t now, uint64_t since, uint64_t last) {
    int delayMs = 250, rate = 10;
    settings_get_int(SETTING_CONTROLS_REPEAT_DELAY, &delayMs);
    settings_get_int(SETTING_CONTROLS_REPEAT_RATE, &rate);
    return now - since >= (uint64_t)delayMs * 1000000u &&
           now - last >= 1000000000u / (uint64_t)rate;
}

static void right_stick(uint64_t now);

void input_poll(void) {
    uint64_t now = clock_now_ns();
    right_stick(now);
    if (!party_moves())
        return;
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++) {
        Held *h = &s_held[k];
        if (!h->down || !is_movement(h->sends) || !repeat_due(now, h->sinceNs, h->lastNs) ||
            !keyboard_buffer_empty())
            continue;
        keyboard_send(h->sends, true);
        h->lastNs = now;
    }
}

/* ---- the wheel ---------------------------------------------------------------------------- */

void input_handle_wheel(const SDL_Event *e) {
    int notches = e->wheel.integer_y;
    if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        notches = -notches;
    SettingId id = notches > 0 ? SETTING_CONTROLS_WHEEL_UP : SETTING_CONTROLS_WHEEL_DOWN;
    const char *name = settings_get_string(id);
    SDL_Scancode key = name[0] != '\0' ? SDL_GetScancodeFromName(name) : SDL_SCANCODE_UNKNOWN;
    for (int i = 0; i < SDL_abs(notches) && key != SDL_SCANCODE_UNKNOWN; i++) {
        keyboard_send(key, true);
        keyboard_send(key, false);
    }
}

/* ---- gamepads ----------------------------------------------------------------------------- */

#define STICK_ON 16384  /* a tilt this far is a direction pressed */
#define STICK_OFF 12288 /* and back under this one, let go */
#define POINTER_DEAD 6000

static const struct {
    SDL_GamepadButton button;
    SDL_Scancode key;
} s_padKeys[] = {
    {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_SCANCODE_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_SCANCODE_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_SCANCODE_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_SCANCODE_RIGHT},
    {SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_ESCAPE},
    {SDL_GAMEPAD_BUTTON_NORTH, SDL_SCANCODE_RETURN},
};

static float s_pointerX, s_pointerY; /* the right stick's motion not yet passed on */
static uint64_t s_pointerNs;

/* A key as the keyboard's would go: the bindings and the clock's repeats apply. */
static void pad_key(SDL_Scancode key, bool down) {
    SDL_Event k = {0};
    k.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    k.key.scancode = key;
    input_handle_key(&k);
}

/* The left stick as the arrows, with a margin so a tilt on the edge does not flicker. */
static void left_stick(void) {
    int x = s_stick[SDL_GAMEPAD_AXIS_LEFTX], y = s_stick[SDL_GAMEPAD_AXIS_LEFTY];
    const int along[4] = {-y, y, -x, x};
    for (int i = 0; i < 4; i++) {
        bool on = s_stickDir[i] ? along[i] > STICK_OFF : along[i] > STICK_ON;
        if (on != s_stickDir[i]) {
            s_stickDir[i] = on;
            pad_key(s_arrows[i], on);
        }
    }
}

void input_handle_gamepad(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        SDL_CloseGamepad(SDL_GetGamepadFromID(e->gdevice.which));
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        bool down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
            mouse_button(1, down);
        else if (e->gbutton.button == SDL_GAMEPAD_BUTTON_WEST)
            mouse_button(2, down);
        for (size_t i = 0; i < SDL_arraysize(s_padKeys); i++)
            if (s_padKeys[i].button == e->gbutton.button)
                pad_key(s_padKeys[i].key, down);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e->gaxis.axis < SDL_GAMEPAD_AXIS_COUNT)
            s_stick[e->gaxis.axis] = e->gaxis.value;
        if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
            left_stick();
        break;
    default:
        break;
    }
}

/* The right stick moves the pointer, faster the further it tilts. */
static void right_stick(uint64_t now) {
    float x = s_stick[SDL_GAMEPAD_AXIS_RIGHTX], y = s_stick[SDL_GAMEPAD_AXIS_RIGHTY];
    uint64_t since = s_pointerNs != 0 ? now - s_pointerNs : 0;
    s_pointerNs = now;
    if (SDL_fabsf(x) < POINTER_DEAD && SDL_fabsf(y) < POINTER_DEAD)
        return;
    int speed = 200;
    settings_get_int(SETTING_CONTROLS_POINTER_SPEED, &speed);
    float scale = (float)speed * (float)since / 1e9f / 32767.0f;
    s_pointerX += x * scale;
    s_pointerY += y * scale;
    if (SDL_fabsf(s_pointerX) >= 0.25f || SDL_fabsf(s_pointerY) >= 0.25f) {
        mouse_nudge(s_pointerX, s_pointerY);
        s_pointerX = s_pointerY = 0.0f;
    }
}

/* The world's and the map's arrow buttons held down: the original stepped on every pass of its
 * loop after a 90-tick wait, as fast as the machine drew the view; now at the keys' rate. */
static uint64_t s_holdSinceNs, s_holdLastNs;

void bak_input_hold_start(void) {
    s_holdSinceNs = s_holdLastNs = clock_now_ns();
}

_Bool bak_input_hold_step(void) {
    uint64_t now = clock_now_ns();
    if (!repeat_due(now, s_holdSinceNs, s_holdLastNs))
        return false;
    s_holdLastNs = now;
    return true;
}
