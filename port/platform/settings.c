/*
 * betterkrondor.ini: the port's own settings, kept in the user folder beside the saves. Each
 * key has a type and a default (s_defs). The file is kept line for line: what the port does
 * not know (comments, keys of other versions) stays as it is, and a value the port changes
 * rewrites its own line only. --set gives a value for one run, never written back.
 * KRONDOR.CFG, the game's own settings (its sound choices), stays the game's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform.h"

#define SETTINGS_FILE "betterkrondor.ini"

typedef enum SettingType {
    SETTING_TEXT,   /* any one line */
    SETTING_BOOL,   /* true or false (yes/no, on/off, 1/0 read too) */
    SETTING_INT,    /* min to max, or nothing when optional */
    SETTING_CHOICE, /* one of choices */
    SETTING_KEY,    /* a key's name as SDL spells it (W, Up, Keypad 8), or none */
} SettingType;

typedef struct SettingDef {
    const char *section;
    const char *key;
    SettingType type;
    const char *defaultValue; /* as the file spells it */
    const char *help;
    int min, max;               /* SETTING_INT */
    bool optional;              /* SETTING_INT: may be empty */
    const char *const *choices; /* SETTING_CHOICE: NULL-terminated */
} SettingDef;

static const char *const s_scalings[] = {"pixelart", "nearest", "linear", NULL};
static const char *const s_renderers[] = {"gl", "sdl", NULL};
static const char *const s_aspects[] = {"4:3", "square", NULL};
static const char *const s_cursors[] = {"system", "game", NULL};
static const char *const s_views[] = {"gpu", "software", NULL};
static const char *const s_devices[] = {"sb", "adlib", "mt32", "none", "game", NULL};

