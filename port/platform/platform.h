/* platform.h - the SDL3 platform layer's internal interface. */
#ifndef BAK_PLATFORM_H
#define BAK_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Game data directory (the user's copy, only read) and the writable user directory (saves,
 * TEMP.GAM, KRONDOR.CFG, betterkrondor.ini). */
void platform_set_dirs(const char *dataDir, const char *userDir);
const char *platform_data_dir(void);
const char *platform_user_dir(void);

/* What the command line chose; NULL where it chose nothing. */
typedef struct PlatformOptions {
    const char *dataDir; /* --data */
    const char *userDir; /* --user */
    const char **sets;   /* each --set's "section.key=value", setCount of them */
    int setCount;
} PlatformOptions;

/* The folders and the settings, SDL, the game folder's check, the window. False after
 * reporting why the game cannot start (or the user declined to choose a game folder). */
bool platform_init(const PlatformOptions *options);
/* An error for the user: stderr, and a message box unless BAK_HEADLESS is set. */
void platform_report_error(const char *msg);

/*
 * betterkrondor.ini (settings.c): the port's settings, in the user folder. Each has a type and
 * a default; a changed one is written back at once, and lines the port does not know stay.
 */
typedef enum SettingId {
    SETTING_PATHS_DATA,       /* [paths] data: the game folder */
    SETTING_VIDEO_FULLSCREEN, /* [video] fullscreen */
    SETTING_VIDEO_SCALING,    /* [video] scaling: SCALING_* */
    SETTING_VIDEO_RENDERER,   /* [video] renderer: RENDERER_* */
    SETTING_VIDEO_INTEGER,    /* [video] integer: whole multiples only */
    SETTING_VIDEO_ASPECT,     /* [video] aspect: ASPECT_* */
    SETTING_VIDEO_CURSOR,     /* [video] cursor: CURSOR_* */
    SETTING_VIDEO_STEP_TIME,  /* [video] step_time: ms a step or turn of the party takes */
    SETTING_VIDEO_VIEW3D,     /* [video] view3d: VIEW3D_* */
    SETTING_VIDEO_ANTIALIAS,  /* [video] antialias: samples a pixel of the GPU's view takes */
    SETTING_WINDOW_WIDTH,     /* [window] width, height, x, y (optional), maximized */
    SETTING_WINDOW_HEIGHT,
    SETTING_WINDOW_X,
    SETTING_WINDOW_Y,
    SETTING_WINDOW_MAXIMIZED,
    SETTING_CONTROLS_FORWARD, /* [controls] forward, back, left, right: a key's name */
    SETTING_CONTROLS_BACK,
    SETTING_CONTROLS_LEFT,
    SETTING_CONTROLS_RIGHT,
    SETTING_CONTROLS_REPEAT_DELAY, /* [controls] repeat_delay: ms */
    SETTING_CONTROLS_REPEAT_RATE,  /* [controls] repeat_rate: a second */
    SETTING_CONTROLS_WHEEL_UP,     /* [controls] wheel_up, wheel_down: a key's name */
    SETTING_CONTROLS_WHEEL_DOWN,
    SETTING_CONTROLS_POINTER_SPEED, /* [controls] pointer_speed: picture pixels a second */
    SETTING_AUDIO_VOLUME,           /* [audio] volume, music_volume, effects_volume: 0-100 */
    SETTING_AUDIO_MUSIC_VOLUME,
    SETTING_AUDIO_EFFECTS_VOLUME,
    SETTING_AUDIO_DEVICE,           /* [audio] device: DEVICE_* */
    SETTING_SAVES_AUTOSAVE,         /* [saves] autosave: on entering a zone and on the clock */
    SETTING_SAVES_AUTOSAVE_MINUTES, /* [saves] autosave_minutes: between them (0: none) */
    SETTING_SAVES_AUTOSAVE_SLOTS,   /* [saves] autosave_slots: how many are kept */
    SETTING_COUNT
} SettingId;
enum { SCALING_PIXELART, SCALING_NEAREST, SCALING_LINEAR };
enum { RENDERER_GL, RENDERER_SDL };
enum { ASPECT_4_3, ASPECT_SQUARE };
enum { CURSOR_SYSTEM, CURSOR_GAME };
enum { VIEW3D_GPU, VIEW3D_SOFTWARE };
enum { DEVICE_SB, DEVICE_ADLIB, DEVICE_MT32, DEVICE_NONE, DEVICE_GAME };
/* Read <dir>/betterkrondor.ini (none: the defaults), where changes then go. False, with a
 * message, when it exists but cannot be read; then nothing is written back. */
