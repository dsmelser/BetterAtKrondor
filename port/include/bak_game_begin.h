/*
 * bak_game_begin.h / bak_game_end.h - bracket game-header includes in port code.
 *
 * Game structs must have Borland's byte-packed layout in every translation unit,
 * port code included. Include host and SDL headers first, then:
 *
 *     #include "bak_game_begin.h"
 *     #include "SRC/GFX/SCREEN/SCREEN.H"
 *     #include "bak_game_end.h"
 *
 * Not include-guarded on purpose: each pair opens and closes one packed region.
 */
#include "bak_port.h"
#include "bak_rtl.h"

#ifndef BAK_NATIVE
#define BAK_NATIVE 1
#endif
#define far
#define near
#define huge
#define __far
#define interrupt
#define _es
#ifndef _WIN32
#define __cdecl
#endif

#define BAK_PACK_ACTIVE 1
#pragma pack(push, 1)

/* The same renames as bak_native.h. */
#define rand bak_rand
