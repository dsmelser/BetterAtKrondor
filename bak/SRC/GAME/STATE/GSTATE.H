/**
 * @file    GSTATE.H
 * @brief   Persistent game state: event flags, timekeeping, party helpers,
 *          and the `TEMP.GAM` swap-file accessors.
 */
#ifndef BAK_GSTATE_H
#define BAK_GSTATE_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"
#include "SRC/GAME/GMAIN.H"


/**
 * @brief Save-region selector for the 1.02 write-region bounds guard in
 * @ref gstate_temp_file_write_at: a caller arms the `TEMP.GAM` region its next
 * write may touch, and a write straying outside it is dropped. @ref TWR_NONE
 * (default and post-write) leaves the write unchecked.
 */
typedef enum TempWriteRegion {
    TWR_NONE               = -1, /**< Unarmed; the write is not bounds-checked. */
    TWR_GAMESTATE          = 0,  /**< The live game-state header. */
    TWR_ENCOUNTER_STATE    = 1,  /**< Mutable encounter/world state: triggers,
                                      rosters, times, moved objects, gold. */
    TWR_COMBAT_ACTOR       = 2,  /**< Scripted combatants' persistent stat sheets. */
    TWR_COMBAT_ACTOR_INNER = 3,  /**< Each combatant's tactical/positional state. */
    TWR_ACTOR_POOL         = 4   /**< Persistent world-object records. */
} TempWriteRegion;


/**
 * @brief The live pending-region latch: the @ref TempWriteRegion the next
 * `TEMP.GAM` write is allowed to touch, or @ref TWR_NONE when disarmed.
 */
extern TempWriteRegion g_pendingTempWriteRegion;


/**
 * @brief Default dialog speaker for each chapter, indexed by chapter - 1.
 *
 * Each entry is a @ref CharacterId slot into @ref GameState::characters — the
 * party member who voices generic lines when no scripted speaker applies.
 */
extern char g_chapterDefaultSpeaker[];


/**
 * @brief Open @ref ResFile token for `TEMP.GAM`, the on-disk game-state
 *        swap image.
 *
 * NULL whenever the file is closed (outside the world loop).
 */
extern ResFile *g_tempGamFP;


/**
 * @brief Per-character fatigue drain — points removed from the combined
 * Health/Stamina reserve each hour the party travels overtired. Indexed by
 * @ref CharacterId. See @ref mech_fatigue.
 */
extern char g_fatigueDrainPerChar[6];

/**
 * @brief Per-character recovery rate — points restored to the combined
 * Health/Stamina reserve on each hour of rest. Indexed by @ref CharacterId.
 * See @ref mech_fatigue.
 */
extern char g_restRegenPerChar[6];

extern unsigned short gstate_event_read(unsigned short event_idx);
extern s16 gstate_event_write(u16 event_id, u16 value);

#define HOTSPOT_SCOUT_TRIED(idx) ((idx) + 5200)
#define HOTSPOT_SCOUTED(idx) ((idx) + 5210)
#define ENCOUNTER_FOUGHT(idx) ((idx) + 5220)
#define SKILL_SELECTED(idx) ((idx) + 6230)
#define SKILL_IMPROVED(idx) ((idx) + 6350)
#define ITEM_USED(idx) ((idx) + 6476)
#define TOWN_VISITED(idx) ((idx) + 6480)
#define MAP_VIEWED(idx) ((idx) + 6500)
#define CONV_TOPIC_INHIBITED(idx) ((idx) + 6700)
#define DOOR_OPEN(idx) ((idx) + 7000)
#define LOCK_PICKED_WITH(idx) ((idx) + 7260)
#define CONDITION(idx) ((idx) + 7320)
#define KEYWORD_ASKED(idx) ((idx) + 7500)
extern s16 gstate_temp_file_open(void);
extern void gstate_temp_file_close(void);
extern s16 gstate_temp_file_read_at(void far *dst_far, s32 offset, u16 bytes);
extern s16 gstate_temp_file_write_at(unsigned char far *src_far, u32 offset, u16 bytes);

