/* Platform lifetime, event pumping, the idle hook and the unimplemented trap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#endif

#include "bak_port.h"
#include "bak_rtl.h"
#include "platform.h"

static const char *s_dataDir = ".";
static const char *s_userDir = ".";

void platform_set_dirs(const char *dataDir, const char *userDir) {
    s_dataDir = dataDir;
    s_userDir = userDir;
}

const char *platform_data_dir(void) {
    return s_dataDir;
}

const char *platform_user_dir(void) {
    return s_userDir;
}

/* Errors go to stderr, and to a message box unless BAK_HEADLESS is set (CI, scripted runs). */
static void report_error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    if (SDL_getenv("BAK_HEADLESS") == NULL)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BetterAtKrondor", msg, NULL);
}

#ifdef _WIN32
/* The original code passes whatever fopen returned straight to fscanf/fread; with a game
 * file missing that is NULL, which the C runtime rejects here instead of misbehaving. */
static void invalid_parameter(const wchar_t *expr, const wchar_t *func, const wchar_t *file,
                              unsigned line, uintptr_t reserved) {
    (void)expr;
    (void)func;
    (void)file;
    (void)line;
    (void)reserved;
    report_error("The C runtime rejected a call from the game code (most likely a game "
                 "file is missing, e.g. drive.cfg or resource.cfg).");
    exit(4);
}

static void install_crt_handlers(void) {
    _set_invalid_parameter_handler(invalid_parameter);
    if (SDL_getenv("BAK_HEADLESS") != NULL) {
        /* No dialogs at all: debug-CRT reports and crash boxes go to stderr / nowhere. */
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    }
}
#endif

/* KRONDOR.RMF, the resource index, must be readable and every archive it names present:
 * u16 archive count, u16 hash seed, u16 hash rotation, then per archive a 13-byte
 * NUL-padded file name and the entries. Only the first archive's name is checked. */
static bool game_data_complete(char *problem, size_t size) {
    FILE *rmf = bakrtl_fopen("krondor.rmf", "rb");
    if (rmf == NULL) {
        snprintf(problem, size, "no KRONDOR.RMF");
        return false;
    }
    unsigned char head[6];
    char archive[14] = {0};
    bool ok = fread(head, sizeof head, 1, rmf) == 1 && (head[0] | head[1] << 8) > 0 &&
              fread(archive, 13, 1, rmf) == 1 && memchr(archive, 0, 13) != NULL;
    fclose(rmf);
    if (!ok) {
        snprintf(problem, size, "KRONDOR.RMF is not a resource index");
        return false;
    }
    FILE *payload = bakrtl_fopen(archive, "rb");
    if (payload == NULL) {
        snprintf(problem, size, "KRONDOR.RMF indexes %s, which is missing", archive);
        return false;
    }
    fclose(payload);
    return true;
}

bool platform_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
#ifdef _WIN32
    install_crt_handlers();
#endif
    crash_reporter_install();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    /* The game assumes it runs from its install folder and does not survive missing
     * resources (it uses what it failed to load); check before it trips over that. */
    char problem[256];
    if (!game_data_complete(problem, sizeof problem)) {
        char msg[1500];
        snprintf(msg, sizeof msg,
                 "Betrayal at Krondor's game files were not found in:\n%s\n(%s)\n\n"
                 "Run with --data <folder>, pointing at the folder that contains KRONDOR.RMF.",
                 platform_data_dir(), problem);
        report_error(msg);
        SDL_Quit();
        return false;
    }
    return video_open();
}

void platform_shutdown(void) {
    video_close();
    SDL_Quit();
}

bool platform_pump_events(void) {
    SDL_Event e;
    bool running = true;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            running = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            mouse_handle_event(&e);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            keyboard_handle_event(&e);
            break;
        default:
            break;
        }
    }
    return running;
}

_Noreturn void bak_unimplemented(const char *what, const char *file, int line) {
    char msg[512];
    snprintf(msg, sizeof msg, "Not implemented yet: %s\n(%s:%d)", what, file, line);
    report_error(msg);
    exit(3);
}
