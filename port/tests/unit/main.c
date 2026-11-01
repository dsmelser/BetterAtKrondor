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
