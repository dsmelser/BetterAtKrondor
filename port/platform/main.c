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
            "usage: betterkrondor [--data DIR] [--user DIR] [game options]\n"
            "  --data DIR   folder with your Betrayal at Krondor files (default: .)\n"
            "  --user DIR   folder for saves and temporary files (default: the data folder)\n");
}

int main(int argc, char *argv[]) {
    /* Port options are consumed here; the rest (e.g. the game's own -S<n>) go to the game. */
    char **gameArgv = calloc((size_t)argc + 1, sizeof *gameArgv);
    int gameArgc = 0;
    const char *dataDir = ".";
    const char *userDir = NULL;
    gameArgv[gameArgc++] = argv[0];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            userDir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            gameArgv[gameArgc++] = argv[i];
        }
    }
    platform_set_dirs(dataDir, userDir != NULL ? userDir : dataDir);

    int trace = SDL_getenv("BAK_TRACE") != NULL;
    if (trace)
        fprintf(stderr, "trace: platform_init\n");
    if (!platform_init(gameArgc, gameArgv))
        return 1;
    if (trace)
        fprintf(stderr, "trace: entering the game (bak_main)\n");
    bak_main((int16_t)gameArgc, gameArgv);
    platform_shutdown();
    free(gameArgv);
    return 0;
}