bool settings_load(const char *dir);
/* Settings from text, written back nowhere (the unit tests). */
void settings_load_text(const char *text, size_t size);
/* --set's "section.key=value": a value for this run only. False, with why, for an unknown key
 * or a bad value. */
bool settings_override(const char *assignment, char *why, size_t whySize);
const char *settings_get_string(SettingId id);
bool settings_get_bool(SettingId id);
/* False, leaving value, when an optional integer is not set. */
bool settings_get_int(SettingId id, int *value);
/* The choice's index (SCALING_*, ASPECT_*). */
int settings_get_choice(SettingId id);
/* A new value, written back at once unless it is the value already; false, with a message, if
 * writing failed. */
bool settings_set_string(SettingId id, const char *value);
bool settings_set_bool(SettingId id, bool value);
bool settings_set_int(SettingId id, int value);
/* Changes between these are written back once, at the release. */
void settings_hold(void);
bool settings_release(void);
/* The file as written (SDL_free it). */
char *settings_text(size_t *size);
/* The settings, their meaning and defaults, on stderr (--help). */
void settings_print_help(void);

/* The folders (folders.c). The default user folder: SDL's per-user one, made if missing
 * (SDL_free it), or NULL. */
char *folders_default_user_dir(void);
/* Whether dir holds the game's resources; if not, problem says what is missing. */
bool folders_game_data_complete(const char *dir, char *problem, size_t size);
/* The game folder (SDL_free it): dataArg if given, else the one remembered in the settings,
 * the current folder or the program's; else, if canAsk, the one the user picks in a folder
 * dialog. The folder found is remembered. NULL if none holds the game: report lists the folders
 * looked at, each with its problem, or is empty when the user gave up on the dialog. */
char *folders_choose_data_dir(const char *dataArg, bool canAsk, char *report, size_t reportSize);
/* Copy the game folder's GAMES (the DOS game's saves) into a user folder that has none, so they
 * load; false, with a message, if copying failed. */
bool folders_import_saves(const char *dataDir, const char *userDir);

/* Set by the test runner: no real-time waits and no presenting (vertical retrace is
 * always under way). */
extern bool g_platformFreeRunning;
/* Called where the game polls while the platform is free-running: the runner's stand-in for
 * time passing (it may deliver a device's interrupt there). */
extern void (*g_platformFreeRunningPoll)(void);

/*
 * The clock everything time-driven follows (clock.c): the host's, or with BAK_CLOCK=virtual
 * one that moves only as the game runs, so scripted runs repeat exactly.
 */
#define CLOCK_POLL_NS 1000ull /* what a poll costs on the virtual clock */
bool clock_is_virtual(void);
/* Switch to the virtual clock at nowNs (the unit tests). */
void clock_use_virtual(uint64_t nowNs);
uint64_t clock_now_ns(void);
/* Account for work done: moves the virtual clock, nothing on the host's. */
void clock_spend(uint64_t ns);
/* Wait: the host's clock sleeps wallNs; the virtual one jumps to dueNs, the next moment
 * something is due (or by a poll's cost if that has passed). */
void clock_idle(uint64_t wallNs, uint64_t dueNs);

/* The plain blocks (dosmem.c's bak_plain_alloc), for FP_OFF of a pointer into one (segments.c). */
void segments_note_plain(const void *base, uint32_t size);
void segments_forget_plain(const void *base);

/* A fast hash of a buffer for the replay logs (hash.c); chain calls through hash. */
uint32_t platform_hash(uint32_t hash, const void *data, size_t bytes);

/* Print crashes (exception + symbolized call stack) to stderr (crash_win32.c). */
void crash_reporter_install(void);
/* Print the caller's call stack, symbolized, to stderr (nothing elsewhere than Windows). */
void crash_print_stack(void);
void platform_shutdown(void);

/* Pump host events; returns false once the user asked to quit. */
bool platform_pump_events(void);

/* The game window (video.c): made as the settings say, its place and size saved on closing. */
bool video_open(void);
void video_close(void);
/* The picture the CRTC scans out, to the window if it changed (the frame log's moments). */
void video_present(void);
/* The last picture to the window if it lacks it, once the display's refresh allows. */
void video_flush(void);
/* A window position as a position on the 320x200 picture; false outside any window. */
bool video_window_to_picture(float wx, float wy, float *px, float *py);
/* To and from the full screen, remembered. */
void video_toggle_fullscreen(void);
/* A key for the window (Alt+Enter, F11, and their releases): true when it took it, so the
 * game does not see it. */
