#include <mem.h>
#include <stdlib.h>
#include "structs.h"
#include "SRC/R3D/ACTOR/ACTMOTN.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/SYS/PANICF.H"

short g_nActorKnockbackColorIdx = 0;
short g_nActorSpriteFlip = 0;
unsigned short g_nActorShakeX = 0x0000;
unsigned short g_nActorShakeY = 0x0000;
unsigned short g_bForceOverlay = 0x0000;

void *actormotion_xzmalloc(u16 size) {
    void *p;

    p = calloc(1, size);
    if (p == NULL) {
        panic("malloc failed");
    }
    memset(p, 0, size);
    return p;
}

void actormotion_xzfree(void *ptr) {
    free(ptr);
}

WorldEntity *actormotion_vec_alloc(unsigned short type_tag) {
    WorldEntity *entity;

    entity = actormotion_xzmalloc(sizeof(WorldEntity));
    entity->base.shapeId = type_tag;
    return entity;
}

u16 *actormotion_alloc_24b_bit800(u16 tag) {
    u16 *rec;

    rec = actormotion_xzmalloc(0x18);
    rec[1] = tag;
    *rec |= 0x800;
    return rec;
}

void actormotion_integrate(WorldEntity *actor) {
    s32 t;

    actor->base.orientation.pitch += actor->angularVelocity.pitch;
    actor->base.orientation.roll += actor->angularVelocity.roll;
    actor->base.orientation.yaw += actor->angularVelocity.yaw;
    if ((actor->base.orientation.pitch | actor->base.orientation.roll) != 0) {
        t = (s32)actor->forwardVelocity * (s32)-r3d_tbl_cos(actor->base.orientation.pitch);
        t = (t >> 0xe) * (s32)r3d_tbl_sin(actor->base.orientation.yaw);
        actor->base.pos.xy.nWorld_x += (t >> 0xe);
        t = (s32)actor->forwardVelocity * (s32)r3d_tbl_cos(actor->base.orientation.pitch);
        t = (t >> 0xe) * (s32)r3d_tbl_cos(actor->base.orientation.yaw);
        actor->base.pos.xy.nWorld_y += (t >> 0xe);
        actor->base.pos.nWorld_z +=
            ((s32)actor->forwardVelocity * (s32)r3d_tbl_sin(actor->base.orientation.pitch) >>
             0xe);
    } else if (actor->base.orientation.yaw != 0) {
        actor->base.pos.xy.nWorld_x +=
            ((s32)actor->forwardVelocity * (s32)-r3d_tbl_sin(actor->base.orientation.yaw) >> 0xe);
        actor->base.pos.xy.nWorld_y +=
            ((s32)actor->forwardVelocity * (s32)r3d_tbl_cos(actor->base.orientation.yaw) >> 0xe);
    } else {
        actor->base.pos.xy.nWorld_y += (s32)actor->forwardVelocity;
    }
    actor->base.pos.xy.nWorld_x += (s32)actor->linearVelocity.nX;
    actor->base.pos.xy.nWorld_y += (s32)actor->linearVelocity.nY;
    actor->base.pos.nWorld_z += (s32)actor->linearVelocity.nZ;
    return;
}

s16 actormotion_atan2_long(s32 y, s32 x) {
    s16 angle;
    unsigned char yNeg;
    unsigned char xNeg;

    if (yNeg = y < 0) {
        y = -y;
    }
    if (xNeg = x < 0) {
        x = -x;
    }
    if (y == 0) {
        angle = 0x400;
    } else if (x == 0) {
        angle = 0;
    } else if (y == x) {
        angle = 0x200;
    } else if (y < x) {
        angle = 0x400 - r3d_tbl_atan_lookup_raw((s16)((y << 9) / x));
    } else {
        angle = r3d_tbl_atan_lookup_raw((s16)((x << 9) / y));
    }
    if (yNeg) {
        angle = 0x800 - angle;
    }
    if (xNeg) {
        angle = 0x1000 - angle;
    }
    angle -= 0x400;
    return angle * 0x10;
}

s16 actormotion_clamp_signed_delta(s16 a, s16 b, s16 max_abs) {
    s16 delta;

    delta = a - b;
    max_abs = (((delta < 0) ? ((delta == -0x8000) ? 0x7fff : -delta) : delta) > max_abs)
                  ? max_abs
                  : ((delta < 0) ? ((delta == -0x8000) ? 0x7fff : -delta) : delta);
    return (delta >= 0) ? max_abs : 0 - max_abs;
}
