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

void input_reset(void) {
    SDL_memset(s_held, 0, sizeof s_held);
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

void input_poll(void) {
    if (!party_moves())
        return;
    int delayMs = 250, rate = 10;
    settings_get_int(SETTING_CONTROLS_REPEAT_DELAY, &delayMs);
    settings_get_int(SETTING_CONTROLS_REPEAT_RATE, &rate);
    uint64_t now = clock_now_ns();
    uint64_t delay = (uint64_t)delayMs * 1000000u, every = 1000000000u / (uint64_t)rate;
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++) {
        Held *h = &s_held[k];
        if (!h->down || !is_movement(h->sends) || now - h->sinceNs < delay ||
            now - h->lastNs < every || !keyboard_buffer_empty())
            continue;
        keyboard_send(h->sends, true);
        h->lastNs = now;
    }
}
