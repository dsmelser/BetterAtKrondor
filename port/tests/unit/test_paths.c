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
