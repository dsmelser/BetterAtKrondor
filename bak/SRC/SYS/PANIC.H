#ifndef BAK_PANIC_H
#define BAK_PANIC_H

#include "structs.h"

extern s16 panic_fatal_error_exit(char far *module, s16 line, char *msg);
extern void panic_assert_fail_report(char far *file, s16 line);

#endif
