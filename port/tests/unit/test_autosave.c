/* Autosaves: when one is due (autosave.c), and the machine standing still while one writes. */
#include "unit.h"

#include "../../platform/platform.h"
#include "bak_port.h"

#define MINUTE_NS 60000000000ull

/* One is due on entering a zone, and on the clock while the game's time moves. */
void unit_autosave_due_on_a_zone_and_the_clock(void) {
    char why[200];
    clock_use_virtual(1000000000ull);
    UNIT_CHECK(settings_override("saves.autosave=true", why, sizeof why));
    UNIT_CHECK(settings_override("saves.autosave_minutes=2", why, sizeof why));
    bak_autosave_reset();
    UNIT_CHECK(!bak_autosave_due(100)); /* the first ask starts the clock */
    bak_autosave_zone_entered();
    UNIT_CHECK(bak_autosave_due(100));
    UNIT_CHECK(bak_autosave_due(100)); /* until it is done */
    bak_autosave_done(true, 100);
    UNIT_CHECK(!bak_autosave_due(100));
    clock_spend(2 * MINUTE_NS - 1);
    UNIT_CHECK(!bak_autosave_due(130));
    clock_spend(1);
    UNIT_CHECK(!bak_autosave_due(100)); /* two minutes, the game's time standing still */
    UNIT_CHECK(bak_autosave_due(130));
    bak_autosave_done(true, 130);
    UNIT_CHECK(!bak_autosave_due(160));
    /* The world started again forgets a zone's entry, and the clock starts anew. */
    bak_autosave_zone_entered();
    bak_autosave_reset();
    clock_spend(2 * MINUTE_NS);
    UNIT_CHECK(!bak_autosave_due(200));
    clock_spend(2 * MINUTE_NS);
    UNIT_CHECK(bak_autosave_due(230));
    /* None on the clock, then none at all. */
    UNIT_CHECK(settings_override("saves.autosave_minutes=0", why, sizeof why));
    UNIT_CHECK(!bak_autosave_due(260));
    bak_autosave_zone_entered();
    UNIT_CHECK(bak_autosave_due(260));
    UNIT_CHECK(settings_override("saves.autosave=false", why, sizeof why));
    UNIT_CHECK(!bak_autosave_due(260));
    UNIT_CHECK(!settings_override("saves.autosave_slots=21", why, sizeof why));
    UNIT_CHECK(settings_override("saves.autosave_slots=5", why, sizeof why));
    UNIT_CHECK(bak_autosave_slots() == 5);
    settings_override("saves.autosave=true", why, sizeof why);
    settings_override("saves.autosave_minutes=10", why, sizeof why);
    settings_override("saves.autosave_slots=3", why, sizeof why);
    bak_autosave_reset();
}

/* Held, polls do nothing: no time passes on the virtual clock and nothing runs. */
void unit_autosave_holds_the_machine_still(void) {
    clock_use_virtual(3000000000ull);
    bak_poll_hold();
    bak_poll_hold();
    for (int i = 0; i < 100; i++)
        bak_poll();
    bak_idle();
    bak_poll_release();
    bak_poll();
    UNIT_CHECK(clock_now_ns() == 3000000000ull); /* one hold left */
    bak_poll_release();
    bak_poll();
    UNIT_CHECK(clock_now_ns() > 3000000000ull);
}
