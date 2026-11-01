/* The combat AI's Gift of Sung for an ally under attack (bak/SRC/COMBAT/AI/CBTAI.C): the ally it
 * picks. The original never picks one (docs/PORT_STATUS.md, bugs of the original fixed). */
#include <string.h>

#include "unit.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "defines.h"
#include "SRC/COMBAT/AI/CBTAI.H"
#include "SRC/COMBAT/ARENA/COMBAT.H"
#include "SRC/COMBAT/ENC/CBENC.H"
#include "bak_game_end.h"

static Combatant s_party[3], s_enemies[3];
static CombatantState s_partyState[3], s_enemyState[3];

static void reset(void) {
    memset(s_party, 0, sizeof s_party);
    memset(s_enemies, 0, sizeof s_enemies);
    memset(s_partyState, 0, sizeof s_partyState);
    memset(s_enemyState, 0, sizeof s_enemyState);
    for (int i = 0; i < 3; i++) {
        s_party[i].inner = &s_partyState[i];
        s_enemies[i].inner = &s_enemyState[i];
        /* Full stamina: the share of health alone decides. */
        for (int stat = SKILL_HEALTH; stat <= SKILL_STAMINA; stat++) {
            s_party[i].stats[stat].max = s_enemies[i].stats[stat].max = 100;
            s_party[i].stats[stat].base = s_enemies[i].stats[stat].base = 100;
        }
    }
}

/* An AI turn: the caster's side is the "other" one, its opponents the active one. */
static void sides(Combatant *own, Combatant *opponents) {
    g_pCombatOtherActors = own;
    g_pCombatActiveActors = opponents;
    g_nCombatOtherCount = g_nCombatActiveCount = 3;
}

/* attacker goes for target, whose health is now percent of its maximum. */
static void attack(Combatant *attacker, Combatant *target, int percent) {
    attacker->inner->target = target;
    target->stats[SKILL_HEALTH].base = (unsigned char)percent;
}

void unit_ai_sung_goes_to_an_ally_under_attack(void) {
    Combatant *savedActive = g_pCombatActiveActors, *savedOther = g_pCombatOtherActors;
    s16 savedActiveCount = g_nCombatActiveCount, savedOtherCount = g_nCombatOtherCount;
    Combatant *savedOrigin = g_actors_B_origin;
    g_actors_B_origin = &s_enemies[0];

    /* A party caster's turn (auto-combat). */
    sides(s_party, s_enemies);
    Combatant *caster = &s_party[2];
    reset();
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    attack(&s_enemies[0], &s_party[0], 100);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    attack(&s_enemies[0], &s_party[0], 70);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    attack(&s_enemies[0], &s_party[0], 69);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == &s_party[0]);
    /* Not a dead ally (the original's !flags & 2 meant this flag), not the caster itself. */
    s_partyState[0].flags = CAF_DEAD;
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    s_partyState[0].flags = 0;
    reset();
    attack(&s_enemies[1], caster, 10);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    /* The first opponent's target first, whoever is worse off. */
    attack(&s_enemies[1], &s_party[1], 60);
    attack(&s_enemies[2], &s_party[0], 20);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == &s_party[1]);

    /* An enemy caster's turn: side B's first combatant is helped below 80%. */
    sides(s_enemies, s_party);
    caster = &s_enemies[1];
    reset();
    attack(&s_party[0], &s_enemies[0], 79);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == &s_enemies[0]);
    attack(&s_party[0], &s_enemies[0], 80);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    attack(&s_party[1], &s_enemies[2], 75);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);
    attack(&s_party[1], &s_enemies[2], 69);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == &s_enemies[2]);
    /* The caster's own side is never searched: an ally's target is an opponent. */
    reset();
    attack(&s_enemies[2], &s_party[0], 10);
    UNIT_CHECK(combat_ai_sung_ally_under_attack(caster) == NULL);

    g_pCombatActiveActors = savedActive;
    g_pCombatOtherActors = savedOther;
    g_nCombatActiveCount = savedActiveCount;
    g_nCombatOtherCount = savedOtherCount;
    g_actors_B_origin = savedOrigin;
}
