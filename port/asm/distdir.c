/*
 * DISTDIR.ASM in C: the octagonal approximation of the distance between two points,
 * max(|dx|, |dy|) + 3 * min(|dx|, |dy|) / 8, in 32 bits as the original computes it: its
 * products and sums wrap, the eighth is a logical shift, and the larger delta is picked
 * with a signed compare. distdir_vec3_distance_dir_approx has no caller in the game and
 * traps.
 */
#include <stdint.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/R3D/CORE/DISTDIR.H"
#include "bak_game_end.h"

/* |a - b| as the original takes it after SUB: the true difference's sign decides, and the
 * 32-bit result wraps (80000000h negates to itself). */
static u32 abs_diff(s32 a, s32 b) {
    u32 d = (u32)a - (u32)b;
    return (int64_t)a - b < 0 ? 0u - d : d;
}

static u32 magnitude(s32 v) {
    return v < 0 ? 0u - (u32)v : (u32)v;
}

static s32 octagonal(u32 dx, u32 dy) {
    u32 max = dx, min = dy;
    if ((s32)dx < (s32)dy) {
        max = dy;
        min = dx;
    }
    return (s32)(max + ((min * 3) >> 3));
}

s32 distdir_octagonal_distance(s32 *p1, s32 *p2) {
    return octagonal(abs_diff(p2[0], p1[0]), abs_diff(p2[1], p1[1]));
}

s32 distdir_octagonal_distance_dxdy(s32 dx, s32 dy) {
    return octagonal(magnitude(dx), magnitude(dy));
}

u32 distdir_vec3_distance_dir_approx(s32 *a, u32 b, unsigned short dir_in) {
    (void)a;
    (void)b;
    (void)dir_in;
    BAK_UNIMPLEMENTED("DISTDIR.ASM: distdir_vec3_distance_dir_approx (no caller in the game)");
}
