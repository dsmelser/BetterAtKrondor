/*
 * midiseq.h - the state of the sequencer (AUDIO/DRIVER/MIDI.ASM, port/asm/midi.c).
 *
 * The original keeps it in its code segment, at 01C2h-03C6h; MidiData has that layout
 * (far pointers become host pointers, the same size), so tables indexed past their end
 * reach the same neighbours. Shared with the differential test.
 */
#ifndef BAK_ASM_MIDISEQ_H
#define BAK_ASM_MIDISEQ_H

#include <stddef.h>
#include <stdint.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

#pragma pack(push, 1)

/* The scalars at 03B3h (_cs_pause_refcount and the bytes after it). */
typedef struct MidiGlobals {
    uint8_t lock;          /* +0  nonzero: the timer tick does nothing */
    uint8_t voiceLow;      /* +1  the voices the arbiter may give out (driver slot 1: CL..CH) */
    uint8_t voiceHigh;     /* +2 */
    uint8_t deviceId;      /* +3  driver slot 0's CH: picks the script's track table */
    uint8_t eventVoice;    /* +4  the voice of the stream being played; FFh none */
    uint8_t eventFixed;    /* +5  that stream has a fixed voice */
    uint8_t budget;        /* +6  driver slot 0's CL: what the voices of all tracks may cost */
    uint8_t caps;          /* +7  driver slot 0's AH >> 4, bit 0 set with a digital driver */
    uint8_t slotKey;       /* +8  registry slot << 4 of the track being played */
    uint8_t globalParam;   /* +9  for the driver's slot 11, 0-10 */
    uint8_t budgetSave;    /* +10 */
    uint8_t dirty;         /* +11 voices must be given out again */
    uint8_t deferVolume;   /* +12 volume changes wait in voiceRefresh */
    uint8_t refreshCursor; /* +13 */
    uint8_t unused14[2];   /* +14 */
    uint8_t seeking;       /* +16 events change the state but send nothing */
    uint8_t unused17[2];   /* +17 */
    uint8_t tableId;       /* +19 */
} MidiGlobals;

typedef struct MidiData {
    unsigned char *registry[16];   /* 01C2h the playing tracks, highest priority first */
    unsigned char *streams[16];    /* 0202h the track played by the digital driver */
    unsigned char *voiceTrack[32]; /* 0242h the track each voice belongs to (16 used) */
    u16 programs[16];              /* 02C2h scratch: the script's track table */
    uint8_t voiceOwner[16];        /* 02E2h registry slot << 4 | channel; FFh free */
    uint8_t voiceFlags[16];        /* 02F2h the voice is its channel's own (number = channel) */
    uint8_t voiceAge[16];          /* 0302h steal the highest first; 0 never */
    uint8_t voiceCost[16];         /* 0312h */
    uint8_t voiceStage[16];        /* 0322h owners being given out */
    uint8_t snapFlags[16];         /* 0332h a copy, to undo a track that does not fit */
    uint8_t snapAge[16];
    uint8_t snapCost[16];
    uint8_t snapStage[16];
    uint8_t voiceChannel[16]; /* 0372h the channel of each voice; 0Fh free */
    uint8_t voiceRefresh[16]; /* 0382h a volume to send; FFh none */
    uint8_t seekSave[15];     /* 0392h */
    unsigned char *drvvec;    /* 03A1h the synth driver's chunk */
    u16 cmdSlots[7];          /* 03A5h midi_queue_command's arguments */
    MidiGlobals g;            /* 03B3h */
} MidiData;

#pragma pack(pop)

_Static_assert(sizeof(MidiData) == 0x3c7 - 0x1c2, "MIDI.ASM data block");
_Static_assert(offsetof(MidiData, drvvec) == 0x3a1 - 0x1c2, "MIDI.ASM data block");

extern MidiData g_midiData;

#endif
