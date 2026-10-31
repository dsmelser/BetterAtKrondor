#ifndef BAK_TTM_H
#define BAK_TTM_H

#include "structs.h"

extern short g_nPaletteCycleSavedDuration;
extern short g_nScriptTickOverride;
extern short g_nScriptSubOriginX;
extern short g_nScriptSubOriginY;
extern unsigned short g_wTtmBlitTint;
extern unsigned short g_nScriptCurOpcode;
extern unsigned short g_wScriptOpCursorCache;
extern short g_aPaletteCycleBandEnd[3];
extern short g_aPaletteCycleBandStart[3];
extern unsigned short g_wScript537fbOpVestigial;
extern unsigned short g_aTtmResourceSlots[20];
extern char g_szTtmScratch[100];
extern unsigned char far *g_pPendingPalette;

extern s16 ttmscript_interpret_loop(s16 block_index);
extern s16 ttmscript_find_block_by_tag(s16 tag_arg);
extern char *ttmscript_strcpy_to_scratch(s16 param_1, char far *param_2);
extern ScriptObject far *ttmscript_body_after_args(void);
extern void ttmscript_vport_set_large_enough(s16 x, s16 y, s16 w, s16 h);
extern void ttmscript_pal_cycle_band_add(s16 param_1, s16 param_2);
extern void ttmscript_screen_trans_disp(unsigned short param_1, unsigned short param_2, s16 param_3, s16 param_4,
                                        unsigned short param_5, unsigned short param_6);
extern unsigned short ttmscript_lookup_coord_global(unsigned short idx);
extern void ttmscript_vports_reset_all_100(void);
extern void ttmscript_sfx_channels_stop_all(void);

#endif
