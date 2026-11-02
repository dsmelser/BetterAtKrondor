/*
 * BAK_INPUT: input from a script, for runs with nobody at the keyboard (headless checks,
 * CI). Steps are separated by ';', each "<ms>:<action>" with ms counted from start-up:
 *
 *   key:<name>     press a key (an SDL key name: Escape, Return, Space, A, F1, Up, ...)
 *                  and release it 100 ms later; key:<name>,<ms> holds it for ms
 *   move:<x>,<y>   move the pointer to pixel (x, y) of the 320x200 picture
 *   click:<x>,<y>  move there and press the left button, releasing it 100 ms later
 *   hold:<x>,<y>,<ms>  the same, held for ms
 *   wheel:<n>      turn the mouse wheel n notches (away from you when positive)
 *   pad:<button>   press a gamepad button (SDL's name: a, b, dpup, dpleft, start, ...) and
 *                  release it 100 ms later; pad:<button>,<ms> holds it for ms
 *   teleport:<zone>,<tile x>,<tile y>,<cell x>,<cell y>,<heading in hex>[,<scene kind>,<scene index>]
 *                  move the party there, as the game's own teleports do (teleport.c); with a
 *                  scene, enter that town scene first (GDS<kind><index as a letter>.DAT), and
 *                  with zone 255 enter the scene only, the party staying where it is
 *   quit           what closing the window does
 *
 * For example BAK_INPUT="20000:key:Escape;30000:click:160,100". The steps run in time
 * order, each once its time has come; a malformed one stops the run with a message.
 */
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "platform.h"

#define MAX_STEPS 4096
#define RELEASE_NS (100 * 1000000ull)

typedef enum {
    STEP_KEY_DOWN,
    STEP_KEY_UP,
    STEP_MOVE,
    STEP_LEFT_DOWN,
    STEP_LEFT_UP,
    STEP_TELEPORT,
    STEP_WHEEL,
    STEP_PAD_DOWN,
    STEP_PAD_UP,
    STEP_QUIT
} Kind;

typedef struct {
    uint64_t atNs;
    Kind kind;
    SDL_Scancode key;
    uint64_t holdNs; /* a press: released this long after */
    float x, y;
    unsigned place[5]; /* teleport: zone, tile x and y, cell x and y */
    unsigned heading;
    unsigned scene[2]; /* teleport: the town scene's kind and index, or 0 */
    int notches;               /* wheel */
    SDL_GamepadButton button; /* pad */
} Step;

static Step s_steps[MAX_STEPS];
static int s_count, s_next;
static bool s_loaded;
static uint64_t s_startNs;

static _Noreturn void bad(const char *what, const char *step) {
    fprintf(stderr, "BAK_INPUT: %s \"%s\"\n", what, step);
    exit(2);
}

/* Steps are kept in time order; a release goes in after the steps due before it. */
static void add(Step s, const char *text) {
    if (s_count == MAX_STEPS) {
        fprintf(stderr, "BAK_INPUT: more than %d steps and releases, at \"%s\"\n", MAX_STEPS, text);
        exit(2);
    }
    int at = s_count;
    while (at > s_next && s_steps[at - 1].atNs > s.atNs)
        at--;
    SDL_memmove(&s_steps[at + 1], &s_steps[at], (size_t)(s_count - at) * sizeof s);
    s_steps[at] = s;
    s_count++;
}

