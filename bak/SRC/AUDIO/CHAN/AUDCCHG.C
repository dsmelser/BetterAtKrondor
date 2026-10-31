#include "structs.h"
#include "SRC/AUDIO/CHAN/AUDCCHG.H"
#include "SRC/AUDIO/CHAN/SFXCHAN.H"
#include "SRC/AUDIO/DRIVER/MIDI.H"

s16 audio_chan_apply_ctrl_change(s16 channel, s16 ctrlNum, s16 value, s16 midiChannel) {
    AudioTrackHandle far *state;

    if ((state = sfxchan_apply_to_channels(channel)) != 0) {
        midi_apply_ctrl_change_thunk(state, ctrlNum, value, midiChannel);
        return 1;
    }
    return 0;
}
