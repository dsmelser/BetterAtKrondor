/*
 * Process entry point. The platform layer owns main(); the game's own main()
 * (GAME/GMAIN.C, renamed bak_main by bak_native.h) runs on top of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "bak_port.h"
#include "platform.h"

void bak_main(int16_t argc, char **argv);

static void usage(void) {
    fprintf(stderr,
            "usage: betterkrondor [--data DIR] [--user DIR] [--set SECTION.KEY=VALUE]...\n"
            "                     [game options]\n"
            "  --data DIR   the folder with your Betrayal at Krondor files, remembered for the\n"
            "               next runs (default: the one remembered, else the current folder or\n"
            "               the program's, else a folder dialog)\n"
            "  --user DIR   the folder for saves and settings (default: the per-user folder,\n"
            "               %%APPDATA%%\\BetterAtKrondor on Windows)\n"
            "  --set S.K=V  a setting for this run only; betterkrondor.ini in the user folder\n"
            "               keeps them. The settings:\n");
    settings_print_help();
}

int main(int argc, char *argv[]) {
    /* Port options are consumed here; the rest (e.g. the game's own -S<n>) go to the game. */
    char **gameArgv = calloc((size_t)argc + 1, sizeof *gameArgv);
    const char **sets = calloc((size_t)argc, sizeof *sets);
    int gameArgc = 0;
    PlatformOptions options = {NULL, NULL, sets, 0};
    gameArgv[gameArgc++] = argv[0];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            options.dataDir = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            options.userDir = argv[++i];
        } else if (strcmp(argv[i], "--set") == 0 && i + 1 < argc) {
            sets[options.setCount++] = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            gameArgv[gameArgc++] = argv[i];
        }
    }

    int trace = SDL_getenv("BAK_TRACE") != NULL;
    if (trace)
        fprintf(stderr, "trace: platform_init\n");
    if (!platform_init(&options))
        return 1;
    if (trace)
        fprintf(stderr, "trace: entering the game (bak_main)\n");
    bak_main((int16_t)gameArgc, gameArgv);
    platform_shutdown();
    free(sets);
    free(gameArgv);
    return 0;
}
