/*
 * dude.h - the SX.OVL sound drivers ("DUDE" drivers, the sources in DRIVERS/SX) in C.
 *
 * A synth driver is the SX.OVL chunk the sequencer (AUDIO/DRIVER/MIDI.ASM) far-calls at
 * offset 0 with BP = slot, AL = MIDI channel (the voice), CH and CL = data and, for
 * slot 1, ES:AX = the patch bank. Results come back in AX and CX. The 18 slots are the
 * same in every driver (3, 6, 9 and 14-16 do nothing):
 *
 *    0 describe: AL = SSM resource id of the bank, AH >> 4 = capabilities,
 *      CH = device id (selects the script's track table), CL = voice budget
 *    1 load the bank at ES:AX: CL..CH = the voices the sequencer may use, AX = FFFFh: failed
 *    2 reset   4 note off   5 note (velocity 0: off)   7 controller   8 program
 *   10 pitch bend (CH = LSB, CL = MSB)   11, 12, 13 set (CL = FFh: query) a global, the
 *      master volume, output on/off; AX = the previous value   17 query a shadowed value
 *
 * Natively the game still loads the chunk, and the port's C code for it works on the
 * chunk's own data, at the offsets the original's code uses, so the driver's state and
 * its power-on values are the original's. Drivers are recognised by the device name in
 * the chunk's DUDE header.
 */
#ifndef BAK_DRIVERS_DUDE_H
#define BAK_DRIVERS_DUDE_H

#include <stdint.h>

typedef struct DudeRegs {
    uint16_t ax;
    uint16_t cx;
    const unsigned char *bank; /* slot 1: ES:AX */
    uint16_t bx;               /* the AdLib driver's reset returns it in CX (the sequencer: 0) */
} DudeRegs;

/* A synth driver's entry: the chunk it was loaded as, the slot (BP) and the registers. */
typedef void DudeEntry(unsigned char *chunk, uint16_t slot, DudeRegs *regs);

/*
 * A digital driver (the "music driver" of SNDINST.C: ASB, APA, ADS, ...) is far-called
 * with AX = command and SS:SI = an argument block, which it may write results into;
 * it returns AX.
 */
typedef uint16_t DudeDigitalEntry(unsigned char *chunk, uint16_t cmd, void *args);

/* The native code of a loaded chunk, by the device name in its DUDE header; NULL when
 * the driver is not ported. */
DudeEntry *dude_synth_entry(const unsigned char *chunk);
DudeDigitalEntry *dude_digital_entry(const unsigned char *chunk);

/* The device name in a chunk's DUDE header, for messages: a Pascal string after the
 * header's tag, "dude" in synth drivers (interface 1), 8 bytes in digital ones (2). */
void dude_device_name(const unsigned char *chunk, char *out, int size);

/* A far call into the digital driver loaded as chunk; stops when it is not ported. */
uint16_t dude_digital_call(unsigned char *chunk, uint16_t cmd, void *args);

/* Tests: drivers of their own, recognised by the device name "difftest". */
extern DudeEntry *g_dudeTestSynth;
extern DudeDigitalEntry *g_dudeTestDigital;

/* DRIVERS/SX/M32.ASM: Roland MT-32 over an MPU-401 at 330h. */
void m32_dispatch(unsigned char *chunk, uint16_t slot, DudeRegs *regs);
/* DRIVERS/SX/ADL.ASM: the AdLib's OPL2 at 388h. */
void adl_dispatch(unsigned char *chunk, uint16_t slot, DudeRegs *regs);

/* DRIVERS/SX/ASB.ASM: the Sound Blaster's digital side ("CMS Sound Blaster"). asb_isr_offset
 * names a handler it installed by the original's offset (0: none, -1: not the driver's), for
 * the tests. */
uint16_t asb_dispatch(unsigned char *chunk, uint16_t cmd, void *args);
int asb_isr_offset(void (*isr)(void));

#endif
