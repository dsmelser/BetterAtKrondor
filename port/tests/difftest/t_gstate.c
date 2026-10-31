/*
 * GAME/STATE/GSTATE.C's native serializer: the TEMP.GAM records that hold near pointers,
 * converted between the file's 16-bit layout and the native structs. Each command unpacks
 * a record, reports named fields of the native struct (the compiler places them), then
 * packs it again.
 */
#include <stddef.h>
#include <string.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GAME/GMAIN.H"
#include "SRC/GAME/STATE/GSTATE.H"
#include "bak_game_end.h"

static void put_ptr(DtOut *out, const void *p) {
    dt_put_u32(out, (uint32_t)(uintptr_t)p);
}

/* An address the pack must not write: in the game the pointers hold real ones. */
#define LIVE_POINTER ((void *)(uintptr_t)0x12345678u)

static void set_pointers(Combatant *c) {
    c->name = LIVE_POINTER;
    c->actor_record = LIVE_POINTER;
    c->inner = LIVE_POINTER;
}

static void put_combatant(DtOut *out, const Combatant *c) {
    dt_put_bytes(out, c->spellsKnown, sizeof c->spellsKnown);
    dt_put_bytes(out, c->stats, sizeof c->stats);
    dt_put_u8(out, (uint8_t)c->charSlot);
    put_ptr(out, c->name);
    put_ptr(out, c->actor_record);
    put_ptr(out, c->inner);
}

/*
 * gstate.gamestate <0xad7 bytes> -> nChapter (2), nParty_gold (4), characterNames (60),
 * each character's fields (put_combatant), partySize, activeParty (3), shared_inventory and
 * ground_pile (pointers, 4 each), bPartyDirtyFlags, aSkillTrainRate (12), the last byte of
 * event_bitmap_hi; then the record packed again (0xad7 bytes), its pointers set first.
 */
static void cmd_gamestate(DtIn *in, DtOut *out) {
    static GameState g;
    unsigned char disk[0xad7];
    memset(&g, 0xA5, sizeof g);
    gstate_gamestate_unpack(&g, dt_bytes(in, sizeof disk));
    dt_put_u16(out, g.nChapter);
    dt_put_u32(out, (uint32_t)g.nParty_gold);
    dt_put_bytes(out, g.characterNames, sizeof g.characterNames);
    for (int i = 0; i < CHARACTER_POOL_SIZE; i++)
        put_combatant(out, &g.characters[i]);
    dt_put_u8(out, (uint8_t)g.partySize);
    dt_put_bytes(out, g.activeParty, sizeof g.activeParty);
    put_ptr(out, g.shared_inventory);
    put_ptr(out, g.ground_pile);
    dt_put_u8(out, g.bPartyDirtyFlags);
    dt_put_bytes(out, g.aSkillTrainRate, sizeof g.aSkillTrainRate);
    dt_put_u8(out, g.event_bitmap_hi[sizeof g.event_bitmap_hi - 1]);
    for (int i = 0; i < CHARACTER_POOL_SIZE; i++)
        set_pointers(&g.characters[i]);
    g.shared_inventory = LIVE_POINTER;
    g.ground_pile = LIVE_POINTER;
    gstate_gamestate_pack(disk, &g);
    dt_put_bytes(out, disk, sizeof disk);
}

/* gstate.combatant <0x5f bytes> -> put_combatant, then the record packed again */
static void cmd_combatant(DtIn *in, DtOut *out) {
    Combatant c;
    unsigned char disk[0x5f];
    memset(&c, 0xA5, sizeof c);
    gstate_combatant_unpack(&c, dt_bytes(in, sizeof disk));
    put_combatant(out, &c);
    set_pointers(&c);
    gstate_combatant_pack(disk, &c);
    dt_put_bytes(out, disk, sizeof disk);
}

/*
 * gstate.state <0x16 bytes> -> target (pointer, 4), creatureType (2), flags, statusHead (2),
 * dmgFloatFrames; then the record packed again
 */
static void cmd_state(DtIn *in, DtOut *out) {
    CombatantState s;
    unsigned char disk[0x16];
    memset(&s, 0xA5, sizeof s);
    gstate_combatant_state_unpack(&s, dt_bytes(in, sizeof disk));
    put_ptr(out, s.target);
    dt_put_u16(out, (uint16_t)s.creatureType);
    dt_put_u8(out, s.flags);
    dt_put_u16(out, (uint16_t)s.statusHead);
    dt_put_u8(out, s.dmgFloatFrames);
    s.target = LIVE_POINTER;
    gstate_combatant_state_pack(disk, &s);
    dt_put_bytes(out, disk, sizeof disk);
}

const DtEntry dt_gstate_entries[] = {
    DT_COMMAND("gstate.gamestate", cmd_gamestate),
    DT_COMMAND("gstate.combatant", cmd_combatant),
    DT_COMMAND("gstate.state", cmd_state),
    DT_END,
};
