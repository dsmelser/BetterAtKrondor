/**
 * @file  structs.h
 * @brief Engine-wide struct, enum, and typedef definitions.
 */
#ifndef BAK_STRUCTS_H
#define BAK_STRUCTS_H
#include "gtypes.h"
#include <stdio.h>

enum CreatureType {
    CREATURE_GORATH = 15,
    CREATURE_OWYN = 16,
    CREATURE_LOCKLEAR = 17,
    CREATURE_MOREDHEL_WARRIOR = 18,
    CREATURE_BRAK_NURR = 19,
    CREATURE_MOREDHEL_SPELLCASTER = 21,
    CREATURE_BLACK_SLAYER = 22,
    CREATURE_NIGHTHAWK = 23,
    CREATURE_ROGUE = 24,
    CREATURE_PANTATHIAN = 25,
    CREATURE_PANTH_TIANDN = 26,
    CREATURE_SPELLWEAVER = 27,
    CREATURE_SERVITOR_OF_LIMS_KRAGMA = 28,
    CREATURE_TOR_GIANT = 29,
    CREATURE_ROGUE_MAGE = 30,
    CREATURE_DEEP_GIANT = 31,
    CREATURE_CAVE_GIANT = 32,
    CREATURE_RIME_GIANT = 33,
    CREATURE_SENTINEL_OGRE = 34,
    CREATURE_HIGHLAND_OGRE = 37,
    CREATURE_SCORPION = 39,
    CREATURE_BULLDRAKE_WYVERN = 41,
    CREATURE_GRANDSIRE_WYVERN = 42,
    CREATURE_HATCHLING_WYVERN = 43,
    CREATURE_SPIDER = 44,
    CREATURE_PUG = 45,
    CREATURE_BEASTHOUND = 46,
    CREATURE_PATRUS = 47,
    CREATURE_TROLL = 48,
    CREATURE_DREAD = 49,
    CREATURE_JAMES = 51,
    CREATURE_WITCH_HAG = 52,
    CREATURE_GOBLIN = 53,
    CREATURE_WIND_ELEMENTAL = 54,
    CREATURE_QUEGIAN_PIRATE = 55,
    CREATURE_RUSALKI = 56,
    CREATURE_SHADE = 57,
    CREATURE_NETHERMANDER = 58,
    CREATURE_GREAT_ONE = 61,
};
typedef enum CreatureType CreatureType;
enum ScreenHeightPx {
    SCREEN_H_VGA = 200,
    SCREEN_H_NEW = 320,
};
typedef enum ScreenHeightPx ScreenHeightPx;
enum ScreenWidthPx {
    SCREEN_W_VGA = 320,
    SCREEN_W_NEW = 640,
};
typedef enum ScreenWidthPx ScreenWidthPx;
enum SkillType {
    SKILL_HEALTH = 0,
    SKILL_STAMINA = 1,
    SKILL_SPEED = 2,
    SKILL_STRENGTH = 3,
    SKILL_DEFENSE = 4,
    SKILL_CROSSBOW = 5,
    SKILL_MELEE = 6,
    SKILL_CASTING = 7,
    SKILL_ASSESSMENT = 8,
    SKILL_ARMORCRAFT = 9,
    SKILL_WEAPONCRAFT = 10,
    SKILL_BARDING = 11,
    SKILL_HAGGLING = 12,
    SKILL_LOCKPICK = 13,
    SKILL_SCOUTING = 14,
    SKILL_STEALTH = 15,
};
typedef enum SkillType SkillType;
enum VideoMode {
    VIDEO_MODE_CGA = 1,
    VIDEO_MODE_EGA = 2,
    VIDEO_MODE_TAN = 3,
    VIDEO_MODE_HER = 4,
    VIDEO_MODE_MCG = 5,
    VIDEO_MODE_BAD = 6,
    VIDEO_MODE_EVA = 7,
    VIDEO_MODE_VGA = 8,
    VIDEO_MODE_EVG = 9,
    VIDEO_MODE_HVG = 10,
    VIDEO_MODE_HEG = 11,
    VIDEO_MODE_NEW = 12,
};
typedef enum VideoMode VideoMode;

typedef void *__gnuc_va_list;
typedef u16 __mode_t;
typedef s16 __ssize_t;
typedef u16 CombatResCacheCol[8][3];
typedef s16 intptr_t;
#ifndef _SIZE_T
#define _SIZE_T
typedef u16 size_t;
#endif
typedef __ssize_t ssize_t;