union SDL_Event;
bool video_handle_key(const union SDL_Event *e);
/* The window moved, was resized or uncovered, or went to another display. */
void video_handle_window_event(const union SDL_Event *e);
/* The DAC index a 16-colour pixel shows, through the attribute controller. */
uint8_t video_attribute_color(unsigned value);
/* The host's pointer to a place on the picture (the game moved the mouse), while the system
 * cursor shows and the window has the mouse. */
void video_warp_pointer(float px, float py);

/*
 * The window's picture through OpenGL 3.3 (glpresent.c). glpresent_open makes the context of a
 * window made with SDL_WINDOW_OPENGL; false, having said why, when it cannot. glpresent_draw
 * shows a w x h picture of XRGB words in the window, at a shown height of shownH picture pixels
 * (4:3: 3w/4), letterboxed and scaled as SCALING_* says, by whole multiples if integer.
 * glpresent_layout is where in an outW x outH output the picture goes; glpresent_render draws
 * it into the framebuffer bound, glpresent_render_offscreen into a new one, read back into
 * outW x outH RGBA bytes, the top row first (the tests). glpresent_name says what draws.
 */
struct SDL_Window;
struct SDL_FRect;
bool glpresent_open(struct SDL_Window *window);
void glpresent_close(void);
const char *glpresent_name(void);
void glpresent_layout(int outW, int outH, int w, int shownH, bool integer, struct SDL_FRect *r);
void glpresent_render(const uint32_t *xrgb, int w, int h, int shownH, int outW, int outH,
                      int scaling, bool integer);
void glpresent_draw(const uint32_t *xrgb, int w, int h, int shownH, int scaling, bool integer);
bool glpresent_render_offscreen(const uint32_t *xrgb, int w, int h, int shownH, int outW,
                                int outH, int scaling, bool integer, uint8_t *rgba);

/*
 * The system cursor (cursor.c). cursor_decode turns a game cursor image of the VGA driver's
 * (colors256) or the 16-colour driver's into DAC indices, CURSOR_CLEAR where transparent; false
 * when it is empty or larger than CURSOR_MAX a side. cursor_update makes the window's cursor
 * the game's, scaled by sx x sy window pixels a picture pixel.
 */
#define CURSOR_MAX 64
#define CURSOR_CLEAR 0xFFFF
struct ImageRecord;
bool cursor_decode(const struct ImageRecord *image, bool colors256, int *w, int *h, uint16_t *px);
void cursor_update(float sx, float sy);
bool cursor_shown(void);

/* Feed a host mouse event to the emulated INT 33h driver (mouse.c). */
union SDL_Event;
void mouse_handle_event(const union SDL_Event *e);
/* scancode.c: a key (an SDL_Scancode) as the PC keyboard sends it, one IRQ 1 a byte; and
 * whether the game has taken every key from the BIOS keyboard buffer. */
void keyboard_send(int key, bool down);
bool keyboard_buffer_empty(void);
/*
 * The keyboard through the bindings (input.c). A host key event, real or scripted, goes to
 * the game as itself, or on the pages where the party moves (the world's and the map's) as
 * the movement it is bound to. Held movement keys repeat on the clock (input_poll, as the
 * platform polls), the host's own repeats left out.
 */
void input_handle_key(const union SDL_Event *e);
/* The wheel: a key press a notch (controls.wheel_up, wheel_down). */
void input_handle_wheel(const union SDL_Event *e);
/* Gamepads: connected and gone, buttons and sticks. The d-pad and the left stick are the
 * arrows (as keys, through the bindings), the right stick moves the pointer, A and X are the
 * left and right buttons, B is Escape and Y Return. */
void input_handle_gamepad(const union SDL_Event *e);
void input_poll(void);
/* Back to no keys held and no page known (the unit tests). */
void input_reset(void);
/* Scripted pointer input: a position on the 320x200 picture, the left button. */
void mouse_script_move(float px, float py);
void mouse_script_left(bool down);
/* The pointer moved by (dx, dy) picture pixels; a button (1 left, 2 right) pressed or let go. */
void mouse_nudge(float dx, float dy);
void mouse_button(uint16_t bit, bool down);

