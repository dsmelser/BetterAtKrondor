/* ymfm_opl2.h - a YM3812 (OPL2) emulated by ymfm, through a C interface (ymfm_opl2.cpp). */
#ifndef BAK_YMFM_OPL2_H
#define BAK_YMFM_OPL2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YmfmOpl2 YmfmOpl2;

/* A chip after its reset; NULL when out of memory. */
YmfmOpl2 *ymfm_opl2_create(void);
void ymfm_opl2_destroy(YmfmOpl2 *chip);
/* Frames a second the chip makes when clocked at clockHz: clockHz / 72. */
uint32_t ymfm_opl2_rate(uint32_t clockHz);
/* A register written, as through the address port and then the data port. */
void ymfm_opl2_write(YmfmOpl2 *chip, uint8_t reg, uint8_t value);
/* The status port. */
uint8_t ymfm_opl2_status(YmfmOpl2 *chip);
/* The next frames of sound, one sample a frame. */
void ymfm_opl2_generate(YmfmOpl2 *chip, int16_t *out, int frames);

#ifdef __cplusplus
}
#endif

#endif
