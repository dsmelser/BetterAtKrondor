/* betterkrondor.ini (settings.c): the known keys read, everything else kept, changes in place. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/platform.h"
#include "bak_port.h"

#ifdef _WIN32
#define EOL "\r\n" /* a new file's line breaks */
#else
#define EOL "\n"
#endif

static void load(const char *text) {
    settings_load_text(text, strlen(text));
}

static bool data_is(const char *value) {
    return strcmp(settings_get_string(SETTING_PATHS_DATA), value) == 0;
}

static bool text_is(const char *expected) {
    size_t size;
    char *text = settings_text(&size);
    bool same = size == strlen(expected) && memcmp(text, expected, size) == 0;
    if (!same)
        fprintf(stderr, "settings are:\n%s---\nexpected:\n%s---\n", text, expected);
    SDL_free(text);
    return same;
}

void unit_settings_keep_what_they_do_not_know(void) {
    const char *file = "\xEF\xBB\xBF; BetterAtKrondor\r\n"
                       "\r\n"
                       "[Paths]\r\n"
                       "  Data =  E:\\GOG\\Betrayal at Krondor  \r\n"
                       "saves = elsewhere\r\n"
                       "[future]\r\n"
                       "key = value ; not a comment\r\n"
                       "a line without an equals sign\r\n";
    load(file);
    UNIT_CHECK(data_is("E:\\GOG\\Betrayal at Krondor"));
    UNIT_CHECK(text_is(file));
    /* A file without a last line break gains one, nothing else. */
    load("[paths]\ndata = D");
    UNIT_CHECK(data_is("D"));
    UNIT_CHECK(text_is("[paths]\ndata = D\n"));
}

void unit_settings_default_to_their_defaults(void) {
    load("");
    UNIT_CHECK(data_is(""));
    UNIT_CHECK(text_is(""));
    /* A key outside its section is another key; a header needs its closing bracket. */
    load("data = X\n[other]\ndata = Y\n[paths\ndata = Z\n[paths)\ndata = Z\n");
    UNIT_CHECK(data_is(""));
}

void unit_settings_set_rewrites_its_own_line(void) {
    load("; top\n[paths]\nother = 1\ndata = old\n\n; video\n[video]\nx = 2\n");
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "new"));
    UNIT_CHECK(data_is("new"));
    UNIT_CHECK(text_is("; top\n[paths]\nother = 1\ndata = new\n\n; video\n[video]\nx = 2\n"));
    /* A key without a line goes after the last value of its section. */
    load("[paths]\nother = 1\n\n; video\n[video]\nx = 2\n");
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "D"));
    UNIT_CHECK(text_is("[paths]\nother = 1\ndata = D\n\n; video\n[video]\nx = 2\n"));
    /* Without its section, into a new one at the end. */
    load("[video]\nx = 2\n");
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "D"));
    UNIT_CHECK(text_is("[video]\nx = 2\n\n[paths]\ndata = D\n"));
    load("");
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "D"));
    UNIT_CHECK(text_is("[paths]" EOL "data = D" EOL));
    /* Of two lines the last wins, and it is the one rewritten. */
    load("[paths]\ndata = A\ndata = B\n");
    UNIT_CHECK(data_is("B"));
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "C"));
    UNIT_CHECK(text_is("[paths]\ndata = A\ndata = C\n"));
    /* A value is one line. */
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "x\r\ny"));
    UNIT_CHECK(data_is("x"));
    UNIT_CHECK(text_is("[paths]\ndata = A\ndata = x\n"));
}

