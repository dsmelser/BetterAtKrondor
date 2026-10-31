#include "structs.h"
#include "SRC/AUDIO/CHAN/AUDCHFID.H"
#include "SRC/AUDIO/CHAN/SFXCHAN.H"

s16 audio_channel_for_id(s16 id) {
    AudioTrackHandle far *h;

    if ((h = sfxchan_apply_to_channels(id)) != 0)
        return (signed char)h->bActive;
    return -1;
}
