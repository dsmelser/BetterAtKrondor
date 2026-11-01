/*
 * The platform's clock: the time the timer interrupt, the vertical retrace, the scripted input
 * and the frames shown all follow.
 *
 * By default it is the host's monotonic clock. With BAK_CLOCK=virtual it is a clock of its own
 * that moves only as the game runs: every poll costs CLOCK_POLL_NS, and where the game would
 * wait (an idle poll, the retrace) the clock jumps to the next moment anything is due instead
 * of sleeping. A scripted run (BAK_INPUT) then plays out the same way every time, as fast as
 * the host can compute it.
 */
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "platform.h"

static bool s_checked;
static bool s_virtual;
static uint64_t s_virtualNs;

bool clock_is_virtual(void) {
    if (!s_checked) {
        s_checked = true;
        const char *mode = SDL_getenv("BAK_CLOCK");
        if (mode != NULL && SDL_strcmp(mode, "virtual") == 0) {
            s_virtual = true;
        } else if (mode != NULL && SDL_strcmp(mode, "wall") != 0) {
            fprintf(stderr, "BAK_CLOCK: \"%s\" is neither \"wall\" nor \"virtual\"\n", mode);
            exit(2);
        }
    }
    return s_virtual;
}

void clock_use_virtual(uint64_t nowNs) {
    s_checked = true;
    s_virtual = true;
    s_virtualNs = nowNs;
}

uint64_t clock_now_ns(void) {
    return clock_is_virtual() ? s_virtualNs : SDL_GetTicksNS();
}

void clock_spend(uint64_t ns) {
    if (clock_is_virtual())
        s_virtualNs += ns;
}

void clock_idle(uint64_t wallNs, uint64_t dueNs) {
    if (!clock_is_virtual()) {
        SDL_DelayPrecise(wallNs);
        return;
    }
    /* Nothing can happen before dueNs, so the wait ends there; if that moment has already
     * passed, the wait still costs a poll's time, so a loop of them moves on. */
    s_virtualNs = dueNs > s_virtualNs ? dueNs : s_virtualNs + CLOCK_POLL_NS;
}
