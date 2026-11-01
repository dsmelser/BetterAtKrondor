#ifndef BAK_WORLDHIT_H
#define BAK_WORLDHIT_H

#include "structs.h"

extern unsigned short g_wSkyColorR;
extern unsigned short g_wSkyColorG;
extern unsigned short g_wSkyColorB;
extern ImageRecord **g_pMapIconsTable;
extern unsigned char g_abActorAnimTemplateDefault[10];
extern unsigned char g_abActorAnimTemplateRanged[10];

extern void world_render_scene_dispatch(s16 animate);
extern void world_render_frame(s16 animate);
extern void world_render_sky_texture_load(void);
extern void world_render_sky_texture_free(void);
extern void world_render_frame_full(void);
extern void world_render_tick_objects(void);
extern void world_render_frame_with_hittest(void);
extern void world_rndr_apply_window_vport(void);
extern void world_render_actor_at_position(s16 actor_kind, s16 gridX, s16 gridY);
extern void world_render_actor_at_tile(s16 actor_id, s16 tile_x, s16 tile_y, s16 z, u16 yaw,
                                       Combatant *override_actor);
extern void world_render_actor_swap_textures(s16 actor_id, s16 nGrid_x, s16 nGrid_y);
extern void world_render_with_overlay(s16 cursor_kind, WorldObject *focus);
extern void world_render_view(s16 force_overlay, s16 chapter_split);
extern s16 world_rndr_actor_angle_actor(Combatant *from, Combatant *to);
extern Combatant *world_rndr_ranged_attack_anim(Combatant *attacker, Combatant *target,
                                                  s16 *p_hit_out, unsigned short action_id, s16 speed,
                                                  char anim_override);
extern void world_render_combat_grid_sprite(WorldObject *actor);
extern void world_render_record_marker_dot(WorldObject far *entry);

#endif