typedef struct ActSpriteCache ActSpriteCache;
typedef struct Rect Rect;
typedef struct ViewContext ViewContext;
typedef struct ActorStatModifier ActorStatModifier;
typedef struct AnimSlot AnimSlot;
typedef struct AudFragNode AudFragNode;
typedef struct AudioListNode AudioListNode;
typedef struct AudioTrackHandle AudioTrackHandle;
typedef struct BmxHeader BmxHeader;
typedef struct BookImage BookImage;
typedef struct BookPage BookPage;
typedef struct BookViewerState BookViewerState;
typedef struct BorlandFarPtr BorlandFarPtr;
typedef struct WorldPos WorldPos;
typedef struct CircObj CircObj;
typedef struct SpriteBillboard SpriteBillboard;
typedef struct CodecVtable CodecVtable;
typedef struct Combatant Combatant;
typedef struct CombatantState CombatantState;
typedef struct CombatTile CombatTile;
typedef struct CombatantData CombatantData;
typedef struct ConditionInfo ConditionInfo;
typedef struct CursorState CursorState;
typedef struct DDXRecord DDXRecord;
typedef struct DdxChoice DdxChoice;
typedef struct DdxOp DdxOp;
typedef struct DefTrapRecord DefTrapRecord;
typedef struct Dialog Dialog;
typedef struct DialogStyleDescriptor DialogStyleDescriptor;
typedef struct DialogWidget DialogWidget;
typedef struct EncounterActorAux EncounterActorAux;
typedef struct EncounterActorTemplate EncounterActorTemplate;
typedef struct EncounterAnimScratch EncounterAnimScratch;
typedef struct EncounterObjectPose EncounterObjectPose;
typedef struct EncounterObjectState EncounterObjectState;
typedef struct EncounterRecordTemplate EncounterRecordTemplate;
typedef struct EncounterTable EncounterTable;
typedef struct EnginePrefs EnginePrefs;
typedef struct EulerAngles EulerAngles;
typedef struct FontSlotMetrics FontSlotMetrics;
typedef struct GamePositionAndHeading GamePositionAndHeading;
typedef struct GraphicsContext GraphicsContext;
typedef struct GraphicsContextRender GraphicsContextRender;
typedef struct GridCombatant GridCombatant;
typedef struct IffResLevel IffResLevel;
typedef struct IffResReader IffResReader;
typedef struct ImageRecord ImageRecord;
typedef struct ItemRecord ItemRecord;
typedef struct ItemSlot ItemSlot;
typedef struct ListWidget ListWidget;
typedef struct Mat3x3 Mat3x3;
typedef struct MenuEntry MenuEntry;
typedef struct MenuPage MenuPage;
typedef struct MeshPartRecord MeshPartRecord;
typedef struct Shape Shape;
typedef struct MusicChunkEntry MusicChunkEntry;
typedef struct MusicChunkHeader MusicChunkHeader;
typedef struct OLD_IMAGE_DOS_HEADER OLD_IMAGE_DOS_HEADER;
typedef struct OLD_IMAGE_DOS_RELOC OLD_IMAGE_DOS_RELOC;
typedef struct ObjFixedDiskHeader ObjFixedDiskHeader;
typedef struct ObjFixedLocationCacheEntry ObjFixedLocationCacheEntry;
typedef struct ObjFixedTemplateBuffer ObjFixedTemplateBuffer;
typedef struct PageDirectory PageDirectory;
typedef struct PagedArrayEntry PagedArrayEntry;
typedef struct PerspDiv2Args PerspDiv2Args;
typedef struct PerspDiv2Result PerspDiv2Result;
typedef struct PlayerSpawnRecord PlayerSpawnRecord;
typedef struct PolyEdge PolyEdge;
typedef struct PolyRasterState PolyRasterState;
typedef struct ProximityPolygon ProximityPolygon;
typedef struct ProximityRecord ProximityRecord;
typedef struct ProximityScanHit ProximityScanHit;
typedef struct ProximityVertex ProximityVertex;
typedef struct ProximityZone ProximityZone;
typedef struct ProximityZoneSettings ProximityZoneSettings;
typedef struct R3dRendererState R3dRendererState;
typedef struct RendererVtable RendererVtable;
typedef struct ResourceRemapDesc ResourceRemapDesc;
typedef struct ScriptAnimNode ScriptAnimNode;
typedef struct ScriptBlock ScriptBlock;
typedef struct ScriptObject ScriptObject;
typedef struct SfxLoopPri SfxLoopPri;
typedef struct SkyStrip SkyStrip;
typedef struct Slot Slot;
typedef struct SpellDef SpellDef;
typedef struct SpellDocRow SpellDocRow;
typedef struct SpellSymbolRecord SpellSymbolRecord;
typedef struct StatModPayload StatModPayload;
typedef struct StatSlot StatSlot;
typedef struct StatusEffectSlot StatusEffectSlot;
typedef struct StreamDesc StreamDesc;
typedef struct TileMoveRecord TileMoveRecord;
typedef struct TimerCallback TimerCallback;
typedef struct TimerEventEntry TimerEventEntry;
typedef struct TimerSched TimerSched;
typedef struct TownScene TownScene;
typedef struct TownSceneActor TownSceneActor;
typedef struct Vec3Long Vec3Long;
typedef struct Vec3Short Vec3Short;
typedef struct VfxRec VfxRec;
typedef struct VisibleEntryList VisibleEntryList;
typedef struct WorldObject WorldObject;
typedef struct WorldEntity WorldEntity;
typedef struct WorldHotspot WorldHotspot;
typedef struct WorldPos2 WorldPos2;
typedef struct ZoneEntryRecord ZoneEntryRecord;
typedef struct ZoneHotspot ZoneHotspot;
typedef union StreamDescSrcUnion StreamDescSrcUnion;

typedef u16(far *AudioDriverDispatchFn)(u16 cmd, u16 data);
typedef void(far *BlitChunkyFn)(unsigned char *src_off, u16 src_seg, s16 dst_x,
                                s16 dst_y, s16 width_bytes);
typedef void(far *BlitSpriteFn)(s16 sprite_desc_offset, s16 dst_x, s16 dst_y);
typedef void(far *BlitSpriteSimpleFn)(ImageRecord *sprite, s16 x, s16 y);
typedef void(far *CirclePlotterFn)(s16 x, s16 y);
typedef void(far *CombatAiActionFn)(Combatant far *actor);
typedef void(far *CrtExitFn)(void);
typedef void(far *DefaultStubFn)(void);
typedef void(far *EncounterAiActionFn)(Combatant *actor);
typedef void(far *EncounterAiTurnFn)(Combatant *actor);
typedef void(far *FARFN)(void);
typedef u16(far *GetpixelFn)(s16 x, s16 y);
typedef u32(far *ImageInstallFn)(u16 *image_descs_table,
                                           u16 *out_status);
typedef void(far *PasteRectFn)(void far *src, s16 x, s16 y, s16 width, s16 height);
typedef void(far *PresentFn)(s16 x, s16 y, s16 w, s16 h);
typedef void(far *PutpixelFn)(s16 x, s16 y, s16 color);
typedef s32(far *RectByteSizeFn)(s16 width, s16 height);
typedef void(far *SetBorderColorFn)(s16 color);
typedef void(far *Slot01Fn)(void);
typedef void(far *Slot02Fn)(void);
typedef void(far *Slot04Fn)(u16 x_start, u16 y_start, u16 width,
                            u16 height);
