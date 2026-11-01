/* platform.h - the SDL3 platform layer's internal interface. */
#ifndef BAK_PLATFORM_H
#define BAK_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Game data directory (the user's copy) and the writable user directory (saves, TEMP.GAM). */
void platform_set_dirs(const char *dataDir, const char *userDir);
const char *platform_data_dir(void);
const char *platform_user_dir(void);

bool platform_init(int argc, char **argv);

/* Set by the test runner: no real-time waits and no presenting (vertical retrace is
 * always under way). */
extern bool g_platformFreeRunning;

/*
 * The clock everything time-driven follows (clock.c): the host's, or with BAK_CLOCK=virtual
 * one that moves only as the game runs, so scripted runs repeat exactly.
 */
#define CLOCK_POLL_NS 1000ull /* what a poll costs on the virtual clock */
bool clock_is_virtual(void);
/* Switch to the virtual clock at nowNs (the unit tests). */
void clock_use_virtual(uint64_t nowNs);
uint64_t clock_now_ns(void);
/* Account for work done: moves the virtual clock, nothing on the host's. */
void clock_spend(uint64_t ns);
/* Wait: the host's clock sleeps wallNs; the virtual one jumps to dueNs, the next moment
 * something is due (or by a poll's cost if that has passed). */
void clock_idle(uint64_t wallNs, uint64_t dueNs);

/* A fast hash of a buffer for the replay logs (hash.c); chain calls through hash. */
uint32_t platform_hash(uint32_t hash, const void *data, size_t bytes);

/* Print crashes (exception + symbolized call stack) to stderr (crash_win32.c). */
void crash_reporter_install(void);
/* Print the caller's call stack, symbolized, to stderr (nothing elsewhere than Windows). */
void crash_print_stack(void);
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
/* Scripted pointer input: a position on the 320x200 picture, the left button. */
void mouse_script_move(float px, float py);
void mouse_script_left(bool down);

/* Run the BAK_INPUT steps that are due (inputscript.c); false once one asks to quit. */
bool input_script_poll(void);

/* machine.c: the I/O ports of the timer, interrupt controller and keyboard controller
 * (false: not one of these), and a byte arriving from the keyboard (IRQ 1). */
bool machine_port_in8(uint16_t port, uint8_t *value);
bool machine_port_out8(uint16_t port, uint8_t value);
void bak_keyboard_irq(uint8_t scancode);
/* The next moment a poll has work: the timer's next tick or the next look at host events. */
uint64_t machine_next_due_ns(void);

/* mpu401.c: the MPU-401's ports (false: not 330h or 331h). */
bool mpu401_port_in8(uint16_t port, uint8_t *value);
bool mpu401_port_out8(uint16_t port, uint8_t value);

#endif
