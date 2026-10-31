/**
 * @file  gtypes.h
 * @brief Fixed-width integer types, the @ref TRUE / @ref FALSE constants and
 *        the @ref bool8 / @ref bool16 types.
 */
#ifndef BAK_GTYPES_H
#define BAK_GTYPES_H

/*
 * The original compiler (Borland C++ 3.1) has a 16-bit int and a 32-bit long.
 * The game's sources spell every integer type with these names so the widths
 * survive a modern compiler; under Borland they are the original types, so the
 * DOS build is unchanged.
 */
#ifdef BAK_NATIVE
#include <stdint.h>
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
#else
typedef int s16;
typedef unsigned int u16;
typedef long s32;
typedef unsigned long u32;
#endif

/** @brief Boolean true value (1). */
#define TRUE 1
/** @brief Boolean false value (0). */
#define FALSE 0

/** @brief 8-bit boolean; assign only @ref TRUE or @ref FALSE. */
typedef char bool8;
/** @brief 16-bit (int-width) boolean; assign only @ref TRUE or @ref FALSE. */
typedef s16 bool16;

typedef void code;

#endif