typedef void(far *Slot05Fn)(void far *dst_buffer, u16 x_start, u16 y_start,
                            u16 width, u16 height);
typedef void(far *Slot07Fn)(void far *src_buffer, u16 x_start, u16 y_start,
                            u16 width, u16 height);
typedef void(far *Slot10Fn)(void);
typedef void(far *Slot11Fn)(void);
typedef void(far *Slot14Fn)(u16 far *image_descs_table, u16 dst_off,
                            u16 dst_seg, u16 src_off, u16 src_seg);
typedef void(far *Slot18Fn)(u16 src_off, u16 src_seg, u16 dst_x,
                            u16 dst_y, u16 width_bytes,
                            u16 height);
typedef void(far *Slot19Fn)(void);
typedef void(far *Slot20Fn)(u16 palette_off, u16 palette_seg);
typedef void(far *Slot23Fn)(void);
typedef void(far *Slot24Fn)(s16 x_pixels, s16 y_row);
typedef void(far *Slot25Fn)(void);
typedef void(far *Slot27Fn)(void);
typedef void(far *Slot29Fn)(u16 sprite_desc_offset, s16 x, s16 y,
                            u16 flip_flags);
typedef void(far *Slot30Fn)(u16 src_off, u16 src_seg,
                            u16 xy_packed);
typedef void(far *Slot31Fn)(u16 desc_list_offset);
typedef void(far *Slot32Fn)(void);
typedef void(far *Slot33Fn)(u16 sprite_desc_offset, s16 x, s16 y);
typedef void(far *Slot34Fn)(u16 start_idx, u16 count, u16 dst_idx,
                            u16 weight);
typedef void(far *Slot35Fn)(unsigned char far *rgb_triplets, u16 start_index,
                            u16 count);
typedef void(far *Slot36Fn)(void);
typedef void(far *Slot37Fn)(void);
typedef void(far *Slot38Fn)(void);
typedef void(far *Slot39Fn)(void);
typedef void(far *Slot40Fn)(void);
typedef s16(far *SpriteRegionSizeFn)(u32 sprite_descriptor);
typedef void(far *UnimplementedSlotFn)(void);
typedef void(far *UnpackNibblesToPlanesFn)(unsigned char far *src, unsigned char far *dst,
                                           u16 count);
typedef void(far *VsyncWaitFn)(s16 wait_flag);

/** Default focal exponent for a full-screen view: 1 << zoom is the view-plane distance in px. */
#define VIEW_ZOOM_DEFAULT 7

struct EulerAngles {
    s16 pitch;
    s16 roll;
    s16 yaw;
};

/* Screen-space rectangle: origin (x, y) plus extent (width, height). */
struct Rect {
    s16 x;
    s16 y;
    s16 width;
    s16 height;
};

/* Clip rectangle saved/restored around a world-effect draw.  Member order is
   (xmin, xmax, ymax, ymin), not the canonical xmin/xmax/ymin/ymax order. */
typedef struct SavedClipRect {
    s16 xmin;
    s16 xmax;
    s16 ymax;
    s16 ymin;
} SavedClipRect;

/* Canonical active clip rectangle (xmin/xmax/ymin/ymax) embedded in the
   graphics context; all four bounds are inclusive screen coordinates. */
typedef struct ClipRect {
    s16 xmin;
    s16 xmax;
    s16 ymin;
    s16 ymax;
} ClipRect;

/* Tile-space axis-aligned bounding box (byte coordinates).  Member order
   matches the on-disk hotspot record (min_x, max_y, max_x, min_y) rather
   than the canonical order; do not reorder. */
typedef struct TileBBox {
    unsigned char minX;
    unsigned char maxY;
    unsigned char maxX;
    unsigned char minY;
} TileBBox;

/* Absolute rectangle: two corners — top-left (x0,y0) and bottom-right (x1,y1), absolute coords, not extents. */
typedef struct AbsRect {
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;
} AbsRect;

struct ViewContext {
    s16 zoom;
    WorldEntity *camera;
    EulerAngles viewRotation;
    Rect viewport;
    unsigned char *
        faceMaterials; /* fill-color -> material-id table for the face-shading path; never set by the shipped game */
    unsigned char *colorRemap;
};

struct WorldPos2 {
    s32 nWorld_x;
    s32 nWorld_y;
};

struct StatModPayload {
    u16 wStatMask;
    s16 nValue;
    u32 dwTApply;
    u32 dwTExpiry;
};

struct ActorStatModifier {
    u16 wMaskFlags;
    StatModPayload payload;
};

struct ActSpriteCache {
    u16 pActorId[6];
    u16 pAssetTable[6];
    unsigned char far *pChunk[6];
};

struct AnimSlot {
    ImageRecord ***sprite_header;
    u16 sprite_id;
    s16 facing;
    s16 current_frame;
    s16 end_frame;
    s16 done_flag;
    u16 loop_period;
    u16 tick_counter;
    unsigned char reverse_flag;
};

struct AudFragNode {
    u16 wKey;
    u16 wSize;
    AudFragNode far *next;
};

struct AudioListNode {
    AudioListNode far *pNext;
    unsigned char far *pData;
    u16 wSortKey;
    u16 wId;
    unsigned char bPriority;
    unsigned char filler_d;
    AudioTrackHandle far *pHandle;
    u16 wFlags;
};

struct AudioTrackHandle {
    unsigned char pHeader[8];
    unsigned char far *pSubBuffer;
    unsigned char pData[332];
    unsigned char bActive;
    unsigned char pRsvd159[3];
    unsigned char bPriority;
    unsigned char bLoop;
    unsigned char bVolume;
    unsigned char pRsvd15f[7];
    unsigned char far *pScript;
    unsigned char far *pPosition;
    unsigned char abRsvd16e[4];
    AudioTrackHandle far *pNext;
    unsigned char abRsvd176[4];
};

