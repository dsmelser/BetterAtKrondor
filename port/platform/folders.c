/*
 * The game's two folders. The game folder is the user's copy of Betrayal at Krondor, and is
 * only read. The user folder holds what the game writes (the saves under GAMES, TEMP.GAM,
 * KRONDOR.CFG) and betterkrondor.ini; it is SDL's per-user folder unless --user names another.
 * The game's DOS paths are looked up in the user folder first (bakrtl.c), so a file the game
 * rewrites, KRONDOR.CFG for one, comes from the game folder only until then.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_rtl.h"
#include "platform.h"

#define APP_NAME "BetterAtKrondor"
#define PATH_LEN 1024

static void strip_trailing_separators(char *path) {
    size_t n = strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\') && !(n == 3 && path[1] == ':'))
        path[--n] = '\0';
}

/* An absolute path without a trailing separator (SDL_free it), or NULL. */
static char *absolute_path(const char *path) {
#ifdef _WIN32
    char *full = _fullpath(NULL, path, 0);
#else
    char *full = realpath(path, NULL);
#endif
    if (full == NULL)
        return NULL;
    char *result = SDL_strdup(full);
    free(full);
    strip_trailing_separators(result);
    return result;
}

static bool same_path(const char *a, const char *b) {
#ifdef _WIN32
    return SDL_strcasecmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

char *folders_default_user_dir(void) {
    /* No organisation's folder above it (%APPDATA%\BetterAtKrondor): SDL warns that only app
     * stores mind. */
    char *path = SDL_GetPrefPath(NULL, APP_NAME);
    if (path != NULL)
        strip_trailing_separators(path);
    return path;
}

/* KRONDOR.RMF, the resource index, must be readable and every archive it names present:
 * u16 archive count, u16 hash seed, u16 hash rotation, then per archive a 13-byte
 * NUL-padded file name and the entries. Only the first archive's name is checked. */
bool folders_game_data_complete(const char *dir, char *problem, size_t size) {
    char path[PATH_LEN];
    SDL_IOStream *rmf = bakrtl_resolve_in(dir, "KRONDOR.RMF", path, sizeof path)
                            ? SDL_IOFromFile(path, "rb")
                            : NULL;
    if (rmf == NULL) {
        SDL_snprintf(problem, size, "no KRONDOR.RMF");
        return false;
    }
    unsigned char head[6];
    char archive[14] = {0};
    bool ok = SDL_ReadIO(rmf, head, sizeof head) == sizeof head && (head[0] | head[1] << 8) > 0 &&
              SDL_ReadIO(rmf, archive, 13) == 13 && memchr(archive, 0, 13) != NULL;
    SDL_CloseIO(rmf);
    if (!ok) {
        SDL_snprintf(problem, size, "KRONDOR.RMF is not a resource index");
        return false;
    }
    SDL_IOStream *payload =
        bakrtl_resolve_in(dir, archive, path, sizeof path) ? SDL_IOFromFile(path, "rb") : NULL;
    if (payload == NULL) {
        SDL_snprintf(problem, size, "KRONDOR.RMF indexes %s, which is missing", archive);
        return false;
    }
    SDL_CloseIO(payload);
    return true;
}

/* ---- choosing the game folder ------------------------------------------------------------ */

#define DIALOG_TITLE "Where is Betrayal at Krondor? Choose the folder with KRONDOR.RMF"

typedef struct FolderAnswer {
    SDL_AtomicInt done;
    char *path; /* NULL: cancelled, or the dialog failed */
} FolderAnswer;

/* Possibly on another thread. */
static void SDLCALL folder_chosen(void *userdata, const char *const *filelist, int filter) {
    (void)filter;
    FolderAnswer *answer = userdata;
    if (filelist == NULL)
        fprintf(stderr, "folder dialog: %s\n", SDL_GetError());
    else if (filelist[0] != NULL)
        answer->path = SDL_strdup(filelist[0]);
    SDL_SetAtomicInt(&answer->done, 1);
}

static char *ask_for_folder(const char *startAt) {
    FolderAnswer answer = {0};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, DIALOG_TITLE);
    if (startAt != NULL)
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, startAt);
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, folder_chosen, &answer, props);
    while (!SDL_GetAtomicInt(&answer.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);
    return answer.path;
}

