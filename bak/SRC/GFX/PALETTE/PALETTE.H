#ifndef BAK_PALETTE_H
#define BAK_PALETTE_H

#include "structs.h"

extern char g_abPaletteBlendLut[127];

extern void palette_buffers_alloc(char *fname);
extern void palette_buffers_free(void);
extern void palette_apply_pending_load(void);
extern void palette_state_reset(void);
extern void palette_fade_in(u16 palette_off, u16 palette_seg, s16 step, s16 wait_each_step);
extern void palette_fade_out(u16 palette_off, u16 palette_seg, s16 step, s16 wait_each_step);
extern void palette_screen_clear_black(void);
extern void palette_blend_with_daynight(unsigned char far *pPalSrc);
extern void palette_apply_cycled(void);
extern void palette_interpolate_range(unsigned char far *src, unsigned char far *dst, char *pTarget,
                                      s16 range_start, s16 range_end, s16 blend);
extern void palette_build_blend_lut(s16 blend_factor);
extern void palette_daylight_tick(void);
extern s16 palette_compute_daylight(void);
extern s16 palette_daylight_modulate(s16 base_brightness);
extern TimerEventEntry *palette_fade_schedule(unsigned short sub_id, u32 duration);
extern void palette_fade_run_scheduled(TimerEventEntry *entry);
extern unsigned char far *palette_set(unsigned char far *pal);

#endif