struct BmxHeader {
    u16 wMagic;
    u16 wCompression;
    u16 wImageCount;
    u16 wCompressedSize;
    u32 dwDecompressedSize;
};

struct BookImage {
    s16 nX;
    s16 nY;
    u16 wImage;
    u16 wMirroring;
};

struct BookPage {
    Rect rect;
    u16 wDisplayNumber;
    u16 wPageNumber;
    u16 wPrevPageNumber;
    u16 wNextPageNumber;
    u16 wPagePointer;
    u16 w_pad12;
    u16 wImageCount;
    u16 wReservedCount;
    u16 wShowPageNumber;
    unsigned char pReserved[30];
};

/* wReservedCount text-exclusion rectangles follow the page header in the
 * BOK data: text runs flow around these (illustration areas). */
typedef AbsRect BookReservedRect;

struct BookViewerState {
    s16 nMarginTop;
    s16 nMarginBottom;
    s16 nLineHeight;
    s16 nVStepIndent;
    s16 nVStepWrap;
    s16 nVStepRetract;
    s16 nVStepUndo;
    u16 wAlignment;
    s16 nFontSlot;
    s16 nBaselineYOff;
    s16 nFgColor;
    s16 nBgColor;
    u16 wStyleFlags;
    s16 nTerminate;
    s16 nCursorX;
    s16 nLineY1;
    s16 nLineYNext;
    s16 nLineHeightActual;
    s16 nLineWidthCur;
    s16 nJustifyOn;
    s16 nSpaceExtra;
    s16 nSpaceExtraStep;
    s16 nSpaceExtraCount;
    s16 nCharExtra;
    s16 nCharExtraStep;
    s16 nCharExtraCount;
    unsigned char huge *pCurChar;
    unsigned char huge *pBreakSave;
    unsigned char far *pResumeSave;
    s16 pFontSlots[10];
    ImageRecord **pImageRecord;
    unsigned char far *pPalette;
    unsigned char p_pad5a[6];
    s16 nEmsPage0;
    s16 nEmsPage1;
};

struct BorlandFarPtr {
    u16 offset;
    u16 segment;
};

struct WorldPos {
    WorldPos2 xy;
    s32 nWorld_z;
};

struct CircObj {
    s16 nRadiusSrc;
    unsigned char bVertexIdx;
    unsigned char bDayColor;
    unsigned char bNightColor;
};

/* Tag-2 payload of a poly-list block (sibling of CircObj); consumed by
 * worldrender_sprite_billboard, which reads it by raw offset (the +2 word is
 * punned into the yAdjust/xOffset byte pair). */
struct SpriteBillboard {
    s16 spriteIndex;         /* +0 sprite id: paged-array index or sprite-bank index */
    unsigned char yAdjust;     /* +2 vertical draw offset (scaled) */
    unsigned char xOffset;     /* +3 horizontal draw offset (scaled, flip-aware) */
    unsigned char scaleFactor; /* +4 per-sprite scale vs projScale (0 = use projScale) */
    unsigned char vertexIdx;   /* +5 mesh vertex whose world position anchors the sprite */
};

struct CodecVtable {
    u16 wBuf_size;
    u16 wScratch_size_read;
    u16 wScratch_size_write;
    void *pRead_chunk;
    void *pWrite_chunk;
    void *pOpen_write;
    void *pOpen_read;
};

struct StatSlot {
    unsigned char max;
    unsigned char base;
    unsigned char cached;
    unsigned char frac;
    char perm_mod;
};

struct Combatant {
    char *name;
    u16 spellsKnown[3];
    StatSlot stats[16];
    char charSlot;
    struct Actor far *actor_record;
    CombatantState *inner;
};

struct CombatantState {
    Combatant *target;
    s16 creatureType;
    char gridX;
    char gridY;
    unsigned char destX;
    unsigned char destY;
    unsigned char flags;
    unsigned char knockbackFrame;
    s16 statusHead;
    unsigned char pad_c;
    unsigned char knockbackTimer;
    unsigned char morale;
    unsigned char aiTurnProfile;
    unsigned char aiEncounterProfile;
    unsigned char aiPathProfile;
    unsigned char selectedMenuCmd;
    unsigned char spellSchool;
    char dmgFloatValue;
    unsigned char dmgFloatFrames;
};

struct GamePositionAndHeading {
    s32 nX;
    s32 nY;
    u16 wHeading_raw;
};

struct CombatantData {
    u16 wMonster_index;
    u16 wMovement_type;
    GamePositionAndHeading pos;
    u32 dwField0x0e;
    u32 dwField0x12;
    unsigned char pPad0[8];
    u32 dwField0x1e;
    u32 dwField0x22;
    unsigned char pPad1[10];
};

struct CombatTile {
    Combatant *pOccupant;
    u16 wTerrain;
    s16 nEffectTimer;
};

struct ConditionInfo {
    char pName[11];
    s16 nStatDelta;
    s16 nRegenDelta;
    s16 pExtra[4];
};

struct CursorState {
    s16 nX;
    s16 nY;
    s16 nHotX;
    s16 nHotY;
    s16 nWidth;
    s16 nHeight;
    u16 shape;
};

struct DdxChoice {
    u16 wCond;
    u16 wRange_min_or_mask;
    u16 wRange_max_or_chapbits;
    u32 dwTarget_key;
};

struct DdxOp {
    u16 wOp;
    s16 nA1;
    s16 nA2;
    s16 nA3;
    s16 nA4;
};

struct DDXRecord {
    unsigned char bStyle;
    u16 wSpeaker_id;
    u16 wFlags;
    unsigned char bCnt1;
    unsigned char bCnt2;
    u16 wBody_len;
};