void unit_settings_override_for_the_run_only(void) {
    load("[paths]\ndata = A\n");
    char why[200];
    UNIT_CHECK(settings_override("paths.data=B", why, sizeof why));
    UNIT_CHECK(data_is("B"));
    UNIT_CHECK(text_is("[paths]\ndata = A\n"));
    UNIT_CHECK(settings_override(" PATHS.Data = C:\\x=y ", why, sizeof why));
    UNIT_CHECK(data_is("C:\\x=y"));
    UNIT_CHECK(!settings_override("paths.dat=B", why, sizeof why) &&
               strstr(why, "no such setting") != NULL);
    UNIT_CHECK(!settings_override("paths=B", why, sizeof why));
    UNIT_CHECK(!settings_override("paths.data", why, sizeof why));
    UNIT_CHECK(data_is("C:\\x=y"));
}

void unit_settings_are_written_back_whole(void) {
    const char *dir = unit_temp_dir("settings");
    char *path = NULL, *temp = NULL;
    SDL_asprintf(&path, "%s/betterkrondor.ini", dir);
    SDL_asprintf(&temp, "%s.new", path);
    UNIT_CHECK(settings_load(dir));
    UNIT_CHECK(data_is(""));
    UNIT_CHECK(!SDL_GetPathInfo(path, NULL)); /* reading writes nothing */
    UNIT_CHECK(settings_set_string(SETTING_PATHS_DATA, "E:\\Games\\Krondor"));
    UNIT_CHECK(settings_load(dir));
    UNIT_CHECK(data_is("E:\\Games\\Krondor"));
    char *text = SDL_LoadFile(path, NULL);
    UNIT_CHECK(text != NULL && strcmp(text, "[paths]" EOL "data = E:\\Games\\Krondor" EOL) == 0);
    UNIT_CHECK(!SDL_GetPathInfo(temp, NULL));
    SDL_free(text);
    SDL_free(temp);
    SDL_free(path);
    settings_load_text("", 0); /* nothing written into the folder from here on */
    unit_remove_tree(dir);
}

void unit_settings_read_their_types(void) {
    const char *file = "[video]\n"
                       "fullscreen = Yes\n"
                       "scaling = Linear\n"
                       "integer = 0\n"
                       "aspect = wide\n"
                       "[window]\n"
                       "width = 1280\n"
                       "height = 640x\n"
                       "x = -1920\n"
                       "y =\n";
    load(file);
    UNIT_CHECK(settings_get_bool(SETTING_VIDEO_FULLSCREEN));
    UNIT_CHECK(settings_get_choice(SETTING_VIDEO_SCALING) == SCALING_LINEAR);
    UNIT_CHECK(!settings_get_bool(SETTING_VIDEO_INTEGER));
    UNIT_CHECK(settings_get_choice(SETTING_VIDEO_ASPECT) == ASPECT_4_3); /* "wide": the default */
    int n = 0;
    UNIT_CHECK(settings_get_int(SETTING_WINDOW_WIDTH, &n) && n == 1280);
    UNIT_CHECK(settings_get_int(SETTING_WINDOW_HEIGHT, &n) && n == 720); /* "640x": the default */
    UNIT_CHECK(settings_get_int(SETTING_WINDOW_X, &n) && n == -1920);
    n = 7;
    UNIT_CHECK(!settings_get_int(SETTING_WINDOW_Y, &n) && n == 7); /* not set */
    /* The file keeps its spelling until the port changes a value. */
    UNIT_CHECK(text_is(file));
    UNIT_CHECK(settings_set_bool(SETTING_VIDEO_FULLSCREEN, false));
    UNIT_CHECK(settings_set_int(SETTING_WINDOW_Y, 40));
    UNIT_CHECK(settings_set_int(SETTING_WINDOW_WIDTH, 1280)); /* the value it has: no change */
    UNIT_CHECK(text_is("[video]\n"
                       "fullscreen = false\n"
                       "scaling = Linear\n"
                       "integer = 0\n"
                       "aspect = wide\n"
                       "[window]\n"
                       "width = 1280\n"
                       "height = 640x\n"
                       "x = -1920\n"
                       "y = 40\n"));
}