static void parse(const char *text) {
    char *copy = SDL_strdup(text);
    char *save = NULL;
    for (char *step = SDL_strtok_r(copy, ";", &save); step != NULL;
         step = SDL_strtok_r(NULL, ";", &save)) {
        char *rest;
        unsigned long ms = SDL_strtoul(step, &rest, 10);
        if (rest == step || *rest != ':')
            bad("cannot read the step", step);
        rest++;
        Step s = {s_startNs + ms * 1000000ull};
        s.holdNs = RELEASE_NS;
        float x, y;
        unsigned long held;
        unsigned *p = s.place;
        if (SDL_strncmp(rest, "key:", 4) == 0) {
            char name[40];
            const char *comma = SDL_strchr(rest + 4, ',');
            size_t n = comma != NULL ? (size_t)(comma - (rest + 4)) : SDL_strlen(rest + 4);
            if (n >= sizeof name)
                bad("cannot read the step", step);
            SDL_memcpy(name, rest + 4, n);
            name[n] = '\0';
            s.key = SDL_GetScancodeFromName(name);
            if (s.key == SDL_SCANCODE_UNKNOWN)
                bad("cannot read the step", step);
            if (comma != NULL) {
                char *end;
                held = SDL_strtoul(comma + 1, &end, 10);
                if (end == comma + 1 || *end != '\0')
                    bad("cannot read the step", step);
                s.holdNs = held * 1000000ull;
            }
            s.kind = STEP_KEY_DOWN;
        } else if (SDL_sscanf(rest, "wheel:%d", &s.notches) == 1) {
            s.kind = STEP_WHEEL;
        } else if (SDL_strncmp(rest, "pad:", 4) == 0) {
            char name[40];
            const char *comma = SDL_strchr(rest + 4, ',');
            size_t n = comma != NULL ? (size_t)(comma - (rest + 4)) : SDL_strlen(rest + 4);
            if (n >= sizeof name)
                bad("cannot read the step", step);
            SDL_memcpy(name, rest + 4, n);
            name[n] = '\0';
            s.button = SDL_GetGamepadButtonFromString(name);
            if (s.button == SDL_GAMEPAD_BUTTON_INVALID)
                bad("cannot read the step", step);
            if (comma != NULL) {
                char *end;
                held = SDL_strtoul(comma + 1, &end, 10);
                if (end == comma + 1 || *end != '\0')
                    bad("cannot read the step", step);
                s.holdNs = held * 1000000ull;
            }
            s.kind = STEP_PAD_DOWN;
        } else if (SDL_sscanf(rest, "hold:%f,%f,%lu", &x, &y, &held) == 3) {
            s.kind = STEP_LEFT_DOWN;
            s.x = x;
            s.y = y;
            s.holdNs = held * 1000000ull;
        } else if (SDL_sscanf(rest, "move:%f,%f", &x, &y) == 2) {
            s.kind = STEP_MOVE;
            s.x = x;
            s.y = y;
        } else if (SDL_sscanf(rest, "click:%f,%f", &x, &y) == 2) {
            s.kind = STEP_LEFT_DOWN;
            s.x = x;
            s.y = y;
        } else if (SDL_strncmp(rest, "teleport:", 9) == 0) {
            int n = SDL_sscanf(rest, "teleport:%u,%u,%u,%u,%u,%x,%u,%u", &p[0], &p[1], &p[2],
                               &p[3], &p[4], &s.heading, &s.scene[0], &s.scene[1]);
            if (n != 6 && n != 8)
                bad("cannot read the step", step);
            if (p[0] == 0 || p[0] > 0xff || (p[0] == 0xff && n == 6) || p[1] > 0xff ||
                p[2] > 0xff || p[3] >= 40 || p[4] >= 40 || s.heading > 0xffff)
                bad("a place out of range in the step", step);
            if (n == 8 && (s.scene[0] == 0 || s.scene[0] > 0xff || s.scene[1] == 0 ||
                           s.scene[1] > 26))
                bad("a scene out of range in the step", step);
            s.kind = STEP_TELEPORT;
        } else if (SDL_strcmp(rest, "quit") == 0) {
            s.kind = STEP_QUIT;
        } else {
            bad("cannot read the step", step);
        }
        add(s, step);
    }
    SDL_free(copy);
}

bool input_script_poll(void) {
    if (!s_loaded) {
        s_loaded = true;
        s_startNs = clock_now_ns();
        const char *text = SDL_getenv("BAK_INPUT");
        if (text != NULL)
            parse(text);
    }
    uint64_t now = clock_now_ns();
    while (s_next < s_count && s_steps[s_next].atNs <= now) {
        Step s = s_steps[s_next++];
        SDL_Event e = {0};
        switch (s.kind) {
        case STEP_KEY_DOWN:
        case STEP_KEY_UP:
            e.type = s.kind == STEP_KEY_DOWN ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.scancode = s.key;
            input_handle_key(&e);
            if (s.kind == STEP_KEY_DOWN) {
                s.atNs = now + s.holdNs;
                s.kind = STEP_KEY_UP;
                add(s, "key release");
            }
            break;
        case STEP_MOVE:
            mouse_script_move(s.x, s.y);
            break;
        case STEP_LEFT_DOWN:
            mouse_script_move(s.x, s.y);
            mouse_script_left(true);
            s.atNs = now + s.holdNs;
            s.kind = STEP_LEFT_UP;
            add(s, "click release");
            break;
        case STEP_LEFT_UP:
            mouse_script_left(false);
            break;
        case STEP_TELEPORT:
            game_request_teleport((uint8_t)s.place[0], (uint8_t)s.place[1], (uint8_t)s.place[2],
                                  (uint8_t)s.place[3], (uint8_t)s.place[4], (uint16_t)s.heading,
                                  (uint16_t)s.scene[0], (uint16_t)s.scene[1]);
            break;
        case STEP_WHEEL:
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.integer_y = s.notches;
            e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            input_handle_wheel(&e);
            break;
        case STEP_PAD_DOWN:
        case STEP_PAD_UP:
            e.type = s.kind == STEP_PAD_DOWN ? SDL_EVENT_GAMEPAD_BUTTON_DOWN
                                             : SDL_EVENT_GAMEPAD_BUTTON_UP;
            e.gbutton.button = (Uint8)s.button;
            input_handle_gamepad(&e);
            if (s.kind == STEP_PAD_DOWN) {
                s.atNs = now + s.holdNs;
                s.kind = STEP_PAD_UP;
                add(s, "pad release");
            }
            break;
        case STEP_QUIT:
            return false;
        }
    }
    return true;
}
