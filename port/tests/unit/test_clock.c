/* The platform clock, and the timer interrupts and retrace waits that follow it. */
#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/platform.h"
#include "bak_port.h"
#include "bak_video.h"

#define PIT_HZ 1193182ull
#define FRAME_NS 14268000ull /* mode 13h's refresh period (vgaregs.c) */

static unsigned s_ticks;

static void count_tick(void) {
    s_ticks++;
}

/* Channel 0, low byte then high byte, as the game's timer_install does. */
static void program_pit(uint16_t divisor) {
    machine_port_out8(0x43, 0x36);
    machine_port_out8(0x40, (uint8_t)divisor);
    machine_port_out8(0x40, (uint8_t)(divisor >> 8));
}

/* Runs first: the other tests switch the process to the virtual clock. */
void unit_clock_wall_by_default(void) {
    if (SDL_getenv("BAK_CLOCK") != NULL)
        return; /* the environment chose a clock */
    UNIT_CHECK(!clock_is_virtual());
    uint64_t before = clock_now_ns();
    clock_spend(1000000000ull); /* work done does not move the host's clock */
    uint64_t after = clock_now_ns();
    UNIT_CHECK(after >= before && after - before < 1000000000ull);
}

void unit_clock_virtual_waits_jump_to_the_due_time(void) {
    clock_use_virtual(5000000000ull);
    UNIT_CHECK(clock_is_virtual());
    UNIT_CHECK(clock_now_ns() == 5000000000ull);
    clock_spend(7);
    UNIT_CHECK(clock_now_ns() == 5000000007ull);
    /* Nothing is due before 5.0004 s: the wait ends there, whatever the host would sleep. */
    clock_idle(100000, 5000400000ull);
    UNIT_CHECK(clock_now_ns() == 5000400000ull);
    /* A due time already past: the wait costs a poll, so a loop of them moves on. */
    clock_idle(100000, 5000300000ull);
    UNIT_CHECK(clock_now_ns() == 5000400000ull + CLOCK_POLL_NS);
}

/* While the game waits for the retrace, every timer tick that falls due runs, at its time. */
void unit_clock_retrace_wait_runs_the_ticks_due(void) {
    clock_use_virtual(700 * FRAME_NS); /* on a frame boundary: the wait spans a whole frame */
    bak_set_vector(0x08, count_tick);
    program_pit(5041); /* the game's 236.7 Hz */
    uint64_t start = clock_now_ns();
    uint64_t tick = 5041ull * 1000000000ull / PIT_HZ;
    uint64_t boundary = start - start % FRAME_NS + FRAME_NS;
    s_ticks = 0;
    bak_vga_wait_retrace(1);
    uint64_t end = clock_now_ns();
    /* The wait ends at the frame boundary, give or take the last poll's cost. */
    UNIT_CHECK(end >= boundary && end <= boundary + CLOCK_POLL_NS);
    UNIT_CHECK(s_ticks == (end - start) / tick);
    UNIT_CHECK(s_ticks == 3);
}
