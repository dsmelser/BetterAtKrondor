/* unit.h - unit tests of the port's own code (bak_unittest), which need no game data. */
#ifndef BAK_UNIT_H
#define BAK_UNIT_H

/* Every test, in the order they run: X(name) for a function void unit_<name>(void). */
#define UNIT_TESTS(X)                                                                              \
    X(clock_wall_by_default)                                                                       \
    X(clock_virtual_waits_jump_to_the_due_time)                                                    \
    X(clock_retrace_wait_runs_the_ticks_due)                                                       \
    X(hash_covers_every_byte_and_chains)                                                           \
    X(rand_fx_is_the_game_generator_on_its_own_ring)                                               \
    X(ai_sung_goes_to_an_ally_under_attack)                                                        \
    X(ai_heal_spell_follows_the_weakest_living_ally)                                               \
    X(paths_are_utf8_for_the_c_runtime)                                                            \
    X(paths_chdir_finds_folders_as_dos_does)                                                       \
    X(settings_keep_what_they_do_not_know)                                                         \
    X(settings_default_to_their_defaults)                                                          \
    X(settings_set_rewrites_its_own_line)                                                          \
    X(settings_override_for_the_run_only)                                                          \
    X(settings_are_written_back_whole)                                                             \
    X(settings_read_their_types)                                                                   \
    X(settings_refuse_what_their_types_do_not_take)                                                \
    X(settings_held_are_written_once)                                                              \
    X(folders_know_the_game_by_its_resources)                                                      \
    X(folders_remember_the_game_folder)                                                            \
    X(folders_copy_the_dos_saves_once)                                                             \
    X(cursor_reads_the_vga_drivers_images)                                                         \
    X(cursor_reads_the_16_colour_drivers_images)                                                   \
    X(cursor_goes_to_the_system_unless_set_otherwise)                                              \
    X(input_letters_stay_letters_off_the_world)                                                    \
    X(input_wasd_moves_the_party)                                                                  \
    X(input_arrows_repeat_on_the_clock)                                                            \
    X(input_bindings_come_from_the_settings)                                                       \
    X(input_held_buttons_step_at_the_keys_rate)                                                    \
    X(clock_ms_for_the_game)

#define UNIT_DECLARE(name) void unit_##name(void);
UNIT_TESTS(UNIT_DECLARE)
#undef UNIT_DECLARE

void unit_fail(const char *file, int line, const char *check);
/* Report a check that fails; the test goes on. */
#define UNIT_CHECK(cond) ((cond) ? (void)0 : unit_fail(__FILE__, __LINE__, #cond))

/* A new empty folder for a test, beside the test runner (forward slashes, no trailing one);
 * remove it with unit_remove_tree. */
const char *unit_temp_dir(const char *name);
void unit_remove_tree(const char *path);

#endif