struct DefTrapRecord {
    u16 wGap0;
    u32 dwCombat_index;
    u32 dwEntry_dialog;
    u32 dwScout_dialog;
    u32 dwZero;
    GamePositionAndHeading landing_dir1;
    GamePositionAndHeading landing_dir2;
    GamePositionAndHeading landing_dir4;
    GamePositionAndHeading landing_dir8;
    GamePositionAndHeading landing_primary;
    unsigned char bNum_enemies;
    CombatantData pCombatants[7];
    u16 wUnknown_u16;
    u16 wIs_ambush;
};

struct Dialog {
    DialogWidget **pWidgets;
    s16 nWidget_capacity;
    s16 nWidget_count;
    s16 nFocused_idx;
};

struct DialogStyleDescriptor {
    unsigned char header[12];
    unsigned char applied_state[8];
};

struct DialogWidget {
    char *pText_buf;
    char *pLabel;
    s16 nCapacity;
    s16 nLength;
    s16 nCursor;
    s16 nFull_length;
    Rect rect;
    s16 nLabel_x;
    s16 nLabel_y;
    unsigned char bBg_color;
    unsigned char bFg_color;
    unsigned char bCursor_color;
    unsigned char bLabel_fg;
    unsigned char bSelection_color;
    unsigned char far *pFill_image;
};

struct EncounterActorAux {
    u16 wReserved_0;
    s16 nRecord_idx;
    s16 nSlot_idx;
    s16 nEncounter_idx;
    u16 wReserved_8;
};

struct EncounterActorTemplate {
    unsigned char kind;
    s16 nPaged_record_delta;
    s16 nVariant;
    s32 nWorld_x_local;
    s32 nWorld_y_local;
    u16 facing;
    s32 pPoly_x[4];
    s32 pPoly_y[4];
    unsigned char slack_2f;
};

struct EncounterAnimScratch {
    unsigned char bSpriteDirFrame;
    unsigned char bSpriteDir;
    s16 nRecordTemplateIdx;
    s16 nActorSlot;
    s16 nEncObjStateIdx;
    unsigned char p_fill[2];
};

struct EncounterObjectPose {
    s32 nWorld_x_offset;
    s32 nWorld_y_offset;
    s16 nFacing;
};

struct EncounterObjectState {
    EncounterObjectPose pose;
    u16 wKind_state;
};

struct EncounterRecordTemplate {
    EncounterActorTemplate pActors[7];
    unsigned char pPadding[3];
};

struct EncounterTable {
    unsigned char xCoord[40];
    unsigned char yCoord[40];
    unsigned char zCoord[40];
    unsigned char flags[40][38];
};

struct EnginePrefs {
    unsigned char step_speed;
    unsigned char combat_step_speed;
    unsigned char detail_level;
    unsigned char text_speed;
    unsigned char flags;
};

struct FontSlotMetrics {
    unsigned char stride[20];
    unsigned char height[20];
    unsigned char base_char[20];
    unsigned char glyph_count[20];
};

typedef ActorStatModifier ActorStatModRow[8];
typedef ActorStatModRow ActorStatModTable[6];
struct TimerEventEntry {
    unsigned char bKind;
    unsigned char bMode_at_insert;
    u16 wSub_id;
    s32 nValue;
};

struct ObjFixedLocationCacheEntry {
    u16 location_id;
    unsigned char pUnk_2[4];
    u32 temp_gam_offset;
    u32 objfixed_offset;
    unsigned char pUnk_14[8];
};

struct GraphicsContext {
    unsigned char bText_fg_color;
    unsigned char bText_bg_color;
    unsigned char bText_style_flags;
    unsigned char bClip_enabled;
    ClipRect clip;
    unsigned char bGfx_fill_enabled;
    unsigned char bGfx_fill_color;
    unsigned char bGfx_outline_color;
    unsigned char bGfx_dither_color;
    u16 wVgaPage1Base;
    u16 wVgaPage2Base;
    u16 wVgaFrontPageBase;
    u16 wGfxBlitSrcPage;
    u16 wGfxBlitDstPage;
    unsigned char pReserved_0x1a[2];
    unsigned char bBiosCompatible;
    unsigned char bVideoAdapter;
    unsigned char bReserved_0x1e;
    unsigned char bGfxRenderStateFlag;
    unsigned char bReserved_0x20;
    unsigned char bActiveVgaMode;
    unsigned char pReserved_0x22[18];
    unsigned char pFont_glyph_width_bits[20];
    unsigned char pFont_height[20];
    unsigned char pFont_base_char[20];
    unsigned char pFont_glyph_count[20];
    unsigned char pReserved_0x84[40];
    s16 pPolygonX[20];
    s16 pPolygonY[20];
    s16 pPolygonXClipped[20];
    s16 pPolygonYClipped[20];
    s16 pPolygonXSaved[20];
    s16 pPolygonYSaved[20];
    s16 nPolygonClipVertexCount;
    unsigned char far *pPaletteScratchBuf;
    unsigned char pDriver_scratch_0x1a2[1306];
    u16 wTextureIdx;
    unsigned char pReserved_0x6be[42];
    unsigned char bYResDoubled;
    unsigned char bReserved_0x6e9;
};

struct GraphicsContextRender {
    unsigned char pReserved_tail[560];
    u16 wSpanTableBufSeg;
    unsigned char bVideoAutoDetectEnabled;
    unsigned char bReserved_0xab5;
};

struct GridCombatant {
    s16 unused;
    s16 paged_id;
    unsigned char tile_x;
    unsigned char tile_y;
};

struct IffResLevel {
    u16 wEnd_lo;
    u16 wEnd_hi_flags;
};

struct IffResReader {
    struct IoFile *pStream;
    char pChunk_id_stack[25];
    IffResLevel pLevel_cache[7];
    s16 nDepth;
    s16 nSkip_count;
    s32 nCursor;
    u16 wChunk_size_lo;
    u16 wChunk_size_hi_flags;
};

