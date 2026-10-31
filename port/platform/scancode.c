/*
 * Host keys as the PC keyboard sends them: scan code set 1, a make code on press (and
 * on each auto-repeat), the make code with bit 7 set on release, and an E0h prefix byte
 * for the keys the 101-key keyboard added. Each byte is one IRQ 1.
 */
#include <SDL3/SDL.h>

#include "bak_port.h"
#include "platform.h"

#define EXT 0x100 /* sent after an E0h prefix */

static uint16_t xt_code(SDL_Scancode sc) {
    static const uint16_t letters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                         0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                         0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    static const uint16_t digits[10] = {0x02, 0x03, 0x04, 0x05, 0x06,
                                        0x07, 0x08, 0x09, 0x0A, 0x0B}; /* 1..9, 0 */
    static const uint16_t fkeys[12] = {0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
                                       0x41, 0x42, 0x43, 0x44, 0x57, 0x58};
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return letters[sc - SDL_SCANCODE_A];
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
        return digits[sc - SDL_SCANCODE_1];
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return fkeys[sc - SDL_SCANCODE_F1];
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:
        return 0x01;
    case SDL_SCANCODE_MINUS:
        return 0x0C;
    case SDL_SCANCODE_EQUALS:
        return 0x0D;
    case SDL_SCANCODE_BACKSPACE:
        return 0x0E;
    case SDL_SCANCODE_TAB:
        return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:
        return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET:
        return 0x1B;
    case SDL_SCANCODE_RETURN:
        return 0x1C;
    case SDL_SCANCODE_LCTRL:
        return 0x1D;
    case SDL_SCANCODE_SEMICOLON:
        return 0x27;
    case SDL_SCANCODE_APOSTROPHE:
        return 0x28;
    case SDL_SCANCODE_GRAVE:
        return 0x29;
    case SDL_SCANCODE_LSHIFT:
        return 0x2A;
    case SDL_SCANCODE_BACKSLASH:
        return 0x2B;
    case SDL_SCANCODE_COMMA:
        return 0x33;
    case SDL_SCANCODE_PERIOD:
        return 0x34;
    case SDL_SCANCODE_SLASH:
        return 0x35;
    case SDL_SCANCODE_RSHIFT:
        return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:
        return 0x37;
    case SDL_SCANCODE_LALT:
        return 0x38;
    case SDL_SCANCODE_SPACE:
        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:
        return 0x3A;
    case SDL_SCANCODE_NUMLOCKCLEAR:
        return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:
        return 0x46;
    case SDL_SCANCODE_KP_7:
        return 0x47;
    case SDL_SCANCODE_KP_8:
        return 0x48;
    case SDL_SCANCODE_KP_9:
        return 0x49;
    case SDL_SCANCODE_KP_MINUS:
        return 0x4A;
    case SDL_SCANCODE_KP_4:
        return 0x4B;
    case SDL_SCANCODE_KP_5:
        return 0x4C;
    case SDL_SCANCODE_KP_6:
        return 0x4D;
    case SDL_SCANCODE_KP_PLUS:
        return 0x4E;
    case SDL_SCANCODE_KP_1:
        return 0x4F;
    case SDL_SCANCODE_KP_2:
        return 0x50;
    case SDL_SCANCODE_KP_3:
        return 0x51;
    case SDL_SCANCODE_KP_0:
        return 0x52;
    case SDL_SCANCODE_KP_PERIOD:
        return 0x53;
    case SDL_SCANCODE_NONUSBACKSLASH:
        return 0x56;
    case SDL_SCANCODE_KP_ENTER:
        return EXT | 0x1C;
    case SDL_SCANCODE_RCTRL:
        return EXT | 0x1D;
    case SDL_SCANCODE_KP_DIVIDE:
        return EXT | 0x35;
    case SDL_SCANCODE_RALT:
        return EXT | 0x38;
    case SDL_SCANCODE_HOME:
        return EXT | 0x47;
    case SDL_SCANCODE_UP:
        return EXT | 0x48;
    case SDL_SCANCODE_PAGEUP:
        return EXT | 0x49;
    case SDL_SCANCODE_LEFT:
        return EXT | 0x4B;
    case SDL_SCANCODE_RIGHT:
        return EXT | 0x4D;
    case SDL_SCANCODE_END:
        return EXT | 0x4F;
    case SDL_SCANCODE_DOWN:
        return EXT | 0x50;
    case SDL_SCANCODE_PAGEDOWN:
        return EXT | 0x51;
    case SDL_SCANCODE_INSERT:
        return EXT | 0x52;
    case SDL_SCANCODE_DELETE:
        return EXT | 0x53;
    default:
        return 0;
    }
}

void keyboard_handle_event(const SDL_Event *e) {
    uint16_t code = xt_code(e->key.scancode);
    if (code == 0)
        return;
    if (code & EXT)
        bak_keyboard_irq(0xE0);
    bak_keyboard_irq((uint8_t)((code & 0x7F) | (e->type == SDL_EVENT_KEY_UP ? 0x80 : 0)));
}