/* Run the BAK_INPUT steps that are due (inputscript.c); false once one asks to quit. */
bool input_script_poll(void);
/* Ask the game to move the party (teleport.c): its pending teleport, carried out by the world
 * loop's next pass. A cell is 1/40 of a tile; a heading's full turn is 10000h. A town scene
 * (its kind and index, or 0) is entered first; zone 255 enters the scene and moves nobody. */
void game_request_teleport(uint8_t zone, uint8_t tileX, uint8_t tileY, uint8_t cellX,
                           uint8_t cellY, uint16_t heading, uint16_t sceneKind,
                           uint16_t sceneIndex);
/* Write one of the game's events, as a dialog's action does (gameevent.c): a flag, or a game
 * state field such as the chapter (7537h) or the world loop's exit request (7541h). */
void game_write_event(uint16_t id, uint16_t value);

/* machine.c: the I/O ports of the timer, interrupt controller and keyboard controller
 * (false: not one of these), and a byte arriving from the keyboard (IRQ 1). */
bool machine_port_in8(uint16_t port, uint8_t *value);
bool machine_port_out8(uint16_t port, uint8_t value);
void bak_keyboard_irq(uint8_t scancode);
/* The next moment a poll has work: the timer's next tick or the next look at host events. */
uint64_t machine_next_due_ns(void);
/* The moment of what the game does now, on the platform's clock: in the timer interrupt, when
 * its tick was due (the platform runs late ticks in a burst), in a device's interrupt when it was
 * due; elsewhere the present. */
uint64_t machine_event_ns(void);
/* A device's interrupt on IRQ irq (0-7, the 8259's) due at dueNs: its vector runs now unless
 * the line is masked or has no handler (then it is lost). */
void machine_raise_irq(int irq, uint64_t dueNs);

/*
 * The CD drive (cdrom.c). It appears when the game folder holds the CD's audio tracks as
 * music\bakNN.ogg: its table of contents is laid out from their lengths, and its plays go to the
 * CD's audio source. False when there are none.
 */
bool cdrom_open_music(const char *dataDir);
/* An Ogg Vorbis file's rate, channels and length in samples, from its first and last pages. */
bool cdrom_ogg_info(const char *path, int *rate, int *channels, uint64_t *samples);
/* The audio track whose gap or audio holds an HSG sector, and the frame of the track it is
 * (negative in the gap before it); 0 for none. */
int cdrom_track_at(uint32_t sector, int64_t *from);

/* mpu401.c: the MPU-401's ports (false: not 330h or 331h). */
bool mpu401_port_in8(uint16_t port, uint8_t *value);
bool mpu401_port_out8(uint16_t port, uint8_t value);

/* opl.c: the AdLib's OPL2 at 388h and 389h (false: not one of them). opl_close drops the chip
 * and forgets its source (after audio_close, in the tests). */
bool opl_port_in8(uint16_t port, uint8_t *value);
bool opl_port_out8(uint16_t port, uint8_t value);
void opl_close(void);

/*
 * sbdsp.c: the Sound Blaster's DSP (base 220h), the 8237 DMA controller's channel 1 (ports 02h,
 * 03h, 0Ah, 0Bh, 0Ch, 83h) and the interrupt (IRQ 7) the DSP raises when a block ends.
 * sbdsp_poll delivers it once due (machine.c's poll; true when it did), sbdsp_next_due_ns is
 * when (0: nothing due). The tests configure the card (sbdsp_configure: absent, another base,
 * IRQ or DSP version), end a block at once (sbdsp_complete_now: the free-running stand-in for
 * its time) and read its state; sbdsp_close drops the audio source (after audio_close).
 */
bool sbdsp_port_in8(uint16_t port, uint8_t *value);
bool sbdsp_port_out8(uint16_t port, uint8_t value);
bool sbdsp_poll(uint64_t nowNs);
uint64_t sbdsp_next_due_ns(void);
void sbdsp_configure(bool present, uint16_t base, int irq, uint16_t version);
bool sbdsp_complete_now(void);
typedef struct SbdspState {
    uint8_t timeConstant;
    bool speaker;
    bool active;     /* a block is under way */
    bool halted;     /* by command D0h */
    uint32_t length; /* the block's samples */
    uint8_t dmaMode, dmaPage;
    uint16_t dmaAddress, dmaCount; /* as programmed */
    bool dmaMasked;
} SbdspState;
void sbdsp_state(SbdspState *out);
void sbdsp_close(void);

#endif