struct ImageRecord {
    u16 wImageData;
    u16 wImageOff;
    u16 wUnk4;
    s16 nWidth;
    s16 nHeight;
};

struct ItemRecord {
    char pName[32];
    u16 wFlags;
    u16 wName_split_off;
    u16 wDamage_class_threshold;
    s16 nBase_price;
    s16 nSwing_damage;
    s16 nThrust_damage;
    s16 nDefense_or_range_close;
    s16 nAttack_or_range_long;
    u16 wUnk_30;
    u16 wDefault_qty_or_1;
    u16 wUse_sfx;
    unsigned char bMax_stack;
    unsigned char bCharges_per_use;
    u16 wRace_mask;
    u16 wSub_flags;
    u16 wCategory;
    u16 wEffect_arg_a;
    u16 wEffect_arg_b;
    u16 wEffect_chance_pct;
    u16 wEffect_stat_value;
    u16 wPlayer_stat_mask;
    s16 nStat_value;
    u16 wWeapon_break_chance_pct;
    s16 nWeapon_break_amount;
    u16 wCondition_floor;
};

struct ItemSlot {
    unsigned char item_id;
    unsigned char condition;
    u16 flags;
};

struct ListWidget {
    s16 nX;
    s16 nY;
    s16 nInner_w;
    s16 nPixel_h;
    s16 nVisible_rows;
    s16 nAnchor_x;
    s16 nAnchor_y;
    u16 wCapacity;
    u16 wCount;
    u16 wScroll_offset;
    char **pLabels;
    u16 *pValues;
    MenuEntry *pParent;
};

struct Mat3x3 {
    s16 pM[9];
    unsigned char bType;
};

struct MenuEntry {
    u16 wWidget_type;
    u16 wAction_id;
    unsigned char bActive_flag;
    u16 wBase_color;
    u16 wEnable_gate;
    u16 wSub_state;
    Rect rect;
    char *pLabel;
    char *pPrimary_label;
    char *pAlt_label;
    u16 wSprite_base;
    u16 wCursor_shape;
    u16 wClick_flags;
    u16 wClick_sfx_id;
};

struct MenuPage {
    u16 wWants_screen_save;
    u16 wVisible;
    u16 wBg_color;
    Rect rect;
    u16 wEntry_count;
    MenuEntry *pEntries;
    char *pTitle;
    s16 nTitle_x;
    s16 nTitle_y;
    unsigned char far *pBackdrop_buffer;
};

struct MeshPartRecord {
    unsigned char bGroupId;
    unsigned char bEdgeVtxIdx;
    unsigned char bAnchorVtxIdx;
    unsigned char bVertexCount;
    u16 wVertexPoolOff;
    s16 nField6;
    u16 wPolyListOff;
    s16 nSub;
    u16 wSubArrOff;
};

struct MusicChunkEntry {
    s16 nId;
    s32 nOffset;
};

struct MusicChunkHeader {
    MusicChunkEntry far *pEntries;
    s16 nMagic;
    s16 nEntries;
    unsigned char bFlag;
};

struct ObjFixedDiskHeader {
    unsigned char kind;
    unsigned char chap_band;
    u16 world_item_id;
    s32 nWorld_x;
    s32 nWorld_y;
    unsigned char container_type;
    unsigned char num_items;
    unsigned char capacity;
    unsigned char data_types;
};

struct ObjFixedTemplateBuffer {
    unsigned char pPrefix[6];
    ObjFixedDiskHeader header;
};

struct OLD_IMAGE_DOS_HEADER {
    char e_magic[2];
    u16 e_cblp;
    u16 e_cp;
    u16 e_crlc;
    u16 e_cparhdr;
    u16 e_minalloc;
    u16 e_maxalloc;
    u16 e_ss;
    u16 e_sp;
    u16 e_csum;
    u16 e_ip;
    u16 e_cs;
    u16 e_lfarlc;
    u16 e_ovno;
};

struct OLD_IMAGE_DOS_RELOC {
    u16 offset;
    u16 segment;
};

struct PagedArrayEntry {
    ImageRecord **imageRecord;
    s16 nCount;
};

struct PageDirectory {
    s16 nCount;
    BookPage far *pPages[1];
};

struct PerspDiv2Args {
    s32 nNumX;
    s32 nDenom;
    s32 nNumY;
};

struct PerspDiv2Result {
    s32 nProjX;
    s32 nProjY;
};

struct PlayerSpawnRecord {
    unsigned char bZoneId;
    unsigned char bTileX;
    unsigned char bTileY;
    unsigned char bSubX;
    unsigned char bSubY;
    u16 wCameraHeading;
};

struct PolyEdge {
    s32 nAcc;
    s32 nStep;
};

struct PolyRasterState {
    s16 nRemapTableOff;
    PolyEdge pEdge[3];
};

struct ProximityPolygon {
    u16 wVertex_list_offset;
    unsigned char bVertex_count;
    unsigned char bShift;
    s16 nBase_height;
    u16 wReference_vertex_offset;
    unsigned char pUnknown_8[2];
};

struct ProximityRecord {
    u16 wRecord_id;
    s32 nWorld_x;
    s32 nWorld_y;
    s32 nZ_base;
    unsigned char pUnknown_e[4];
    s16 nAngle;
};

struct ProximityScanHit {
    ProximityZone far *pZone_rec;
    ProximityRecord far *pRecord;
    unsigned char bHit_index;
    unsigned char bPad;
    s32 nZ_delta;
};

struct ProximityVertex {
    unsigned char cCx;
    unsigned char cCy;
    s16 nX;
    s16 nY;
};

struct ProximityZone {
    s16 nMax_x;
    s16 nMax_y;
    unsigned char bFlags;
    unsigned char bVertex_count;
    u16 wVertex_table_offset;
};

struct ProximityZoneSettings {
    unsigned char pPad[2];
    unsigned char bFlat_flag;
    unsigned char bResolution_shift;
};

