/* The game's folders (folders.c): which one holds the game, and the DOS saves copied once. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/platform.h"

static char *path_in(const char *dir, const char *name) {
    char *path = NULL;
    SDL_asprintf(&path, "%s/%s", dir, name);
    return path;
}

static void write_file(const char *dir, const char *name, const char *text) {
    char *path = path_in(dir, name);
    UNIT_CHECK(SDL_SaveFile(path, text, strlen(text)));
    SDL_free(path);
}

static bool file_is(const char *dir, const char *name, const char *text) {
    char *path = path_in(dir, name);
    size_t size = 0;
    char *data = SDL_LoadFile(path, &size);
    bool same = data != NULL && size == strlen(text) && memcmp(data, text, size) == 0;
    SDL_free(data);
    SDL_free(path);
    return same;
}

static bool exists(const char *dir, const char *name) {
    char *path = path_in(dir, name);
    bool found = SDL_GetPathInfo(path, NULL);
    SDL_free(path);
    return found;
}

/* A KRONDOR.RMF that names an archive: the count, the hash's seed and rotation, the name. */
static void write_rmf(const char *dir, unsigned char count, const char *archive) {
    unsigned char rmf[6 + 13] = {count, 0, 0x34, 0x12, 5, 0};
    memcpy(rmf + 6, archive, strlen(archive));
    char *path = path_in(dir, "KRONDOR.RMF");
    UNIT_CHECK(SDL_SaveFile(path, rmf, sizeof rmf));
    SDL_free(path);
}

void unit_folders_know_the_game_by_its_resources(void) {
    const char *dir = unit_temp_dir("folders-game");
    char problem[256];
    UNIT_CHECK(!folders_game_data_complete(dir, problem, sizeof problem));
    UNIT_CHECK(strcmp(problem, "no KRONDOR.RMF") == 0);
    write_file(dir, "KRONDOR.RMF", "");
    UNIT_CHECK(!folders_game_data_complete(dir, problem, sizeof problem));
    UNIT_CHECK(strcmp(problem, "KRONDOR.RMF is not a resource index") == 0);
    write_rmf(dir, 1, "KRONDOR.00013"); /* 13 letters: no room for the name's end */
    UNIT_CHECK(!folders_game_data_complete(dir, problem, sizeof problem));
    UNIT_CHECK(strcmp(problem, "KRONDOR.RMF is not a resource index") == 0);
    write_rmf(dir, 0, "KRONDOR.001"); /* no archives */
    UNIT_CHECK(!folders_game_data_complete(dir, problem, sizeof problem));
    UNIT_CHECK(strcmp(problem, "KRONDOR.RMF is not a resource index") == 0);
    write_rmf(dir, 1, "KRONDOR.001");
    UNIT_CHECK(!folders_game_data_complete(dir, problem, sizeof problem));
    UNIT_CHECK(strcmp(problem, "KRONDOR.RMF indexes KRONDOR.001, which is missing") == 0);
    write_file(dir, "krondor.001", "x"); /* names in any case */
    UNIT_CHECK(folders_game_data_complete(dir, problem, sizeof problem));
    unit_remove_tree(dir);
}

