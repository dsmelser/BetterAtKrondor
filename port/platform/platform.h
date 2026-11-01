/* platform.h - the SDL3 platform layer's internal interface. */
#ifndef BAK_PLATFORM_H
#define BAK_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Game data directory (the user's copy, only read) and the writable user directory (saves,
 * TEMP.GAM, KRONDOR.CFG, betterkrondor.ini). */
void platform_set_dirs(const char *dataDir, const char *userDir);
const char *platform_data_dir(void);
const char *platform_user_dir(void);

/* What the command line chose; NULL where it chose nothing. */
typedef struct PlatformOptions {
    const char *dataDir; /* --data */
    const char *userDir; /* --user */
    const char **sets;   /* each --set's "section.key=value", setCount of them */
    int setCount;
} PlatformOptions;

/* The folders and the settings, SDL, the game folder's check, the window. False after
 * reporting why the game cannot start (or the user declined to choose a game folder). */
bool platform_init(const PlatformOptions *options);
/* An error for the user: stderr, and a message box unless BAK_HEADLESS is set. */
void platform_report_error(const char *msg);

/*
 * betterkrondor.ini (settings.c): the port's settings, in the user folder. Each has a type and
 * a default; a changed one is written back at once, and lines the port does not know stay.
 */
typedef enum SettingId {
    SETTING_PATHS_DATA,       /* [paths] data: the game folder */
    SETTING_VIDEO_FULLSCREEN, /* [video] fullscreen */
    SETTING_VIDEO_SCALING,    /* [video] scaling: SCALING_* */
    SETTING_VIDEO_INTEGER,    /* [video] integer: whole multiples only */
    SETTING_VIDEO_ASPECT,     /* [video] aspect: ASPECT_* */
    SETTING_WINDOW_WIDTH,     /* [window] width, height, x, y (optional), maximized */
    SETTING_WINDOW_HEIGHT,
    SETTING_WINDOW_X,
    SETTING_WINDOW_Y,
    SETTING_WINDOW_MAXIMIZED,
    SETTING_COUNT
} SettingId;
enum { SCALING_PIXELART, SCALING_NEAREST, SCALING_LINEAR };
enum { ASPECT_4_3, ASPECT_SQUARE };
/* Read <dir>/betterkrondor.ini (none: the defaults), where changes then go. False, with a
 * message, when it exists but cannot be read; then nothing is written back. */
bool settings_load(const char *dir);
/* Settings from text, written back nowhere (the unit tests). */
void settings_load_text(const char *text, size_t size);
/* --set's "section.key=value": a value for this run only. False, with why, for an unknown key
 * or a bad value. */
bool settings_override(const char *assignment, char *why, size_t whySize);
const char *settings_get_string(SettingId id);
bool settings_get_bool(SettingId id);
/* False, leaving value, when an optional integer is not set. */
bool settings_get_int(SettingId id, int *value);
/* The choice's index (SCALING_*, ASPECT_*). */
int settings_get_choice(SettingId id);
/* A new value, written back at once unless it is the value already; false, with a message, if
 * writing failed. */
bool settings_set_string(SettingId id, const char *value);
bool settings_set_bool(SettingId id, bool value);
bool settings_set_int(SettingId id, int value);
/* Changes between these are written back once, at the release. */
void settings_hold(void);
bool settings_release(void);
/* The file as written (SDL_free it). */
char *settings_text(size_t *size);
/* The settings, their meaning and defaults, on stderr (--help). */
void settings_print_help(void);

/* The folders (folders.c). The default user folder: SDL's per-user one, made if missing
 * (SDL_free it), or NULL. */
char *folders_default_user_dir(void);
/* Whether dir holds the game's resources; if not, problem says what is missing. */
bool folders_game_data_complete(const char *dir, char *problem, size_t size);
/* The game folder (SDL_free it): dataArg if given, else the one remembered in the settings,
 * the current folder or the program's; else, if canAsk, the one the user picks in a folder
 * dialog. The folder found is remembered. NULL if none holds the game: report lists the folders
 * looked at, each with its problem, or is empty when the user gave up on the dialog. */
char *folders_choose_data_dir(const char *dataArg, bool canAsk, char *report, size_t reportSize);
/* Copy the game folder's GAMES (the DOS game's saves) into a user folder that has none, so they
 * load; false, with a message, if copying failed. */
bool folders_import_saves(const char *dataDir, const char *userDir);

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

/* The game window (video.c): made as the settings say, its place and size saved on closing. */
bool video_open(void);
void video_close(void);
void video_present(void);
/* A window position as a position on the 320x200 picture; false outside any window. */
bool video_window_to_picture(float wx, float wy, float *px, float *py);
/* To and from the full screen, remembered. */
void video_toggle_fullscreen(void);
/* A key for the window (Alt+Enter, F11, and their releases): true when it took it, so the
 * game does not see it. */
union SDL_Event;
bool video_handle_key(const union SDL_Event *e);
/* The window moved or was resized. */
void video_handle_window_event(const union SDL_Event *e);

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
/* Ask the game to move the party (teleport.c): its pending teleport, carried out by the world
 * loop's next pass. A cell is 1/40 of a tile; a heading's full turn is 10000h. */
void game_request_teleport(uint8_t zone, uint8_t tileX, uint8_t tileY, uint8_t cellX,
                           uint8_t cellY, uint16_t heading);

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
