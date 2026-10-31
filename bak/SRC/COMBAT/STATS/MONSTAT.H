#ifndef BAK_MONSTAT_H
#define BAK_MONSTAT_H

#include "structs.h"

extern s16 monstat_roll_stat_in_range(Combatant *actor, s16 max_val, s16 min_val, s16 stat_index);
extern void monstat_roll_stats_from_file(Combatant *actor);

#endif