/* TEMP.GAM layout: GameState at 0, then EncounterTable, then the keyed
   record regions below, each based at the end of the previous one (after
   the objfixed directory come 0x28 bytes: ten u32 gold stashes, one per
   chapter, SAVEGAME.C). Accessors yield the file
   offset of record idx. The Combatant pool base must fold in long
   (1730 * 0x5f overflows 16-bit arithmetic). */
#ifdef BAK_NATIVE
/* The file keeps the original's 16-bit layout (saves are copies of it), in which a near
   pointer takes 2 bytes: these records are smaller there than in memory, and are read
   and written field by field (gstate_temp_read_gamestate and the others below). */
#define GAM_SIZEOF_GAMESTATE ((size_t)0xad7)
#define GAM_SIZEOF_COMBATANT ((size_t)0x5f)
#define GAM_SIZEOF_COMBATANT_STATE ((size_t)0x16)
extern s16 gstate_temp_read_gamestate(GameState *dst);
extern s16 gstate_temp_write_gamestate(GameState *src);
extern s16 gstate_temp_read_combatant(Combatant *dst, s32 offset);
extern s16 gstate_temp_write_combatant(Combatant *src, u32 offset);
extern s16 gstate_temp_read_combatant_state(CombatantState *dst, s32 offset);
extern s16 gstate_temp_write_combatant_state(CombatantState *src, u32 offset);
extern void gstate_gamestate_unpack(GameState *dst, const unsigned char *disk);
extern void gstate_gamestate_pack(unsigned char *disk, const GameState *src);
extern void gstate_combatant_unpack(Combatant *dst, const unsigned char *disk);
extern void gstate_combatant_pack(unsigned char *disk, const Combatant *src);
extern void gstate_combatant_state_unpack(CombatantState *dst, const unsigned char *disk);
extern void gstate_combatant_state_pack(unsigned char *disk, const CombatantState *src);
#else
#define GAM_SIZEOF_GAMESTATE sizeof(GameState)
#define GAM_SIZEOF_COMBATANT sizeof(Combatant)
#define GAM_SIZEOF_COMBATANT_STATE sizeof(CombatantState)
#endif
#define GAM_ENCOUNTER_TABLE GAM_SIZEOF_GAMESTATE
#define GAM_OBJFIXED_LOCATION(idx)                                                                 \
    ((idx) * sizeof(ObjFixedLocationCacheEntry) + (GAM_ENCOUNTER_TABLE + sizeof(EncounterTable)))
#define GAM_ENC_ROSTER(idx) ((idx) * (7 * sizeof(short)) + (GAM_OBJFIXED_LOCATION(20) + 0x28))
#define GAM_ENC_FOUGHT_TIME(idx) ((idx) * sizeof(u32) + GAM_ENC_ROSTER(700))
#define GAM_ENC_VISITED_TIME(idx) ((idx) * sizeof(u32) + GAM_ENC_FOUGHT_TIME(700))
#define GAM_ENC_OBJ_STATE(idx) ((idx) * sizeof(EncounterObjectState) + GAM_ENC_VISITED_TIME(700))
#define GAM_COMBAT_ACTOR(idx) ((idx) * GAM_SIZEOF_COMBATANT + GAM_ENC_OBJ_STATE(40 * 5 * 7))
#define GAM_COMBAT_ACTOR_INNER(idx) ((idx) * GAM_SIZEOF_COMBATANT_STATE + GAM_COMBAT_ACTOR(1730L))
extern s16 gstate_member_consume_rations(s16 member_slot, s16 a);
extern s16 gstate_advance_time(s32 seconds, s16 flags, s16 recompute_party, s16 a, s16 b);
extern s16 gstate_advance_hours(s16 hours, s16 pad, s16 flags);
extern s16 gstate_is_party_member(s16 actor_id);
extern s16 gstate_actor_is_caster(Combatant *actor);
extern Combatant *gstate_party_member_record(s16 slot);
extern s16 gstate_find_party_slot(Combatant *actor);
extern void gstate_format_money(char *buf, s32 gold, s16 mode);

#endif
