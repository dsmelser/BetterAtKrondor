#include "structs.h"
#include "SRC/AUDIO/SND/SNDINST.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/AUDIO/DRIVER/MUSDISP.H"
#include "SRC/STREAM/RESLOAD/RELBUF.H"
#include "SRC/STREAM/BUFLOAD/LOADCHNK.H"
#include "SRC/AUDIO/DRIVER/MIDI.H"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "SRC/AUDIO/DRIVER/SNDDRV.H"

s16 sndinst_drivers(s16 sfx_driver, s16 music_driver, u16 p3, char *fileName) {
    s16 failed;

    failed = 0;

    if (music_driver != -2) {
        my_strcpy(g_szSsmTag + 4, g_apMusicDriverTags[music_driver]);
        if ((g_pMusicDriver = bak_load_chunk(fileName, g_szSsmTag, 0)) != (void far *)0) {
            g_bMusicDriverInstalled = 1;
            snddrv_patch_farptr((u16)g_pMusicDriver, ((u16 *)&g_pMusicDriver)[1]);
            if (audio_driver_disp_seg1000_alt(p3, 1) == 0) {
                g_bMusicDriverInstalled = 0;
                audio_driver_disp_seg1000_far();
                release_buffer(g_pMusicDriver, 1);
                g_pMusicDriver = (void far *)0;
                failed = 1;
            }
        } else {
            failed = 1;
        }
    }

    if (sfx_driver != -2) {
        my_strcpy(g_szSsmTag + 4, g_apSfxDriverTags[sfx_driver]);
        if ((g_pSfxDriver = bak_load_chunk(fileName, g_szSsmTag, 0)) != (void far *)0) {
            g_nSfxResourceId = midi_driver_install_dispatch(
                (u16)g_pSfxDriver, (u16)((u32)g_pSfxDriver >> 16));
            if (audio_resource_install_by_id(fileName, (s16 *)&g_nSfxResourceId, 0) == 0) {
                release_buffer(g_pSfxDriver, 1);
                g_pSfxDriver = (void far *)0;
                failed = 1;
            }
        } else {
            failed = 1;
        }
        sfx_driver = (sfx_driver == SNDDRV_NONE) ? SNDDRV_M32 : sfx_driver;
    }

    g_nSfxDriverMode = sfx_driver;
    return !failed;
}