struct R3dRendererState {
    u32 dwMeshVertexFarPtrCache;
    u16 wMeshVertexPoolOff;
    u16 wMeshPartVertexCount;
    u16 wMeshScaleShift;
    u16 wMeshScaleResidual;
    unsigned char bMeshRenormCount;
    unsigned char _pad_0xd[1];
    unsigned char bCurMeshLod;
    unsigned char _pad_0xf[1];
    unsigned char bSkipFarEdgeCull;
    unsigned char bActorViewportClip;
    unsigned char _pad_0x12[1];
    unsigned char bPolyNormalFlipped;
    Mat3x3 mat3x3ViewRot;
    Mat3x3 mat3x3ActorRot;
    Mat3x3 mat3x3TempOut;
    s32 nActorRelX;
    s32 nActorRelY;
    s32 nActorRelZ;
    s32 nActorCamX;
    s32 nActorCamZ;
    s32 nActorCamY;
    s16 nMeshCamOriginX;
    s16 nMeshCamOriginY;
    s16 nMeshCamOriginZ;
    s16 nMeshOriginX;
    s16 nMeshOriginY;
    s16 nMeshOriginZ;
    unsigned char _pad_0x71[12];
    s16 nCamViewYaw;
    s16 nCamViewPitch;
    unsigned char _pad_0x81[2];
    s16 nCurModelIdx;
    void far *pCurActorPagedRec;
    unsigned char *pCurMeshPartVisTable;
    s32 nMeshBoundHalfExtent;
    s32 nActorBoundExtent;
    s32 nActorLodZ;
    u16 wActorProjScale;
    unsigned char _pad_0x99[6];
    s32 nActorClampedCamZ;
    s16 nMeshNormOriginX;
    s16 nActorFarClipThreshold;
    unsigned char bScreenShift;
    unsigned char _pad_0xa8[1];
    s16 nViewportCenterX;
    s16 nViewportCenterY;
};

struct RendererVtable {
    UnimplementedSlotFn far *slot_00_null;
    Slot01Fn far *pfn_blit_glyph_row;
    Slot02Fn far *pfn_draw_line;
    BlitSpriteFn far *pfn_blit_sprite;
    PresentFn far *pfn_present;
    Slot05Fn far *pfn_save_rect;
    RectByteSizeFn far *pfn_rect_byte_size;
    PasteRectFn far *pfn_paste_rect;
    VsyncWaitFn far *pfn_vsync_wait;
    UnimplementedSlotFn far *slot_09_unimplemented;
    Slot10Fn far *pfn_fill_run_a;
    Slot11Fn far *pfn_fill_run_b;
    UnimplementedSlotFn far *slot_12_unimplemented;
    ImageInstallFn far *pfn_image_install;
    Slot14Fn far *pfn_image_decode;
    UnpackNibblesToPlanesFn far *pfn_unpack_nibbles_to_planes;
    UnimplementedSlotFn far *slot_16_unimplemented;
    BlitChunkyFn far *pfn_blit_chunky;
    Slot18Fn far *pfn_blit_chunky_planar_4bit;
    Slot19Fn far *pfn_fill_textured_span_modey_b;
    Slot20Fn far *pfn_palette_set;
    GetpixelFn far *pfn_getpixel;
    PutpixelFn far *pfn_putpixel;
    Slot23Fn far *pfn_blit_rect_buffer;
    Slot24Fn far *pfn_set_buffer_offset;
    UnimplementedSlotFn far *pPfn_set_border_color;
    UnimplementedSlotFn far *slot_26_unimplemented;
    Slot27Fn far *pfn_fill_spans_dithered_modey;
    UnimplementedSlotFn far *slot_28_unimplemented;
    Slot29Fn far *pfn_blit_sprite_master;
    Slot30Fn far *pfn_blit_sprite_flip_h;
    Slot31Fn far *pfn_blit_sprite_flip_v;
    Slot32Fn far *pfn_clear_rect_state;
    BlitSpriteSimpleFn far *pfn_blit_sprite_simple;
    Slot34Fn far *pfn_palette_lerp_dac;
    Slot35Fn far *pfn_dac_write_palette;
    Slot36Fn far *pfn_blit_sprite_planar_rotated;
    Slot37Fn far *pfn_blit_stretched;
    Slot38Fn far *pfn_blit_sprite_chunky_flip;
    Slot39Fn far *pfn_fill_world_textured_span_modey;
    Slot40Fn far *pfn_fill_textured_span_modey;
    UnimplementedSlotFn far *slot_41_unimplemented;
    UnimplementedSlotFn far *slot_42_unimplemented;
    UnimplementedSlotFn far *slot_43_unimplemented;
    UnimplementedSlotFn far *slot_44_unimplemented;
    UnimplementedSlotFn far *slot_45_unimplemented;
    UnimplementedSlotFn far *slot_46_unimplemented;
    UnimplementedSlotFn far *slot_47_unimplemented;
    UnimplementedSlotFn far *slot_48_unimplemented;
    UnimplementedSlotFn far *slot_49_null;
};

struct ResourceRemapDesc {
    u16 wIdOrSeg;
    u16 wOffset;
    u16 wFlags;
};

struct ScriptAnimNode {
    u16 wTag;
    u16 wIdB;
    u16 wPcSaved;
    u16 wPcJumpTarget;
    u16 wPc;
    u16 wDispatchPc;
    u16 wFld_0c;
    u16 wOpCursor;
    u16 wFld_10;
    u16 wFld_12;
    u16 wFld_14;
    u16 wFld_16;
    u16 wFld_18;
    u16 wFld_1a;
    u16 wFld_1c;
    unsigned char bSavedFgColor;
    unsigned char bSavedFillColor;
    unsigned char bSavedClipEnabled;
    s16 nSavedClipXmin;
    s16 nSavedClipXmax;
    s16 nSavedClipYmin;
    s16 nSavedClipYmax;
    u16 wInterval;
    u16 wOpState;
    u16 wCounter;
    u16 wMode;
    u16 wCountdown;
    u32 dwDeadline;
    u32 dwTimerExpiry;
    ScriptAnimNode *pNext;
};

