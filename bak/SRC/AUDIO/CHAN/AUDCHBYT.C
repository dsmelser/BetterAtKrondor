#include "structs.h"
#include "SRC/AUDIO/CHAN/AUDCHBYT.H"
#include "SRC/AUDIO/CHAN/SFXCHAN.H"
#include "SRC/AUDIO/DRIVER/MIDI.H"

s16 far audio_channel_set_byte_15a(s16 channel, s16 value) {
    AudioTrackHandle far *track;

    if ((track = sfxchan_apply_to_channels(channel)) != 0) {
        midi_track_set_byte_15a(track->pHeader, value);
        return 1;
    }
    return 0;
}