static const SettingDef s_defs[SETTING_COUNT] = {
    [SETTING_PATHS_DATA] = {"paths", "data", SETTING_TEXT, "",
                            "the game folder, remembered once it held the game"},
    [SETTING_VIDEO_FULLSCREEN] = {"video", "fullscreen", SETTING_BOOL, "false",
                                  "fill the screen (Alt+Enter or F11 switches)"},
    [SETTING_VIDEO_SCALING] = {"video", "scaling", SETTING_CHOICE, "pixelart",
                               "how the picture is scaled up", .choices = s_scalings},
    [SETTING_VIDEO_RENDERER] = {"video", "renderer", SETTING_CHOICE, "gl",
                                "what draws the window: gl (OpenGL 3.3; SDL's renderer where "
                                "the driver has none), or sdl (SDL's renderer)",
                                .choices = s_renderers},
    [SETTING_VIDEO_INTEGER] = {"video", "integer", SETTING_BOOL, "false",
                               "scale by whole multiples only"},
    [SETTING_VIDEO_ASPECT] = {"video", "aspect", SETTING_CHOICE, "4:3",
                              "the picture's shape: a 4:3 screen's, or square pixels",
                              .choices = s_aspects},
    [SETTING_VIDEO_CURSOR] = {"video", "cursor", SETTING_CHOICE, "system",
                              "the mouse cursor: the system's, moving smoothly, or drawn into "
                              "the picture by the game as the original did",
                              .choices = s_cursors},
    [SETTING_VIDEO_STEP_TIME] = {"video", "step_time", SETTING_INT, "100",
                                 "ms a step or turn of the party takes, drawn smoothly (0: at "
                                 "once, as the original)",
                                 .min = 0, .max = 1000},
    [SETTING_VIDEO_VIEW3D] = {"video", "view3d", SETTING_CHOICE, "gpu",
                              "the 3-D view: gpu (drawn again through OpenGL at the window's "
                              "resolution), or software (the game's own 320x200 drawing)",
                              .choices = s_views},
    [SETTING_VIDEO_ANTIALIAS] = {"video", "antialias", SETTING_INT, "4",
                                 "samples each pixel of the GPU's 3-D view takes, which smooths "
                                 "its edges (0 or 1: none; at most what the driver offers)",
                                 .min = 0, .max = 16},
    [SETTING_WINDOW_WIDTH] = {"window", "width", SETTING_INT, "960", "the window's width",
                              .min = 320, .max = 16384},
    [SETTING_WINDOW_HEIGHT] = {"window", "height", SETTING_INT, "720", "the window's height",
                               .min = 200, .max = 16384},
    [SETTING_WINDOW_X] = {"window", "x", SETTING_INT, "",
                          "the window's place on the desktop, with window.y (none: centred)",
                          .min = -32768, .max = 32767, .optional = true},
    [SETTING_WINDOW_Y] = {"window", "y", SETTING_INT, "", "see window.x", .min = -32768,
                          .max = 32767, .optional = true},
    [SETTING_WINDOW_MAXIMIZED] = {"window", "maximized", SETTING_BOOL, "false",
                                  "the window fills the desktop"},
    [SETTING_CONTROLS_FORWARD] = {"controls", "forward", SETTING_KEY, "W",
                                  "a key that steps forward where the party moves (with Up)"},
    [SETTING_CONTROLS_BACK] = {"controls", "back", SETTING_KEY, "S",
                               "a key that steps back (with Down)"},
    [SETTING_CONTROLS_LEFT] = {"controls", "left", SETTING_KEY, "A",
                               "a key that turns left (with Left)"},
    [SETTING_CONTROLS_RIGHT] = {"controls", "right", SETTING_KEY, "D",
                                "a key that turns right (with Right)"},
    [SETTING_CONTROLS_REPEAT_DELAY] = {"controls", "repeat_delay", SETTING_INT, "250",
                                       "ms a movement key is held before it repeats",
                                       .min = 0, .max = 2000},
    [SETTING_CONTROLS_REPEAT_RATE] = {"controls", "repeat_rate", SETTING_INT, "10",
                                      "steps a second while it is held", .min = 1, .max = 60},
    [SETTING_CONTROLS_WHEEL_UP] = {"controls", "wheel_up", SETTING_KEY, "Up",
                                   "the key a notch of the wheel away from you presses"},
    [SETTING_CONTROLS_WHEEL_DOWN] = {"controls", "wheel_down", SETTING_KEY, "Down",
                                     "the key a notch towards you presses"},
    [SETTING_CONTROLS_POINTER_SPEED] = {"controls", "pointer_speed", SETTING_INT, "200",
                                        "pixels a second the gamepad's right stick moves the "
                                        "pointer at full tilt",
                                        .min = 10, .max = 2000},
    [SETTING_AUDIO_VOLUME] = {"audio", "volume", SETTING_INT, "100",
                              "the sound's volume, in percent", .min = 0, .max = 100},
    [SETTING_AUDIO_MUSIC_VOLUME] = {"audio", "music_volume", SETTING_INT, "100",
                                    "the CD music's, in percent of that", .min = 0, .max = 100},
    [SETTING_AUDIO_EFFECTS_VOLUME] = {"audio", "effects_volume", SETTING_INT, "100",
                                      "the synthesizer's and the samples', in percent of that",
                                      .min = 0, .max = 100},
    [SETTING_AUDIO_DEVICE] = {"audio", "device", SETTING_CHOICE, "sb",
                              "the sound card the game drives: sb (the Sound Blaster: the AdLib's "
                              "synthesizer and digital samples), adlib, mt32 (its MIDI goes "
                              "nowhere yet), none, or game (RESOURCE.CFG's choice)",
                              .choices = s_devices},
    [SETTING_SAVES_AUTOSAVE] = {"saves", "autosave", SETTING_BOOL, "true",
                                "save on entering a zone (and a chapter) and on the clock, into "
                                "the AUTOSAVE folder"},
    [SETTING_SAVES_AUTOSAVE_MINUTES] = {"saves", "autosave_minutes", SETTING_INT, "10",
                                        "minutes between the clock's autosaves, while the "
                                        "game's time moves (0: none)",
                                        .min = 0, .max = 600},
    [SETTING_SAVES_AUTOSAVE_SLOTS] = {"saves", "autosave_slots", SETTING_INT, "3",
                                      "autosaves kept, the newest first", .min = 1, .max = 20},
};

static char *s_values[SETTING_COUNT];
static int s_lineOf[SETTING_COUNT]; /* the line that sets it, or -1 */
static char **s_lines;              /* the file's lines, without their line breaks */
static int s_lineCount;
static bool s_crlf;  /* the file's line breaks */
static bool s_bom;   /* it starts with UTF-8's byte order mark */
static char *s_path; /* where changes are written; NULL: nowhere */
static int s_held;   /* settings_hold calls not yet released */
static bool s_unsaved;

typedef struct Span {
    const char *at;
    size_t len;
} Span;

