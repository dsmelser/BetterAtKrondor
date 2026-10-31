#ifndef BAK_ADSCRIPT_H
#define BAK_ADSCRIPT_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern NearPtr g_aTtmTextStringOffsets[9];
extern ScriptObject far *g_pScriptObjectListHead;
extern ScriptObject far *g_pCurScriptObject;
extern ScriptAnimNode *g_pScriptObjectChainHead;
extern ScriptAnimNode *g_pCurScriptAnimNode;
extern unsigned char g_bTtmViewportClampFlag;
extern unsigned short g_nVgaRenderMode;

extern void adscript_set_dbb_if_202d_eq_0c(void);
extern s16 adscript_resource_load(FileRef *file);
extern s16 adscript_select_object(s16 id);
extern short adscript_channel_dispatch_loop(unsigned short script_obj_id);
extern void adscript_object_unload(s16 param_1);
extern void adscript_renderer_reset(void);
extern s16 adscript_run_then_apply_palette(s16 block_index);
extern s16 adscript_run_by_index(s16 block_index);
extern void adscript_drain_object_list(void);
extern void adscript_apply_pending_palette(void);
extern void adscript_op_noop(s16 a, s16 b, s16 c, s16 d);
extern void adscript_blit_full_other_page(void);
extern void adscript_rndr_blit_other_page(s16 x, s16 y, s16 w, s16 h);
extern char far *adscript_skip_aligned_cstring(char far *p);

#endif
