/*
 * R3DDOT3.ASM in C: the dot product of two vectors of three shorts, in 32 bits (the
 * original's adds wrap). The back-face test, the face shading and the mesh-part sort use it.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/R3D/CORE/R3D.H"
#include "bak_game_end.h"

s32 r3d_dot3_long(s16 *a, s16 *b) {
    u32 sum = (u32)((s32)a[0] * b[0]);
    sum += (u32)((s32)a[1] * b[1]);
    sum += (u32)((s32)a[2] * b[2]);
    return (s32)sum;
}

s32 r3d_dot3_long_far(s16 *pVecA, s16 *pVecB) {
    return r3d_dot3_long(pVecA, pVecB);
}
