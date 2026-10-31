#ifndef BAK_GMAIN_H
#define BAK_GMAIN_H

#include "structs.h"

/** Size of the recruitable character pool. */
#define CHARACTER_POOL_SIZE 6

/** Maximum members in the active/traveling party. */
#define MAX_ACTIVE_PARTY 3

/**
 * @brief Fixed identity of each @ref GameState::characters pool slot.
 *
 * The slot-to-character binding never changes at runtime, so these indices
 * name the six party members directly. Dialog speaker ids encode a slot as
 * slot+1 (1..6), reserving 0 for the narrator.
 */
typedef enum CharacterId {
    CHR_LOCKLEAR = 0, /**< Squire Locklear. */
    CHR_GORATH   = 1, /**< Gorath, the Moredhel. */
    CHR_OWYN     = 2, /**< Owyn, magician. */
    CHR_PUG      = 3, /**< Pug, magician. */
    CHR_JAMES    = 4, /**< James (Jimmy the Hand). */
    CHR_PATRUS   = 5  /**< Patrus, magician. */
} CharacterId;

typedef struct GameState {
    u16 nChapter;
    s32 nParty_gold;
    u32 game_time;
    u32 dwLastActionTimeSnapshot;
    unsigned char bCombatExitRequest;
    unsigned char nWorldLoopExitRequest;
    unsigned char rsvd_10[1];
    unsigned char nPrevZoneId;
    unsigned char nZoneId;
    unsigned char nPlayerTileX;
    unsigned char nPlayerTileY;
    WorldPos2 zoneDefaultCameraPos;
    unsigned char rsvd_1d[4];
    u16 wZoneDefaultCameraHeading;
    unsigned char abTeleportRecord[11];
    s16 nLastSeenStepSpeed;
    s16 nLastSeenGridStride;
    s16 nWorldStepPending;
    unsigned char nWorldStepTickCount;
    u16 world_step_tick;
    s32 lInsetCameraPosZ;
    char characterNames[CHARACTER_POOL_SIZE][10];

    /**
     * The six recruitable characters' persistent @ref Combatant records,
     * indexed by character id (0..5). The active traveling subset is chosen
     * by @ref GameState::activeParty; each record is copied to and from the
     * combat grid at combat start and end.
     */
    Combatant characters[CHARACTER_POOL_SIZE];

    /**
     * Number of members in the active/traveling party — the count of
     * valid @ref GameState::activeParty entries (0..3), not the 6-member
     * character pool (@ref GameState::characters).
     */
    char partySize;

    /**
     * Pool indices (0..5, into @ref GameState::characters) of the active
     * traveling party; @ref GameState::partySize entries are valid, and 0xFF
     * marks an empty slot.
     */
    char activeParty[MAX_ACTIVE_PARTY];

    struct Actor far *shared_inventory;
    struct Actor far *ground_pile;
    unsigned char bPartyDirtyFlags;
    u16 dwPopup_retry_state;
    u16 aSkillTrainRate[CHARACTER_POOL_SIZE];
    unsigned char abActorStatusRanks[CHARACTER_POOL_SIZE][7];
    ActorStatModTable aActorStatModifiers;
    s16 nEvtArgActor0;
    s16 nEvtArgActor1;
    s16 nEvtArgStat;
    unsigned char rsvd_59c[2];
    s16 nEvtArgCount;
    s16 nEvtArgAux1;
    s16 nEvtArgItemId;
    s16 nEvtArgDlgResult;
    s32 lEvtArgGoldCost;
    s32 lEvtArgValue;
    s32 lEvtArgAuxValue;
    s16 nTimerEventPoolCount;
    TimerEventEntry aTimerEventPool[20];
    u16 wPalEventMask;
    s16 nSpellMenuCasterSlot;
    s16 nSpellMenuPreselect;
    s16 nPalFadeDirty;
    s16 nPrevWorldBrightness;
    s16 world_brightness;
    s16 nPalDaynightDelta;
    s16 nPalAreaBrightness;
    s16 nPalBrightnessBoost;
    s16 nPalBlendWeight;
    ObjFixedLocationCacheEntry objfixed_location_cache;
    unsigned char event_bitmap_lo[1063];
    unsigned char event_bitmap_hi[50];
} GameState;

extern u32 g_nVec3DistResult;
extern PolyRasterState g_polyRasterState;
extern unsigned char g_bCutsceneEscPressed;
extern unsigned char g_bssgap_4863[3];
extern GameState g_gameState;
extern u16 g_bPaletteCycleEbActive;
extern u16 g_wVgaScratchPageBase;
extern u16 g_wGameFontSlot;
extern u16 *g_pSkyPanoAssetTable;
#ifdef V102CD
extern s16 g_cd_present;
#endif
extern WorldEntity *g_render_camera_scratch;
extern ViewContext *g_active_window;
extern u16 g_nChapterAtLoopExit;

extern s16 gmain_start_dispatch(s16 mode);
extern void gmain_play_intro_animation(void);
extern s16 gmain_play_chapter_intro(s16 chapter, s16 part);
extern s16 gmain_play_chapter_cutscene(s16 chapter_num, s16 channel_1, s16 channel_2);
extern void gmain_screen_fade_to_black(void);
extern void gmain_cutsc_play_fullmap_scene(s16 scene_index);
extern void gmain_restart_from_save_flow(void);
extern void gmain_parse_cmdline_sound_flag(s16 argc, char **argv);


#endif
