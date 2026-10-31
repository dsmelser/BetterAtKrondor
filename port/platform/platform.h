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

/* Print crashes (exception + symbolized call stack) to stderr (crash_win32.c). */
void crash_reporter_install(void);
void platform_shutdown(void);

/* Pump host events; returns false once the user asked to quit. */
bool platform_pump_events(void);

/* The game window (video.c). */
bool video_open(void);
void video_close(void);
void video_present(void);

#endif
