/* The keyboard through the bindings (input.c): what reaches the game's IRQ 1. */
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/platform.h"
#include "bak_port.h"

#define MS 1000000ull

static uint8_t s_bytes[64];
static int s_count;

/* The game's handler: reads the byte the keyboard sent. */
static void record_byte(void) {
    if (s_count < (int)sizeof s_bytes)
        s_bytes[s_count++] = bak_port_in8(0x60);
}

static void start(int16_t page) {
    input_reset();
    settings_load_text("", 0);
    clock_use_virtual(1000 * MS);
    bak_set_vector(0x09, record_byte);
    bak_input_page_polled(page);
    s_count = 0;
}

/* No handler, no keys held, the settings' defaults. */
static void finish(void) {
    bak_set_vector(0x09, NULL);
    input_reset();
    settings_load_text("", 0);
}

static void key(SDL_Scancode sc, bool down, bool repeat) {
    SDL_Event e = {0};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.repeat = repeat;
    input_handle_key(&e);
}

static bool sent(const uint8_t *bytes, int n) {
    bool same = s_count == n && memcmp(s_bytes, bytes, (size_t)n) == 0;
    s_count = 0;
    return same;
}

/* The game's keyboard buffer: a key waiting in it, or none. */
static void buffer_holds_a_key(bool holds) {
    unsigned char *bda = bak_bios_data();
    bda[0x1A] = 0x1E;
    bda[0x1B] = 0;
    bda[0x1C] = holds ? 0x20 : 0x1E;
    bda[0x1D] = 0;
}

void unit_input_letters_stay_letters_off_the_world(void) {
    start(BAK_PAGE_OTHER);
    key(SDL_SCANCODE_W, true, false);
    key(SDL_SCANCODE_W, true, true); /* the host's repeat, as typing wants */
    key(SDL_SCANCODE_W, false, false);
    UNIT_CHECK(sent((const uint8_t[]){0x11, 0x11, 0x91}, 3));
    finish();
}

void unit_input_wasd_moves_the_party(void) {
    start(BAK_PAGE_WORLD);
    buffer_holds_a_key(false);
    key(SDL_SCANCODE_W, true, false);
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x48}, 2)); /* Up */
    key(SDL_SCANCODE_W, true, true);                    /* the host's repeat: left out */
    UNIT_CHECK(sent(NULL, 0));
    /* Held: a repeat after 250 ms, then every 100 ms, while the game has taken the last. */
    clock_spend(249 * MS);
    input_poll();
    UNIT_CHECK(sent(NULL, 0));
    clock_spend(1 * MS);
    input_poll();
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x48}, 2));
    clock_spend(99 * MS);
    input_poll();
    UNIT_CHECK(sent(NULL, 0));
    clock_spend(1 * MS);
    buffer_holds_a_key(true); /* not taken yet: no more */
    input_poll();
    UNIT_CHECK(sent(NULL, 0));
    buffer_holds_a_key(false);
    input_poll();
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x48}, 2));
    /* Left the world while it was held: the release is still Up's, and nothing repeats. */
    bak_input_page_polled(BAK_PAGE_OTHER);
    clock_spend(500 * MS);
    input_poll();
    key(SDL_SCANCODE_W, false, false);
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0xC8}, 2));
    /* The others, on the map too. */
    start(BAK_PAGE_MAP);
    key(SDL_SCANCODE_A, true, false);
    key(SDL_SCANCODE_S, true, false);
    key(SDL_SCANCODE_D, true, false);
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x4B, 0xE0, 0x50, 0xE0, 0x4D}, 6));
    key(SDL_SCANCODE_E, true, false); /* not bound */
    UNIT_CHECK(sent((const uint8_t[]){0x12}, 1));
    finish();
}

void unit_input_arrows_repeat_on_the_clock(void) {
    start(BAK_PAGE_WORLD);
    buffer_holds_a_key(false);
    key(SDL_SCANCODE_LEFT, true, false);
    key(SDL_SCANCODE_LEFT, true, true);
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x4B}, 2));
    clock_spend(250 * MS);
    input_poll();
    key(SDL_SCANCODE_LEFT, false, false);
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x4B, 0xE0, 0xCB}, 4));
    finish();
}

void unit_input_bindings_come_from_the_settings(void) {
    start(BAK_PAGE_WORLD);
    char why[100];
    UNIT_CHECK(settings_override("controls.forward=Keypad 8", why, sizeof why));
    UNIT_CHECK(settings_override("controls.back=", why, sizeof why));
    UNIT_CHECK(settings_override("controls.repeat_delay=0", why, sizeof why));
    UNIT_CHECK(settings_override("controls.repeat_rate=20", why, sizeof why));
    UNIT_CHECK(!settings_override("controls.left=Nothing", why, sizeof why));
    key(SDL_SCANCODE_KP_8, true, false);
    key(SDL_SCANCODE_W, true, false);
    key(SDL_SCANCODE_S, true, false);
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x48, 0x11, 0x1F}, 4));
    buffer_holds_a_key(false);
    clock_spend(50 * MS); /* no delay, 20 a second */
    input_poll();
    UNIT_CHECK(sent((const uint8_t[]){0xE0, 0x48}, 2));
    finish();
}

/* The world's and the map's arrow buttons held with the mouse step as a held key repeats. */
void unit_input_held_buttons_step_at_the_keys_rate(void) {
    start(BAK_PAGE_WORLD);
    bak_input_hold_start();
    UNIT_CHECK(!bak_input_hold_step());
    clock_spend(249 * MS);
    UNIT_CHECK(!bak_input_hold_step());
    clock_spend(1 * MS);
    UNIT_CHECK(bak_input_hold_step());
    UNIT_CHECK(!bak_input_hold_step()); /* just stepped */
    clock_spend(99 * MS);
    UNIT_CHECK(!bak_input_hold_step());
    clock_spend(1 * MS);
    UNIT_CHECK(bak_input_hold_step());
    finish();
}