static Span trimmed(const char *at, const char *end) {
    while (at < end && (*at == ' ' || *at == '\t'))
        at++;
    while (end > at && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    return (Span){at, (size_t)(end - at)};
}

static bool span_is(Span s, const char *name) {
    return SDL_strncasecmp(s.at, name, s.len) == 0 && name[s.len] == '\0';
}

static bool is_blank(const char *line) {
    return trimmed(line, line + strlen(line)).len == 0;
}

typedef enum LineKind {
    LINE_OTHER, /* blank, a comment, or nothing the format knows */
    LINE_SECTION,
    LINE_VALUE,
} LineKind;

/* "[name]", "name = value" (spaces around either are not part of it), or something else. */
static LineKind classify(const char *line, Span *name, Span *value) {
    Span s = trimmed(line, line + strlen(line));
    if (s.len == 0 || s.at[0] == ';' || s.at[0] == '#')
        return LINE_OTHER;
    if (s.at[0] == '[') {
        if (s.len < 2 || s.at[s.len - 1] != ']')
            return LINE_OTHER;
        *name = trimmed(s.at + 1, s.at + s.len - 1);
        return LINE_SECTION;
    }
    const char *eq = memchr(s.at, '=', s.len);
    if (eq == NULL)
        return LINE_OTHER;
    *name = trimmed(s.at, eq);
    *value = trimmed(eq + 1, s.at + s.len);
    return name->len != 0 ? LINE_VALUE : LINE_OTHER;
}

static int find_def(Span section, Span key) {
    for (int id = 0; id < SETTING_COUNT; id++)
        if (span_is(section, s_defs[id].section) && span_is(key, s_defs[id].key))
            return id;
    return -1;
}

static void set_value(int id, const char *value, size_t len) {
    SDL_free(s_values[id]);
    s_values[id] = SDL_strndup(value, len);
}

/* A value for the setting, as its type reads it, in the spelling it is kept in; false, with
 * why, for a value its type does not take. */
static bool take_value(int id, Span value, char *why, size_t whySize) {
    const SettingDef *d = &s_defs[id];
    switch (d->type) {
    case SETTING_TEXT:
        set_value(id, value.at, value.len);
        return true;
    case SETTING_BOOL: {
        static const char *const yes[] = {"true", "yes", "on", "1"};
        static const char *const no[] = {"false", "no", "off", "0"};
        for (size_t i = 0; i < SDL_arraysize(yes); i++) {
            if (span_is(value, yes[i]) || span_is(value, no[i])) {
                const char *kept = span_is(value, yes[i]) ? "true" : "false";
                set_value(id, kept, strlen(kept));
                return true;
            }
        }
        SDL_snprintf(why, whySize, "not true or false");
        return false;
    }
    case SETTING_INT: {
        if (value.len == 0 && d->optional) {
            set_value(id, "", 0);
            return true;
        }
        char digits[16];
        char *end = NULL;
        long n = 0;
        if (value.len < sizeof digits) {
            memcpy(digits, value.at, value.len);
            digits[value.len] = '\0';
            n = strtol(digits, &end, 10);
        }
        if (value.len == 0 || value.len >= sizeof digits || *end != '\0' || n < d->min ||
            n > d->max) {
            SDL_snprintf(why, whySize, "not a whole number from %d to %d", d->min, d->max);
            return false;
        }
        SDL_snprintf(digits, sizeof digits, "%ld", n);
        set_value(id, digits, strlen(digits));
        return true;
    }
    case SETTING_CHOICE:
        for (int i = 0; d->choices[i] != NULL; i++) {
            if (span_is(value, d->choices[i])) {
                set_value(id, d->choices[i], strlen(d->choices[i]));
                return true;
            }
        }
        SDL_snprintf(why, whySize, "not one of");
        for (int i = 0; d->choices[i] != NULL; i++) {
            size_t used = strlen(why);
            SDL_snprintf(why + used, whySize - used, "%s %s", i == 0 ? "" : ",", d->choices[i]);
        }
        return false;
    case SETTING_KEY: {
        if (value.len == 0) {
            set_value(id, "", 0);
            return true;
        }
        char name[64];
        SDL_snprintf(name, sizeof name, "%.*s", (int)value.len, value.at);
        SDL_Scancode key = SDL_GetScancodeFromName(name);
        if (key == SDL_SCANCODE_UNKNOWN || value.len >= sizeof name) {
            SDL_snprintf(why, whySize, "not a key's name (W, Up, Space, Keypad 8...)");
            return false;
        }
        const char *kept = SDL_GetScancodeName(key);
        set_value(id, kept, strlen(kept));
        return true;
    }
    }
    SDL_snprintf(why, whySize, "unknown type");
    return false;
}

static void reset(void) {
    for (int i = 0; i < s_lineCount; i++)
        SDL_free(s_lines[i]);
    SDL_free(s_lines);
    s_lines = NULL;
    s_lineCount = 0;
    for (int id = 0; id < SETTING_COUNT; id++) {
        set_value(id, s_defs[id].defaultValue, strlen(s_defs[id].defaultValue));
        s_lineOf[id] = -1;
    }
#ifdef _WIN32
    s_crlf = true;
#else
    s_crlf = false;
#endif
    s_bom = false;
    s_unsaved = false;
    SDL_free(s_path);
    s_path = NULL;
}

static void insert_line(int at, char *line) {
    s_lines = SDL_realloc(s_lines, sizeof *s_lines * (size_t)(s_lineCount + 1));
    memmove(s_lines + at + 1, s_lines + at, sizeof *s_lines * (size_t)(s_lineCount - at));
    s_lines[at] = line;
    s_lineCount++;
    for (int id = 0; id < SETTING_COUNT; id++)
        if (s_lineOf[id] >= at)
            s_lineOf[id]++;
}

/* Split text into lines, then take the known keys' values: the last line for a key wins. */
static void parse(const char *text, size_t size, const char *where) {
    if (size >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) {
        s_bom = true;
        text += 3;
        size -= 3;
    }
    const char *end = text + size;
    bool sawBreak = false;
    for (const char *p = text; p < end;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *lineEnd = nl != NULL ? nl : end;
        if (nl != NULL && !sawBreak) {
            s_crlf = nl > p && nl[-1] == '\r';
            sawBreak = true;
        }
        if (lineEnd > p && lineEnd[-1] == '\r')
            lineEnd--;
        insert_line(s_lineCount, SDL_strndup(p, (size_t)(lineEnd - p)));
        p = nl != NULL ? nl + 1 : end;
    }
    Span section = {NULL, 0};
    for (int i = 0; i < s_lineCount; i++) {
        Span name, value;
        LineKind kind = classify(s_lines[i], &name, &value);
        if (kind == LINE_SECTION)
            section = name;
        if (kind != LINE_VALUE || section.at == NULL)
            continue;
        int id = find_def(section, name);
        if (id < 0)
            continue;
        s_lineOf[id] = i;
        char why[200];
        if (!take_value(id, value, why, sizeof why))
            fprintf(stderr, "%s:%d: %s.%s: %s; ignored\n", where, i + 1, s_defs[id].section,
                    s_defs[id].key, why);
    }
}

bool settings_load(const char *dir) {
    reset();
    if (SDL_asprintf(&s_path, "%s/%s", dir, SETTINGS_FILE) < 0) {
        s_path = NULL;
        return false;
    }
    size_t size;
    void *text = SDL_LoadFile(s_path, &size);
    if (text == NULL) {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(s_path, &info))
            return true; /* no file yet: the defaults */
        fprintf(stderr, "%s: cannot be read (%s); the defaults apply, and nothing is written\n",
                s_path, SDL_GetError());
        SDL_free(s_path);
        s_path = NULL;
        return false;
    }
    parse(text, size, s_path);
    SDL_free(text);
    return true;
}

