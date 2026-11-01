/* Platform lifetime, event pumping, the idle hook and the unimplemented trap. */
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#endif

#include "bak_port.h"
#include "platform.h"

static const char *s_dataDir = ".";
static const char *s_userDir = ".";
static char *s_chosenData, *s_chosenUser; /* by platform_init, for the run */

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
void platform_report_error(const char *msg) {
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
    platform_report_error("The C runtime rejected a call from the game code (most likely a "
                          "game file is missing, e.g. drive.cfg or resource.cfg).");
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

bool platform_init(const PlatformOptions *options) {
#ifdef _WIN32
    install_crt_handlers();
#endif
    crash_reporter_install();
    /* The user folder first: the settings live there. */
    s_chosenUser =
        options->userDir != NULL ? SDL_strdup(options->userDir) : folders_default_user_dir();
    if (s_chosenUser == NULL || !SDL_CreateDirectory(s_chosenUser)) {
        char msg[1500];
        snprintf(msg, sizeof msg,
                 "There is no folder for the saves and settings (%s: %s).\n\n"
                 "Run with --user <folder>.",
                 s_chosenUser != NULL ? s_chosenUser : "the per-user folder", SDL_GetError());
        platform_report_error(msg);
        return false;
    }
    settings_load(s_chosenUser);
    for (int i = 0; i < options->setCount; i++) {
        char why[200], msg[600];
        if (!settings_override(options->sets[i], why, sizeof why)) {
            snprintf(msg, sizeof msg, "--set %s: %s", options->sets[i], why);
            platform_report_error(msg);
            return false;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    /* The game assumes it runs from its install folder and does not survive missing
     * resources (it uses what it failed to load); check before it trips over that. */
    char report[3000];
    s_chosenData = folders_choose_data_dir(options->dataDir, SDL_getenv("BAK_HEADLESS") == NULL,
                                           report, sizeof report);
    if (s_chosenData == NULL) {
        if (report[0] != '\0') {
            char msg[sizeof report + 200];
            snprintf(msg, sizeof msg,
                     "Betrayal at Krondor's game files were not found in:\n%s\n\n"
                     "Run with --data <folder>, pointing at the folder that contains KRONDOR.RMF.",
                     report);
            platform_report_error(msg);
        }
        SDL_Quit();
        return false;
    }
    platform_set_dirs(s_chosenData, s_chosenUser);
    folders_import_saves(s_chosenData, s_chosenUser);
    return video_open();
}

void platform_shutdown(void) {
    video_close();
    SDL_Quit();
}

bool platform_pump_events(void) {
    SDL_Event e;
    bool running = input_script_poll();
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
    fprintf(stderr, "%s\n", msg);
    crash_print_stack();
    if (SDL_getenv("BAK_HEADLESS") == NULL)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "BetterAtKrondor", msg, NULL);
    exit(3);
}
