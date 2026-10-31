#ifndef BAK_MUSSTART_H
#define BAK_MUSSTART_H

#include "structs.h"

extern AudioTrackHandle far *music_handle_start(AudioTrackHandle far *handle, s16 advance_n,
                                                s16 volume);

#endif