/* A message with two buttons; true for the first. Without a message box (SDL's dummy video
 * driver has none) the message goes to stderr, and the answer is the second. */
static bool ask_yes_no(SDL_MessageBoxFlags kind, const char *msg, const char *yes, const char *no) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no},
    };
    const SDL_MessageBoxData box = {kind, NULL, "BetterAtKrondor", msg, 2, buttons, NULL};
    int choice = 0;
    if (!SDL_ShowMessageBox(&box, &choice)) {
        fprintf(stderr, "%s\n", msg);
        return false;
    }
    return choice == 1;
}

/* Until the user picks a folder that holds the game (returned) or gives up (NULL). */
static char *ask_until_found(const char *startAt) {
    for (;;) {
        char *picked = ask_for_folder(startAt);
        if (picked == NULL)
            return NULL;
        strip_trailing_separators(picked);
        char problem[256], msg[PATH_LEN + 400];
        if (folders_game_data_complete(picked, problem, sizeof problem))
            return picked;
        SDL_snprintf(msg, sizeof msg,
                     "That folder does not hold Betrayal at Krondor's files:\n%s\n(%s)", picked,
                     problem);
        SDL_free(picked);
        if (!ask_yes_no(SDL_MESSAGEBOX_WARNING, msg, "Choose another", "Quit"))
            return NULL;
    }
}

/* One more folder looked at: true if it holds the game, else a line "<folder> (<problem>)" for
 * the report. tried keeps the folders seen, so one is looked at once. */
static bool try_folder(const char *dir, const char **tried, int *triedCount, char *report,
                       size_t reportSize) {
    for (int i = 0; i < *triedCount; i++)
        if (same_path(tried[i], dir))
            return false;
    tried[(*triedCount)++] = dir;
    char problem[256];
    if (folders_game_data_complete(dir, problem, sizeof problem))
        return true;
    size_t used = strlen(report);
    SDL_snprintf(report + used, reportSize - used, "%s%s (%s)", used != 0 ? "\n" : "", dir,
                 problem);
    return false;
}

char *folders_choose_data_dir(const char *dataArg, bool canAsk, char *report, size_t reportSize) {
    report[0] = '\0';
    const char *tried[3];
    int triedCount = 0;
    char *chosen = NULL;
    if (dataArg != NULL) {
        /* Asked for: that folder or none. */
        if (try_folder(dataArg, tried, &triedCount, report, reportSize)) {
            chosen = absolute_path(dataArg);
            if (chosen == NULL)
                chosen = SDL_strdup(dataArg);
        }
    } else {
        /* The folder last used, the current one, the program's, else ask. */
        const char *remembered = settings_get_string(SETTING_PATHS_DATA);
        char *current = SDL_GetCurrentDirectory();
        char *program = SDL_strdup(SDL_GetBasePath() != NULL ? SDL_GetBasePath() : "");
        if (current != NULL)
            strip_trailing_separators(current);
        strip_trailing_separators(program);
        char lost[PATH_LEN + 300] = "";
        if (remembered[0] != '\0') {
            if (try_folder(remembered, tried, &triedCount, report, reportSize))
                chosen = SDL_strdup(remembered);
            else
                SDL_snprintf(lost, sizeof lost, "%s", report);
        }
        if (chosen == NULL && current != NULL &&
            try_folder(current, tried, &triedCount, report, reportSize))
            chosen = SDL_strdup(current);
        if (chosen == NULL && program[0] != '\0' &&
            try_folder(program, tried, &triedCount, report, reportSize))
            chosen = SDL_strdup(program);
        if (chosen == NULL && canAsk) {
            if (lost[0] != '\0') {
                char msg[sizeof lost + 100];
                SDL_snprintf(msg, sizeof msg,
                             "Betrayal at Krondor is no longer where it was:\n%s\n\nChoose the "
                             "folder it is in now.",
                             lost);
                if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "BetterAtKrondor", msg,
                                              NULL))
                    fprintf(stderr, "%s\n", msg);
            }
            chosen = ask_until_found(lost[0] != '\0' ? remembered : NULL);
            report[0] = '\0'; /* found, or the user gave up: nothing to report */
        }
        SDL_free(current);
        SDL_free(program);
    }
    if (chosen != NULL && strcmp(chosen, settings_get_string(SETTING_PATHS_DATA)) != 0)
        settings_set_string(SETTING_PATHS_DATA, chosen);
    return chosen;
}

