/*
 * Autosaves: when one is due. MAINMENU.C's mainmenu_save_auto writes it at a pass of the world
 * loop where the player could save (WORLDLP.C), and the game does not notice it.
 *
 * One is asked for when the party enters a zone (zone_load, which a chapter's start goes through
 * too), and once saves.autosave_minutes have passed on the platform's clock since the last one if
 * the game's time moved meanwhile: a party standing still is not saved again. Starting the world
 * again (a load, a chapter, the Contents) forgets what was asked, and the clock starts anew.
 */
#include <stdio.h>

#include "bak_port.h"
#include "platform.h"

#define NS_PER_MINUTE 60000000000ull

static bool s_zoneEntered;
static bool s_started; /* s_lastNs and s_lastGameTime hold the last one's moment */
static uint64_t s_lastNs;
static uint32_t s_lastGameTime;
static bool s_failed; /* the last one could not be written, and that was said */

void bak_autosave_reset(void) {
    s_zoneEntered = false;
    s_started = false;
}

void bak_autosave_zone_entered(void) {
    s_zoneEntered = true;
}

bool bak_autosave_due(uint32_t gameTime) {
    if (!settings_get_bool(SETTING_SAVES_AUTOSAVE))
        return false;
    uint64_t now = clock_now_ns();
    if (!s_started) {
        s_started = true;
        s_lastNs = now;
        s_lastGameTime = gameTime;
    }
    if (s_zoneEntered)
        return true;
    int minutes = 0;
    settings_get_int(SETTING_SAVES_AUTOSAVE_MINUTES, &minutes);
    return minutes > 0 && now - s_lastNs >= (uint64_t)minutes * NS_PER_MINUTE &&
           gameTime != s_lastGameTime;
}

void bak_autosave_done(bool written, uint32_t gameTime) {
    s_zoneEntered = false;
    s_started = true;
    s_lastNs = clock_now_ns();
    s_lastGameTime = gameTime;
    if (!written && !s_failed)
        fprintf(stderr, "An autosave could not be written into %s/GAMES.\n", platform_user_dir());
    s_failed = !written;
}

int bak_autosave_slots(void) {
    int slots = 3;
    settings_get_int(SETTING_SAVES_AUTOSAVE_SLOTS, &slots);
    return slots;
}
