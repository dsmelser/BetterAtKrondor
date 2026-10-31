/* R3D/CORE/DISTDIR.ASM: the octagonal distance (port/asm/distdir.c). */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/R3D/CORE/DISTDIR.H"
#include "bak_game_end.h"

/* distdir.points <s32 x1, y1, x2, y2>... -> u32 each */
static void cmd_points(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0) {
        s32 p1[2], p2[2];
        p1[0] = (s32)dt_u32(in);
        p1[1] = (s32)dt_u32(in);
        p2[0] = (s32)dt_u32(in);
        p2[1] = (s32)dt_u32(in);
        dt_put_u32(out, (uint32_t)distdir_octagonal_distance(p1, p2));
    }
}

/* distdir.deltas <s32 dx, dy>... -> u32 each */
static void cmd_deltas(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0) {
        s32 dx = (s32)dt_u32(in);
        s32 dy = (s32)dt_u32(in);
        dt_put_u32(out, (uint32_t)distdir_octagonal_distance_dxdy(dx, dy));
    }
}

const DtEntry dt_distdir_entries[] = {
    DT_COMMAND("distdir.points", cmd_points),
    DT_COMMAND("distdir.deltas", cmd_deltas),
    DT_END,
};
