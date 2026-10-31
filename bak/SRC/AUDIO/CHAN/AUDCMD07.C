#include "SRC/AUDIO/CHAN/AUDCMD07.H"
#include "SRC/AUDIO/DRIVER/MIDI.H"

s16 audio_driver_cmd07_dispatch(s16 velocity) {
    midi_set_note_velocity_thunk(velocity);
    return 1;
}
