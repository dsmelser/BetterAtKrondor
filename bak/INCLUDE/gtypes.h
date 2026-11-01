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
#include <stddef.h>
#include <stdint.h>
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;

/*
 * Idioms of the 16-bit original that still name what it did.
 *
 * NearPtr: a near (DGROUP) data pointer held in an integer - a 16-bit offset in
 * the original, a full pointer natively. Users cast it back, e.g. (char *)p.
 * NearWord is the same where the original's type is u16 rather than unsigned short.
 */
typedef uintptr_t NearPtr;
typedef uintptr_t NearWord;

/*
 * FP_SEG_NORM(p): the segment of a far pointer the original holds normalized, its offset
 * under 16, such as a shape record's: the near offsets inside the record count from it.
 * Natively a pointer keeps no segment, and FP_SEG gives its block's; this gives the
 * paragraph it is in.
 */
#define FP_SEG_NORM(p) bak_ptr_seg_norm(p)

/*
 * NEAR_STR(p): a near string pointer the original may hold as 0. It then reads DS:0000,
 * whose first bytes Borland's startup keeps zero (its null-pointer check area): an empty
 * string. Natively a NULL pointer reads as "".
 */
#define NEAR_STR(p) ((p) != NULL ? (p) : "")

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
