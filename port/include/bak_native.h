/*
 * bak_native.h - force-included first into every game translation unit
 * (bak/SRC/**.C) of the native build.
 *
 * It makes the Borland C++ 3.1 dialect of the reconstructed sources compile with
 * a modern compiler and gives game structs Borland's layout (byte-packed). Port
 * code under port/ does not get this header: it reaches game headers through
 * bak_game.h, which applies the same packing around them, so every translation
 * unit agrees on struct layouts.
 */
#ifndef BAK_NATIVE_H
#define BAK_NATIVE_H

#define BAK_NATIVE 1

/*
 * Every host header the game sources use is included here, before the dialect
 * macros below and before packing is switched on, so the game's own #includes of
 * them are no-ops and no host structure is ever parsed under pack(1).
 */
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bak_port.h"
#include "bak_rtl.h"

/*
 * Borland memory-model, calling-convention and segment keywords. In a flat
 * address space a far, near or huge pointer is just a pointer.
 */
#define far
#define near
#define huge
#define __far
#define interrupt
#define _es
#ifndef _WIN32
#define __cdecl
#endif

/* The platform layer owns the process entry point and calls the game's main. */
#define main bak_main

/* The game has its own rand() (SYS/RAND.ASM, a 16-bit table generator), not libc's. */
#define rand bak_rand

/*
 * Borland's ctype.h, as the game was compiled with it. isalpha and isdigit are macros that
 * index the classification table with the argument as given, and the game passes plain
 * chars: a byte from 80h up reads before the table (bak_rtl.h). toupper and tolower are
 * Borland's functions, which return the byte unsigned.
 */
#undef isalpha
#undef isdigit
#undef toupper
#undef tolower
#define isalpha(c) (_ctype[(c) + 1] & (_IS_UPP | _IS_LOW))
#define isdigit(c) (_ctype[(c) + 1] & _IS_DIG)
#define toupper(c) bakrtl_toupper(c)
#define tolower(c) bakrtl_tolower(c)

/*
 * Borland's default structure alignment is one byte (the original build never
 * passes -a). Game file formats and hand-computed offsets depend on it.
 */
#define BAK_PACK_ACTIVE 1
#pragma pack(push, 1)

#endif
