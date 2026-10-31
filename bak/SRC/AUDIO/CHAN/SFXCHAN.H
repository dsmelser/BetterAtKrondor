#ifndef BAK_SFXCHAN_H
#define BAK_SFXCHAN_H

#include "structs.h"

extern AudioTrackHandle far *sfxchan_apply_to_channels(s16 channel_or_mode);
extern AudioTrackHandle far *sfxchan_handle_channel(AudioListNode far *node);

#endif
