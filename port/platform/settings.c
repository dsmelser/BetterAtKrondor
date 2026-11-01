/*
 * betterkrondor.ini: the port's own settings, kept in the user folder beside the saves. Each
 * key has a type and a default (s_defs). The file is kept line for line: what the port does
 * not know (comments, keys of other versions) stays as it is, and a value the port changes
 * rewrites its own line only. --set gives a value for one run, never written back.
 * KRONDOR.CFG, the game's own settings (its sound choices), stays the game's.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform.h"

#define SETTINGS_FILE "betterkrondor.ini"

typedef enum SettingType {
    SETTING_TEXT, /* any one line */
} SettingType;

typedef struct SettingDef {
    const char *section;
    const char *key;
    SettingType type;
    const char *defaultValue;
    const char *help;
} SettingDef;

static const SettingDef s_defs[SETTING_COUNT] = {
    [SETTING_PATHS_DATA] = {"paths", "data", SETTING_TEXT, "",
                            "the game folder, remembered once it held the game"},
};

static char *s_values[SETTING_COUNT];
static int s_lineOf[SETTING_COUNT]; /* the line that sets it, or -1 */
static char **s_lines;              /* the file's lines, without their line breaks */
static int s_lineCount;
static bool s_crlf;  /* the file's line breaks */
static bool s_bom;   /* it starts with UTF-8's byte order mark */
static char *s_path; /* where changes are written; NULL: nowhere */

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

/* Whether value suits the setting's type; why says what is wrong when it does not. */
static bool check_value(int id, const char *value, char *why, size_t whySize) {
    (void)value;
    switch (s_defs[id].type) {
    case SETTING_TEXT:
        return true;
    }
    SDL_snprintf(why, whySize, "unknown type");
    return false;
}

static void set_value(int id, const char *value, size_t len) {
    SDL_free(s_values[id]);
    s_values[id] = SDL_strndup(value, len);
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
        char *v = SDL_strndup(value.at, value.len);
        char why[200];
        if (check_value(id, v, why, sizeof why))
            set_value(id, v, value.len);
        else
            fprintf(stderr, "%s:%d: %s.%s: %s; ignored\n", where, i + 1, s_defs[id].section,
                    s_defs[id].key, why);
        SDL_free(v);
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
    Span value = trimmed(eq + 1, eq + 1 + strlen(eq + 1));
    char *v = SDL_strndup(value.at, value.len);
    bool ok = check_value(id, v, why, whySize);
    if (ok)
        set_value(id, v, value.len);
    SDL_free(v);
    return ok;
}

const char *settings_get_string(SettingId id) {
    return s_values[id];
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
    set_value(id, value, len);
    if (s_lineOf[id] < 0)
        s_lineOf[id] = add_line(id);
    char *line = NULL;
    SDL_asprintf(&line, "%s = %s", s_defs[id].key, s_values[id]);
    SDL_free(s_lines[s_lineOf[id]]);
    s_lines[s_lineOf[id]] = line;
    return s_path == NULL || write_back();
}

void settings_print_help(void) {
    for (int id = 0; id < SETTING_COUNT; id++) {
        const SettingDef *d = &s_defs[id];
        char name[64];
        SDL_snprintf(name, sizeof name, "%s.%s", d->section, d->key);
        fprintf(stderr, "  %-14s %s%s%s%s\n", name, d->help,
                d->defaultValue[0] ? " (default: " : "", d->defaultValue,
                d->defaultValue[0] ? ")" : "");
    }
}
