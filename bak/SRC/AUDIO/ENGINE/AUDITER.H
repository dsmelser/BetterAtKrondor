#ifndef BAK_AUDITER_H
#define BAK_AUDITER_H

#include "structs.h"

extern s16 g_nAudioIterFilter;
extern AudioListNode far *g_pAudioIterCursor;

extern AudioListNode far *audio_iter(s16 filter);

#endif