void settings_load_text(const char *text, size_t size) {
    reset();
    parse(text, size, "settings");
}

bool settings_override(const char *assignment, char *why, size_t whySize) {
    const char *eq = strchr(assignment, '=');
    const char *dot = eq != NULL ? memchr(assignment, '.', (size_t)(eq - assignment)) : NULL;
    if (dot == NULL) {
        SDL_snprintf(why, whySize, "not section.key=value");
        return false;
    }
    int id = find_def(trimmed(assignment, dot), trimmed(dot + 1, eq));
    if (id < 0) {
        SDL_snprintf(why, whySize, "no such setting (--help lists them)");
        return false;
    }
    return take_value(id, trimmed(eq + 1, eq + 1 + strlen(eq + 1)), why, whySize);
}

/* A setting's value; the defaults when nothing was loaded (the test runners). */
static const char *value_of(SettingId id) {
    if (s_values[id] == NULL)
        reset();
    return s_values[id];
}

const char *settings_get_string(SettingId id) {
    return value_of(id);
}

bool settings_get_bool(SettingId id) {
    return strcmp(value_of(id), "true") == 0;
}

bool settings_get_int(SettingId id, int *value) {
    const char *text = value_of(id);
    if (text[0] == '\0')
        return false;
    *value = (int)strtol(text, NULL, 10);
    return true;
}

int settings_get_choice(SettingId id) {
    const char *text = value_of(id);
    for (int i = 0; s_defs[id].choices[i] != NULL; i++)
        if (strcmp(text, s_defs[id].choices[i]) == 0)
            return i;
    return 0;
}