struct ScriptBlock {
    u16 wOpcode;
    u16 wOperand;
};

struct ScriptObject {
    u16 wResourceId;
    s16 nBlockCount;
    unsigned char far *pTt3_data;
    unsigned char far *pTt3_end;
    u16 pAhPagedImage[6];
    u16 pAhFont[6];
    unsigned char far *pCachedResource[6];
    unsigned char huge *pFreemem[12];
    u16 pRectSrcX[12];
    u16 pRectSrcY[12];
    u16 pRectDstX[12];
    u16 pRectDstY[12];
    unsigned char far *pNext;
    ScriptBlock far *far *pBlocks;
};

struct SfxLoopPri {
    unsigned char bLoop;
    unsigned char bPriority;
};

struct SkyStrip {
    unsigned char bKey;
    unsigned char bHorizonOverride;
    unsigned char bFlags;
    unsigned char bYTop;
    unsigned char bYBot;
    unsigned char bBaseRow;
    unsigned char bEndRow;
    u16 wSiBase;
    s16 nSiStride;
    unsigned char pPad[2];
};

struct Slot {
    s16 nInitialised;
    s16 nChannel_count;
    u16 far *pScript_buf;
    u16 far *pScript_end;
    ScriptObject far *pObjects_head;
    u16 wObjects_head_alt;
    u16 pChannel_state[80];
    u16 pChannel_b2[80];
    u16 far *pChannels[80];
    u32 pChannel_alt[80];
    u16 pOp_lists[80];
};

struct SpellDef {
    char *pName;
    s16 nCost;
    s16 nCost_max;
    s16 nSchool;
    s16 nSpell_kind;
    s16 nEffect_sprite_id;
    s16 nEffect_kind;
    s16 nInv_component_id;
    s16 nEffect_subkind;
    s16 nEffect_magnitude;
    s16 nEffect_param;
};

struct SpellDocRow {
    char far *pTitle;
    char far *pDesc1;
    char far *pDesc2;
    char far *pDesc3;
    char far *pDesc4;
    char far *pDesc5;
    char far *pDesc6;
};

struct SpellSymbolRecord {
    u16 wSpellId;
    s16 nHotspotX;
    s16 nHotspotY;
    char cHotkeyLetter;
};

struct StatusEffectSlot {
    s16 nType;
    s16 nSource;
    s16 nDuration_or_hp;
    s16 nAge_ticks;
    s16 nNext;
    unsigned char bFlag;
};

union StreamDescSrcUnion {
    unsigned char huge *pBufBase;
    struct IoFile *pFile;
};

struct StreamDesc {
    unsigned char *pBuf;
    unsigned char huge *pScratch;
    StreamDescSrcUnion src;
    u32 dwInPos;
    u32 dwInSize;
    u32 dwUncompressedSize;
    u32 dwOutPos;
    unsigned char bRingHead;
    unsigned char bRingTail;
    u32 dwFileStartOff;
    unsigned char bFlags;
};

struct TileMoveRecord {
    unsigned char bSubX;
    unsigned char bSubY;
    s16 nHeading;
};

struct TimerCallback {
    u16 wOff;
    u16 wSeg;
};

struct TimerSched {
    u16 wCountdown;
    u16 wReload;
};

struct TownSceneActor {
    Rect rect;
    u16 wChapterMask;
    s16 nCursorShape;
    char cKind;
    char cClickCount;
    s16 nNextScene;
    s16 nAnimChannel;
    u32 dwAltDialogKey;
    u32 dwDialogKey;
    void far *pDdxRecord;
    s16 nGateEventId;
    s16 nGateMin;
    s16 nGateMax;
};

struct TownScene {
    char pAnimPrefix[13];
    u16 wFlags;
    s16 nExitScene;
    s16 nMusicTrack;
    s16 nMusicIntensity;
    s16 nEntryAnim;
    s16 nExitAnim;
    s16 nIdleAnim;
    s16 nActorCount;
    u32 dwDialogKey;
    void far *pDdxRecord;
    void far *pActor;
    TownSceneActor pAActors[1];
};

struct Vec3Long {
    s32 nX;
    s32 nY;
    s32 nZ;
};

struct Vec3Short {
    s16 nX;
    s16 nY;
    s16 nZ;
};

struct VfxRec {
    s16 nW0;
    s16 nW1;
    s16 nW2;
};

struct WorldObject {
    u16 shapeId;
    WorldPos pos;
    EulerAngles orientation;
    union {
        u16 stateBits;
        s16 animationState;
        EncounterActorAux near *encounterAux;
    } state;
};

struct VisibleEntryList {
    unsigned char bZone;
    unsigned char bParty_x;
    unsigned char bParty_y;
    unsigned char bRef_pair_index;
    u16 wEntry_count;
    WorldObject far *pEntries;
};

struct WorldEntity {
    WorldObject base;
    EulerAngles angularVelocity;
    Vec3Short linearVelocity;
    s16 forwardVelocity;
};

struct WorldHotspot {
    WorldObject far *pEntity;
    u32 dwDist;
    Rect rect;
};

struct ZoneEntryRecord {
    unsigned char pHeader[2];
    PlayerSpawnRecord spawn;
    u32 dwPromptDlgKey;
    u32 dwEntryDlgKey;
    unsigned char pTrailing[2];
};

struct ZoneHotspot {
    u16 wKind;
    TileBBox bbox;
    u32 dwDef_record_offset;
    unsigned char bInhibitChapter;
    u16 wEvent_flag_pre1;
    u16 wEvent_flag_pre2;
    u16 wEvent_key_post;
    u16 wRepeat;
};

typedef DDXRecord far *PDdxRecord;

#endif
