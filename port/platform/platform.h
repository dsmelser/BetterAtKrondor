/* platform.h - the SDL3 platform layer's internal interface. */
#ifndef BAK_PLATFORM_H
#define BAK_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* Game data directory (the user's copy) and the writable user directory (saves, TEMP.GAM). */
void platform_set_dirs(const char *dataDir, const char *userDir);
const char *platform_data_dir(void);
const char *platform_user_dir(void);

bool platform_init(int argc, char **argv);

/* Set by the test runner: no real-time waits and no presenting (vertical retrace is
 * always under way). */
extern bool g_platformFreeRunning;

/* Print crashes (exception + symbolized call stack) to stderr (crash_win32.c). */
void crash_reporter_install(void);
void platform_shutdown(void);

/* Pump host events; returns false once the user asked to quit. */
bool platform_pump_events(void);

/* The game window (video.c). */
bool video_open(void);
void video_close(void);
void video_present(void);
/* A window position as a position on the 320x200 picture; false outside any window. */
bool video_window_to_picture(float wx, float wy, float *px, float *py);

/* Feed a host mouse event to the emulated INT 33h driver (mouse.c), a key to the
 * keyboard (scancode.c). */
union SDL_Event;
void mouse_handle_event(const union SDL_Event *e);
void keyboard_handle_event(const union SDL_Event *e);

/* machine.c: the I/O ports of the timer, interrupt controller and keyboard controller
 * (false: not one of these), and a byte arriving from the keyboard (IRQ 1). */
bool machine_port_in8(uint16_t port, uint8_t *value);
bool machine_port_out8(uint16_t port, uint8_t value);
void bak_keyboard_irq(uint8_t scancode);

#endif
