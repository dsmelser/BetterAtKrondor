/* Host paths: the game's DOS paths reach the user's folders, whatever their names. */
#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "unit.h"

#include "../../platform/platform.h"
#include "bak_rtl.h"

/* SDL's paths are UTF-8 (the folder dialog's, the user's folders); the C runtime's are too. */
void unit_paths_are_utf8_for_the_c_runtime(void) {
#ifdef _WIN32
    UNIT_CHECK(GetACP() == CP_UTF8);
#endif
    const char *root = unit_temp_dir("paths-utf8");
    char user[1024];
    SDL_snprintf(user, sizeof user, "%s/us\xC3\xA9r", root);
    UNIT_CHECK(SDL_CreateDirectory(user));
    platform_set_dirs(root, user);
    FILE *f = bakrtl_fopen("C:\\TEMP.GAM", "wb");
    UNIT_CHECK(f != NULL);
    if (f != NULL) {
        fputc('x', f);
        fclose(f);
    }
    char file[1100];
    SDL_snprintf(file, sizeof file, "%s/TEMP.GAM", user);
    SDL_PathInfo info;
    UNIT_CHECK(SDL_GetPathInfo(file, &info) && info.size == 1);
    platform_set_dirs(".", ".");
    unit_remove_tree(root);
}

/* The save dialog makes GAMES when chdir says it is not there (MAINMENU.C). */
void unit_paths_chdir_finds_folders_as_dos_does(void) {
    const char *root = unit_temp_dir("paths-chdir");
    char data[1024], user[1024], dataGames[1100], userFile[1100];
    SDL_snprintf(data, sizeof data, "%s/data", root);
    SDL_snprintf(user, sizeof user, "%s/user", root);
    SDL_snprintf(dataGames, sizeof dataGames, "%s/GAMES", data);
    SDL_snprintf(userFile, sizeof userFile, "%s/FILE", user);
    UNIT_CHECK(SDL_CreateDirectory(data) && SDL_CreateDirectory(user));
    platform_set_dirs(data, user);
    UNIT_CHECK(bakrtl_chdir("GAMES") == -1);
    UNIT_CHECK(SDL_CreateDirectory(dataGames));
    UNIT_CHECK(bakrtl_chdir("GAMES") == 0); /* read through to the game folder */
    UNIT_CHECK(bakrtl_chdir("..") == 0);
    UNIT_CHECK(SDL_RemovePath(dataGames));
    UNIT_CHECK(bakrtl_chdir("GAMES") == -1);
    UNIT_CHECK(bakrtl_mkdir("GAMES") == 0); /* made in the user folder */
    UNIT_CHECK(bakrtl_chdir("games") == 0);
    UNIT_CHECK(SDL_SaveFile(userFile, "not a folder", 12));
    UNIT_CHECK(bakrtl_chdir("FILE") == -1);
    platform_set_dirs(".", ".");
    unit_remove_tree(root);
}

/* DOS keeps a search in its DTA, and the game leaves some unfinished (mainmenu_save_any_exists
 * stops at the first save): those must not use up the searches the port holds, or every save
 * list comes up empty after a few visits to the menus. */
void unit_paths_unfinished_searches_are_dropped(void) {
    const char *root = unit_temp_dir("paths-find");
    char file[1100];
    for (int i = 0; i < 2; i++) {
        SDL_snprintf(file, sizeof file, "%s/SAVE%02d.GAM", root, i);
        UNIT_CHECK(SDL_SaveFile(file, "x", 1));
    }
    platform_set_dirs(root, root);
    struct ffblk outer, inner;
    for (int i = 0; i < 20; i++)
        UNIT_CHECK(bakrtl_findfirst("SAVE??.GAM", &inner, 0) == 0); /* each left after one */
    /* Searches under way still step to their ends, a nested one too. */
    UNIT_CHECK(bakrtl_findfirst("SAVE??.GAM", &outer, 0) == 0);
    UNIT_CHECK(SDL_strcmp(outer.ff_name, "SAVE00.GAM") == 0);
    UNIT_CHECK(bakrtl_findfirst("*.GAM", &inner, 0) == 0);
    UNIT_CHECK(bakrtl_findnext(&inner) == 0);
    UNIT_CHECK(bakrtl_findnext(&inner) == -1);
    UNIT_CHECK(bakrtl_findnext(&outer) == 0);
    UNIT_CHECK(SDL_strcmp(outer.ff_name, "SAVE01.GAM") == 0);
    UNIT_CHECK(bakrtl_findnext(&outer) == -1);
    platform_set_dirs(".", ".");
    unit_remove_tree(root);
}

/* The autosaves move up a key at a time (MAINMENU.C): rename, within the user folder, never over
 * a file that is there. */
void unit_paths_rename_within_the_user_folder(void) {
    const char *root = unit_temp_dir("paths-rename");
    char games[1100], file[1100];
    SDL_snprintf(games, sizeof games, "%s/GAMES", root);
    UNIT_CHECK(SDL_CreateDirectory(games));
    SDL_snprintf(file, sizeof file, "%s/SAVE01.GAM", games);
    UNIT_CHECK(SDL_SaveFile(file, "1", 1));
    SDL_snprintf(file, sizeof file, "%s/SAVE02.GAM", games);
    UNIT_CHECK(SDL_SaveFile(file, "2", 1));
    platform_set_dirs(root, root);
    UNIT_CHECK(bakrtl_rename("GAMES\\SAVE01.GAM", "GAMES\\SAVE02.GAM") == -1);
    UNIT_CHECK(bakrtl_rename("games\\save02.gam", "GAMES\\SAVE03.GAM") == 0);
    UNIT_CHECK(bakrtl_rename("GAMES\\SAVE01.GAM", "GAMES\\SAVE02.GAM") == 0);
    size_t size;
    char *text = SDL_LoadFile(file, &size); /* SAVE02.GAM, the first one now */
    UNIT_CHECK(text != NULL && size == 1 && text[0] == '1');
    SDL_free(text);
    SDL_snprintf(file, sizeof file, "%s/SAVE03.GAM", games);
    text = SDL_LoadFile(file, &size);
    UNIT_CHECK(text != NULL && size == 1 && text[0] == '2');
    SDL_free(text);
    platform_set_dirs(".", ".");
    unit_remove_tree(root);
}
