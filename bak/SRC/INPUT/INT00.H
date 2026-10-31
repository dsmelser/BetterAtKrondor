#ifndef BAK_INT00_H
#define BAK_INT00_H

#include "structs.h"

extern unsigned char g_int00_installed;
extern u16 g_wInt00SaveSeg;
extern u16 g_wInt00SaveOff;

extern void install_int00_handler(void);
extern u16 near int00_divide_recovery(u16 ret_cs);
extern void restore_int00_handler(void);

#endif