/* ---- the DOS saves ------------------------------------------------------------------------- */

static SDL_EnumerationResult SDLCALL remove_entry(void *userdata, const char *dirname,
                                                  const char *fname);

static void remove_tree(const char *path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info))
        return;
    if (info.type == SDL_PATHTYPE_DIRECTORY)
        SDL_EnumerateDirectory(path, remove_entry, NULL);
    SDL_RemovePath(path);
}

static SDL_EnumerationResult SDLCALL remove_entry(void *userdata, const char *dirname,
                                                  const char *fname) {
    (void)userdata;
    char *path = NULL;
    if (SDL_asprintf(&path, "%s%s", dirname, fname) < 0)
        return SDL_ENUM_FAILURE;
    remove_tree(path);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

typedef struct CopyJob {
    const char *to;
    int depth; /* folder levels still to copy */
} CopyJob;

static bool copy_tree(const char *from, const char *to, int depth);

static SDL_EnumerationResult SDLCALL copy_entry(void *userdata, const char *dirname,
                                                const char *fname) {
    const CopyJob *job = userdata;
    char *src = NULL, *dst = NULL;
    bool ok = SDL_asprintf(&src, "%s%s", dirname, fname) >= 0 &&
              SDL_asprintf(&dst, "%s/%s", job->to, fname) >= 0;
    SDL_PathInfo info;
    if (ok && !SDL_GetPathInfo(src, &info))
        ok = false;
    else if (ok && info.type == SDL_PATHTYPE_DIRECTORY && job->depth > 0)
        ok = copy_tree(src, dst, job->depth - 1);
    else if (ok && info.type == SDL_PATHTYPE_FILE)
        ok = SDL_CopyFile(src, dst);
    SDL_free(src);
    SDL_free(dst);
    return ok ? SDL_ENUM_CONTINUE : SDL_ENUM_FAILURE;
}

static bool copy_tree(const char *from, const char *to, int depth) {
    CopyJob job = {to, depth};
    return SDL_CreateDirectory(to) && SDL_EnumerateDirectory(from, copy_entry, &job);
}

bool folders_import_saves(const char *dataDir, const char *userDir) {
    char from[PATH_LEN], to[PATH_LEN], partial[PATH_LEN + 8];
    SDL_PathInfo info;
    /* (One folder for both has saves, or none to copy.) */
    if (bakrtl_resolve_in(userDir, "GAMES", to, sizeof to) ||
        !bakrtl_resolve_in(dataDir, "GAMES", from, sizeof from) || !SDL_GetPathInfo(from, &info) ||
        info.type != SDL_PATHTYPE_DIRECTORY)
        return true; /* the user folder has saves, or there are none to copy */
    /* The DOS game keeps GAMES\<name>.G<nn>\SAVE<nn>.GAM; deeper folders are not its own. */
    SDL_snprintf(partial, sizeof partial, "%s.new", to);
    remove_tree(partial);
    if (!copy_tree(from, partial, 1) || !SDL_RenamePath(partial, to)) {
        fprintf(stderr, "The saves in %s could not be copied to %s: %s\n", from, to,
                SDL_GetError());
        remove_tree(partial);
        return false;
    }
    fprintf(stderr, "Copied the saves in %s to %s.\n", from, to);
    return true;
}