void unit_folders_remember_the_game_folder(void) {
    const char *root = unit_temp_dir("folders-choose");
    char *game = path_in(root, "game"), *other = path_in(root, "other");
    UNIT_CHECK(SDL_CreateDirectory(game) && SDL_CreateDirectory(other));
    write_rmf(game, 1, "KRONDOR.001");
    write_file(game, "KRONDOR.001", "x");
    char report[2000];
    settings_load_text("", 0);
    /* The folder asked for, remembered as an absolute path. */
    char *chosen = folders_choose_data_dir(game, false, report, sizeof report);
    UNIT_CHECK(chosen != NULL && strcmp(chosen, settings_get_string(SETTING_PATHS_DATA)) == 0);
    UNIT_CHECK(chosen != NULL && strstr(chosen, "folders-choose") != NULL);
    UNIT_CHECK(chosen != NULL && folders_game_data_complete(chosen, report, sizeof report));
#ifdef _WIN32
    UNIT_CHECK(chosen != NULL && strchr(chosen, '/') == NULL); /* as Windows spells it */
#endif
    /* Then the one used when none is asked for. */
    char *again = folders_choose_data_dir(NULL, false, report, sizeof report);
    UNIT_CHECK(again != NULL && chosen != NULL && strcmp(again, chosen) == 0);
    /* A folder asked for without the game: none, and why; the remembered one stays. */
    UNIT_CHECK(folders_choose_data_dir(other, false, report, sizeof report) == NULL);
    UNIT_CHECK(strstr(report, other) != NULL && strstr(report, "(no KRONDOR.RMF)") != NULL);
    UNIT_CHECK(chosen != NULL && strcmp(settings_get_string(SETTING_PATHS_DATA), chosen) == 0);
    /* The remembered folder lost the game, and neither the current folder nor the program's
     * holds one (in the build tree): none, with each folder looked at in the report. */
    char *ini = NULL;
    SDL_asprintf(&ini, "[paths]\ndata = %s\n", other);
    settings_load_text(ini, strlen(ini));
    UNIT_CHECK(folders_choose_data_dir(NULL, false, report, sizeof report) == NULL);
    UNIT_CHECK(strncmp(report, other, strlen(other)) == 0);
    UNIT_CHECK(strchr(report, '\n') != NULL);
    SDL_free(ini);
    SDL_free(again);
    SDL_free(chosen);
    SDL_free(other);
    SDL_free(game);
    settings_load_text("", 0);
    unit_remove_tree(root);
}

void unit_folders_copy_the_dos_saves_once(void) {
    const char *root = unit_temp_dir("folders-saves");
    char *data = path_in(root, "data"), *user = path_in(root, "user");
    char *slot = path_in(data, "GAMES/SAVES.G01"), *other = path_in(data, "GAMES/OTHER.G02");
    UNIT_CHECK(SDL_CreateDirectory(slot) && SDL_CreateDirectory(other) &&
               SDL_CreateDirectory(user));
    write_file(slot, "SAVE01.GAM", "first");
    write_file(slot, "SAVE02.GAM", "second");
    write_file(other, "SAVE00.GAM", "bookmark");
    char *partial = path_in(user, "GAMES.new"); /* what a copy cut short left */
    UNIT_CHECK(SDL_CreateDirectory(partial));
    write_file(partial, "STRAY", "");
    UNIT_CHECK(folders_import_saves(data, user));
    UNIT_CHECK(!exists(user, "GAMES/STRAY"));
    UNIT_CHECK(file_is(user, "GAMES/SAVES.G01/SAVE01.GAM", "first"));
    UNIT_CHECK(file_is(user, "GAMES/SAVES.G01/SAVE02.GAM", "second"));
    UNIT_CHECK(file_is(user, "GAMES/OTHER.G02/SAVE00.GAM", "bookmark"));
    UNIT_CHECK(!exists(user, "GAMES.new"));
    UNIT_CHECK(file_is(slot, "SAVE01.GAM", "first")); /* the game folder as it was */
    /* Once: from then on the user folder's saves are its own. */
    write_file(slot, "SAVE01.GAM", "changed in DOS");
    char *copied = path_in(user, "GAMES/SAVES.G01/SAVE02.GAM");
    UNIT_CHECK(SDL_RemovePath(copied));
    UNIT_CHECK(folders_import_saves(data, user));
    UNIT_CHECK(file_is(user, "GAMES/SAVES.G01/SAVE01.GAM", "first"));
    UNIT_CHECK(!exists(user, "GAMES/SAVES.G01/SAVE02.GAM"));
    /* No saves to copy: nothing made. */
    char *bare = path_in(root, "bare"), *user2 = path_in(root, "user2");
    UNIT_CHECK(SDL_CreateDirectory(bare) && SDL_CreateDirectory(user2));
    UNIT_CHECK(folders_import_saves(bare, user2));
    UNIT_CHECK(!exists(user2, "GAMES"));
    /* One folder for both: nothing copied. */
    UNIT_CHECK(folders_import_saves(data, data));
    UNIT_CHECK(!exists(data, "GAMES.new"));
    SDL_free(user2);
    SDL_free(bare);
    SDL_free(copied);
    SDL_free(partial);
    SDL_free(other);
    SDL_free(slot);
    SDL_free(user);
    SDL_free(data);
    unit_remove_tree(root);
}
