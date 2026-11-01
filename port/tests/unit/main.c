/*
 * bak_unittest: unit tests of the port's own code. They need no game data, so CI runs them.
 * The tests are listed in unit.h and run in that order; a failed check is reported with its
 * place and the run goes on.
 */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "unit.h"

static const char *s_test;
static int s_failedChecks;

void unit_fail(const char *file, int line, const char *check) {
    fprintf(stderr, "%s:%d: %s: check failed: %s\n", file, line, s_test, check);
    s_failedChecks++;
}

static SDL_EnumerationResult SDLCALL remove_entry(void *userdata, const char *dirname,
                                                  const char *fname) {
    (void)userdata;
    char *path = NULL;
    if (SDL_asprintf(&path, "%s%s", dirname, fname) < 0)
        return SDL_ENUM_FAILURE;
    unit_remove_tree(path);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

void unit_remove_tree(const char *path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info))
        return;
    if (info.type == SDL_PATHTYPE_DIRECTORY)
        SDL_EnumerateDirectory(path, remove_entry, NULL);
    SDL_RemovePath(path);
}

/* Under unit-tmp beside the runner; a run that crashed may have left one behind. The name
 * lives until the process ends. */
const char *unit_temp_dir(const char *name) {
    char *path = NULL;
    if (SDL_asprintf(&path, "%sunit-tmp/%s", SDL_GetBasePath(), name) < 0)
        return NULL;
    for (char *p = path; *p; p++)
        if (*p == '\\')
            *p = '/';
    unit_remove_tree(path);
    if (!SDL_CreateDirectory(path))
        fprintf(stderr, "cannot create %s: %s\n", path, SDL_GetError());
    return path;
}

int main(void) {
    /* The platform pumps host events while it waits. */
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    static const struct {
        const char *name;
        void (*run)(void);
    } tests[] = {
#define UNIT_ENTRY(name) {#name, unit_##name},
        UNIT_TESTS(UNIT_ENTRY)
#undef UNIT_ENTRY
    };
    int failedTests = 0;
    for (size_t i = 0; i < SDL_arraysize(tests); i++) {
        int before = s_failedChecks;
        s_test = tests[i].name;
        tests[i].run();
        if (s_failedChecks != before)
            failedTests++;
    }
    printf("%d tests, %d failed\n", (int)SDL_arraysize(tests), failedTests);
    SDL_Quit();
    return failedTests != 0;
}
