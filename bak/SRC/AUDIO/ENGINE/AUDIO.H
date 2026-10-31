#ifndef BAK_AUDIO_H
#define BAK_AUDIO_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern short g_alloc_to_pool;
extern short g_current_music_track;
extern unsigned short g_nMusicVolume;

extern void audio_ambient_tick(void);
extern s16 audio_music_play(s16 track_id);
extern void audio_music_fade_in_current(void);
extern void audio_music_fade_out_and_stop(void);
extern s16 audio_music_set_volume(s16 volume);
extern void audio_sfx_stop_combat_bank(void);
extern void audio_sfx_register_combat_bank(void);
extern void audio_preload_ui_sfx(void);
extern void audio_sfx_stop_environment_set(void);
extern void audio_sfx_register_world_bank(void);
extern void audio_sfx_stop_scene_sounds(void);
extern void audio_sfx_register_pair_4_18(void);
extern void audio_sfx_stop_pair_4_18(void);
extern void audio_sfx_register_pair_38_39(void);
extern void audio_sfx_stop_pair_38_39(void);
extern void audio_sfx_register_50(void);
extern void audio_sfx_stop_50(void);
extern void audio_sfx_register_45(void);
extern void audio_sfx_stop_45(void);
extern void audio_play(s16 sound_id);
extern s16 audio_sfx_register(ResFile *file, s16 sfx_id);
extern s16 audio_sfx_stop(s16 sfx_id);
extern s16 audio_sfx_play_n_times(s16 sfx_id, s16 extra_repeats, s16 blocking);

#endif
