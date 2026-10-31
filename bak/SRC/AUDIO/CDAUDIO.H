#ifndef BAK_CDAUDIO_H
#define BAK_CDAUDIO_H

#ifdef V102CD
extern s16 far v102_cdaudio_mscdex_request(void);
extern void far v102_cdaudio_set_track_msf(s16 track, s32 msf_bias, s32 frame_offset);
extern void far v102_cdaudio_play(void);
extern void far v102_cdaudio_stop(void);
extern s16 far v102_cddrive_detect(char drive_letter);
#endif

#endif
