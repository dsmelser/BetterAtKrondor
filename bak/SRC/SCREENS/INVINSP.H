#ifndef BAK_INVINSP_H
#define BAK_INVINSP_H

#include "structs.h"

extern DDXRecord g_invInspectPanelDdx;
extern unsigned char g_abInvInspectPanelDdxStyle[10];

extern s16 invinspect_quantity_picker_dlg(ItemSlot far *item, s16 param_2, s16 param_3);
extern void invinspect_item_flow(ItemSlot far *item, s16 affecting, MenuPage *page);

#endif
