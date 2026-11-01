/* unit.h - unit tests of the port's own code (bak_unittest), which need no game data. */
#ifndef BAK_UNIT_H
#define BAK_UNIT_H

/* Every test, in the order they run: X(name) for a function void unit_<name>(void). */
#define UNIT_TESTS(X)                                                                              \
    X(clock_wall_by_default)                                                                       \
    X(clock_virtual_waits_jump_to_the_due_time)                                                    \
    X(clock_retrace_wait_runs_the_ticks_due)                                                       \
    X(hash_covers_every_byte_and_chains)                                                           \
    X(rand_fx_is_the_game_generator_on_its_own_ring)

#define UNIT_DECLARE(name) void unit_##name(void);
UNIT_TESTS(UNIT_DECLARE)
#undef UNIT_DECLARE

void unit_fail(const char *file, int line, const char *check);
/* Report a check that fails; the test goes on. */
#define UNIT_CHECK(cond) ((cond) ? (void)0 : unit_fail(__FILE__, __LINE__, #cond))

#endif
