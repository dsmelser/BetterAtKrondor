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
    X(paths_unfinished_searches_are_dropped)                                                       \
    X(paths_rename_within_the_user_folder)                                                         \
    X(settings_keep_what_they_do_not_know)                                                         \
    X(settings_default_to_their_defaults)                                                          \
    X(settings_set_rewrites_its_own_line)                                                          \
    X(settings_override_for_the_run_only)                                                          \
    X(settings_are_written_back_whole)                                                             \
    X(settings_read_their_types)                                                                   \
    X(settings_refuse_what_their_types_do_not_take)                                                \
    X(settings_held_are_written_once)                                                              \
    X(settings_step_time_in_frames)                                                                \
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
    X(clock_ms_for_the_game)                                                                       \
    X(input_the_wheel_presses_its_keys)                                                            \
    X(input_the_gamepad_is_keys)                                                                   \
    X(input_the_gamepad_points)                                                                    \
    X(audio_events_land_on_their_frame)                                                            \
    X(audio_timer_events_take_their_ticks_time)                                                    \
    X(audio_volumes_scale_the_mix)                                                                 \
    X(audio_sources_at_other_rates_keep_time)                                                      \
    X(audio_sources_join_and_queue)                                                                \
    X(audio_epoch_follows_the_device_drift)                                                        \
    X(audio_dump_follows_the_clock)                                                                \
    X(audio_device_chooses_the_driver)                                                             \
    X(cdrom_reads_an_ogg_files_length)                                                             \
    X(cdrom_lays_out_the_disc_from_the_music)                                                      \
    X(cdrom_plays_a_stretch_of_a_track)                                                            \
    X(opl_ports_reach_the_sink)                                                                    \
    X(opl_notes_sound_at_their_moment)                                                             \
    X(sbdsp_resets_and_identifies)                                                                 \
    X(sbdsp_block_interrupt_comes_on_time)                                                         \
    X(sbdsp_block_reaches_the_mix)                                                                 \
    X(sbdsp_held_block_is_silent)                                                                  \
    X(sbdsp_hold_and_go_on)                                                                        \
    X(sbdsp_dma_wraps_within_its_page)                                                             \
    X(sbdsp_free_running_polls_end_blocks)                                                         \
    X(autosave_due_on_a_zone_and_the_clock)                                                        \
    X(autosave_holds_the_machine_still)                                                            \
    X(memory_plain_blocks_have_offsets)                                                            \
    X(memory_images_sit_in_plain_blocks)                                                           \
    X(memory_ems_is_plain_blocks)                                                                  \
    X(glpresent_letterboxes_the_picture)                                                           \
    X(glpresent_draws_the_picture)

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