void unit_settings_refuse_what_their_types_do_not_take(void) {
    load("");
    char why[200];
    UNIT_CHECK(settings_override("video.fullscreen=on", why, sizeof why));
    UNIT_CHECK(settings_get_bool(SETTING_VIDEO_FULLSCREEN));
    UNIT_CHECK(!settings_override("video.fullscreen=2", why, sizeof why));
    UNIT_CHECK(strcmp(why, "not true or false") == 0);
    UNIT_CHECK(!settings_override("window.width=319", why, sizeof why));
    UNIT_CHECK(strcmp(why, "not a whole number from 320 to 16384") == 0);
    UNIT_CHECK(settings_override("window.width=16384", why, sizeof why));
    UNIT_CHECK(!settings_override("window.width=16385", why, sizeof why));
    UNIT_CHECK(!settings_override("window.width=", why, sizeof why)); /* not optional */
    UNIT_CHECK(!settings_override("window.width=12345678901234567890", why, sizeof why));
    UNIT_CHECK(settings_override("window.x=-32768", why, sizeof why));
    UNIT_CHECK(settings_override("window.x=", why, sizeof why)); /* optional: not set */
    int n = 3;
    UNIT_CHECK(!settings_get_int(SETTING_WINDOW_X, &n) && n == 3);
    UNIT_CHECK(!settings_override("video.scaling=bilinear", why, sizeof why));
    UNIT_CHECK(strcmp(why, "not one of pixelart, nearest, linear") == 0);
    UNIT_CHECK(settings_override("video.scaling=NEAREST", why, sizeof why));
    UNIT_CHECK(settings_get_choice(SETTING_VIDEO_SCALING) == SCALING_NEAREST);
    UNIT_CHECK(strcmp(settings_get_string(SETTING_VIDEO_SCALING), "nearest") == 0);
    UNIT_CHECK(text_is("")); /* overrides stay out of the file */
}

void unit_settings_held_are_written_once(void) {
    const char *dir = unit_temp_dir("settings-held");
    char *path = NULL;
    SDL_asprintf(&path, "%s/betterkrondor.ini", dir);
    UNIT_CHECK(settings_load(dir));
    settings_hold();
    UNIT_CHECK(settings_set_int(SETTING_WINDOW_WIDTH, 960)); /* the default: no change */
    UNIT_CHECK(settings_release());
    UNIT_CHECK(!SDL_GetPathInfo(path, NULL));
    settings_hold();
    UNIT_CHECK(settings_set_int(SETTING_WINDOW_WIDTH, 1024));
    UNIT_CHECK(settings_set_bool(SETTING_WINDOW_MAXIMIZED, true));
    UNIT_CHECK(!SDL_GetPathInfo(path, NULL)); /* not yet */
    UNIT_CHECK(settings_release());
    char *text = SDL_LoadFile(path, NULL);
    UNIT_CHECK(text != NULL &&
               strcmp(text, "[window]" EOL "width = 1024" EOL "maximized = true" EOL) == 0);
    SDL_free(text);
    SDL_free(path);
    settings_load_text("", 0);
    unit_remove_tree(dir);
}

/* Smooth steps last video.step_time, in frames of the VGA's 70 Hz (14.268 ms), rounded. */
void unit_settings_step_time_in_frames(void) {
    char why[200];
    UNIT_CHECK(settings_override("video.step_time=100", why, sizeof why));
    UNIT_CHECK(bak_smooth_frames() == 7);
    UNIT_CHECK(settings_override("video.step_time=50", why, sizeof why));
    UNIT_CHECK(bak_smooth_frames() == 4);
    UNIT_CHECK(settings_override("video.step_time=0", why, sizeof why));
    UNIT_CHECK(bak_smooth_frames() == 0);
    UNIT_CHECK(settings_override("video.step_time=1000", why, sizeof why));
    UNIT_CHECK(bak_smooth_frames() == 70);
    UNIT_CHECK(!settings_override("video.step_time=1001", why, sizeof why));
    settings_override("video.step_time=100", why, sizeof why);
}