char *settings_text(size_t *size) {
    const char *eol = s_crlf ? "\r\n" : "\n";
    size_t total = s_bom ? 3 : 0;
    for (int i = 0; i < s_lineCount; i++)
        total += strlen(s_lines[i]) + strlen(eol);
    char *text = SDL_malloc(total + 1);
    char *p = text;
    if (s_bom) {
        memcpy(p, "\xEF\xBB\xBF", 3);
        p += 3;
    }
    for (int i = 0; i < s_lineCount; i++) {
        size_t n = strlen(s_lines[i]);
        memcpy(p, s_lines[i], n);
        p += n;
        memcpy(p, eol, strlen(eol));
        p += strlen(eol);
    }
    *p = '\0';
    if (size != NULL)
        *size = total;
    return text;
}

/* Through a temporary file, so a failed write leaves the old one whole. */
static bool write_back(void) {
    size_t size;
    char *text = settings_text(&size);
    char *temp = NULL;
    bool ok = SDL_asprintf(&temp, "%s.new", s_path) >= 0 && SDL_SaveFile(temp, text, size) &&
              SDL_RenamePath(temp, s_path);
    if (!ok) {
        fprintf(stderr, "%s: cannot be written (%s)\n", s_path, SDL_GetError());
        if (temp != NULL)
            SDL_RemovePath(temp);
    }
    SDL_free(temp);
    SDL_free(text);
    return ok;
}

/* Where a key without a line goes: after the last value in its section, or in a new section
 * at the end. Returns the line. */
static int add_line(int id) {
    int header = -1;
    for (int i = 0; i < s_lineCount; i++) {
        Span name, value;
        if (classify(s_lines[i], &name, &value) == LINE_SECTION &&
            span_is(name, s_defs[id].section))
            header = i;
    }
    if (header < 0) {
        if (s_lineCount != 0 && !is_blank(s_lines[s_lineCount - 1]))
            insert_line(s_lineCount, SDL_strdup(""));
        char *line = NULL;
        SDL_asprintf(&line, "[%s]", s_defs[id].section);
        insert_line(s_lineCount, line);
        insert_line(s_lineCount, SDL_strdup(""));
        return s_lineCount - 1;
    }
    int at = header + 1;
    for (int i = header + 1; i < s_lineCount; i++) {
        Span name, value;
        LineKind kind = classify(s_lines[i], &name, &value);
        if (kind == LINE_SECTION)
            break;
        if (kind == LINE_VALUE)
            at = i + 1;
    }
    insert_line(at, SDL_strdup(""));
    return at;
}

bool settings_set_string(SettingId id, const char *value) {
    size_t len = strcspn(value, "\r\n"); /* one line */
    if (strlen(value_of(id)) == len && memcmp(s_values[id], value, len) == 0)
        return true; /* nothing changes */
    set_value(id, value, len);
    if (s_lineOf[id] < 0)
        s_lineOf[id] = add_line(id);
    char *line = NULL;
    SDL_asprintf(&line, "%s = %s", s_defs[id].key, s_values[id]);
    SDL_free(s_lines[s_lineOf[id]]);
    s_lines[s_lineOf[id]] = line;
    if (s_held != 0) {
        s_unsaved = true;
        return true;
    }
    return s_path == NULL || write_back();
}

void settings_hold(void) {
    s_held++;
}

bool settings_release(void) {
    if (--s_held != 0 || !s_unsaved)
        return true;
    s_unsaved = false;
    return s_path == NULL || write_back();
}

bool settings_set_bool(SettingId id, bool value) {
    return settings_set_string(id, value ? "true" : "false");
}

bool settings_set_int(SettingId id, int value) {
    char text[16];
    SDL_snprintf(text, sizeof text, "%d", value);
    return settings_set_string(id, text);
}

void settings_print_help(void) {
    for (int id = 0; id < SETTING_COUNT; id++) {
        const SettingDef *d = &s_defs[id];
        char name[64], what[300];
        SDL_snprintf(name, sizeof name, "%s.%s", d->section, d->key);
        SDL_snprintf(what, sizeof what, "%s", d->help);
        for (int i = 0; d->type == SETTING_CHOICE && d->choices[i] != NULL; i++) {
            size_t used = strlen(what);
            SDL_snprintf(what + used, sizeof what - used, "%s%s", i == 0 ? ": " : ", ",
                         d->choices[i]);
        }
        if (d->defaultValue[0] != '\0') {
            size_t used = strlen(what);
            SDL_snprintf(what + used, sizeof what - used, " (default: %s)", d->defaultValue);
        }
        fprintf(stderr, "  %-17s %s\n", name, what);
    }
}
