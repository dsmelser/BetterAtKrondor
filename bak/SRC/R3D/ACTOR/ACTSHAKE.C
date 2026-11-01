#include "SRC/R3D/ACTOR/ACTOROVL.H"
#include "structs.h"
#include "SRC/R3D/ACTOR/ACTSHAKE.H"
#include "SRC/SYS/RAND.H"
#include "SRC/R3D/ACTOR/ACTRENDR.H"

void far render_actor_with_shake(WorldObject far *visible_entry) {
    WorldObjectState savedState;

    if (g_bShakeActorEnabled != 0) {
        savedState = visible_entry->state;
        g_bShakeJitterX = RND2_FX(4);
        g_bShakeJitterY = RND_FX(5);
        visible_entry->state.visTable = g_shakeJitterTable;
        actorrender_entity(visible_entry);
        visible_entry->state = savedState;
        return;
    }
    actorrender_entity(visible_entry);
    return;
}
