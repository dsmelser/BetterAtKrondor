/*
 * The Sound Blaster's digital side, for the "CMS Sound Blaster" driver (ASB.ASM in C): the DSP
 * at base 220h (reset at +6, read data at +0Ah, command and write status at +0Ch, read status
 * at +0Eh), the 8237 DMA controller's channel 1 (address 02h, count 03h, mask 0Ah, mode 0Bh,
 * flip-flop 0Ch, page 83h) and the interrupt the DSP raises on IRQ 7 when a block ends. The
 * DSP reports version 2.01, a Sound Blaster 2.0's.
 *
 * Everything the game can read follows the platform's clock. A block started with command 14h
 * lasts its samples times the time constant's period (256 - tc microseconds), from the moment
 * the command came (machine_event_ns); its interrupt is delivered at the first poll from then
 * on (machine.c), stamped with that moment, so a block the handler starts follows without a gap;
 * the DMA's count register counts down with it. Command D0h holds a block, D4h lets it go on.
 *
 * The sound is an audio source. A block's bytes are read through the 8237 when it starts: a
 * 20-bit address is a paragraph and an offset in the segment table (a DOS block's), and the
 * address wraps within its 64 KB page as the 8237's does. They become an event that the source
 * plays at the block's rate, rendered at the mix's rate with linear interpolation between the
 * samples; the speaker switch (D1h, D3h) mutes it.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio.h"
#include "bak_port.h"
#include "platform.h"

#define SB_BASE 0x220
#define SB_IRQ 7
#define SB_VERSION 0x0201

#define DMA_ADDRESS1 0x02
#define DMA_COUNT1 0x03
#define DMA_MASK 0x0A
#define DMA_MODE 0x0B
#define DMA_FLIPFLOP 0x0C
#define DMA_PAGE1 0x83

#define READY 0x7F    /* write status: bit 7 clear, the DSP takes a byte */
#define NO_DATA 0x7F  /* read status: bit 7 clear, nothing to read */
#define HAS_DATA 0xFF /* read status: bit 7 set */
#define SILENCE 0x80

enum {
    EV_STOP,
    EV_BLOCK,
    EV_HOLD,
    EV_GO_ON,
    EV_SPEAKER_ON,
    EV_SPEAKER_OFF
};

static struct {
    bool present;
    uint16_t base;
    int irq;
    uint16_t version;
    /* the DSP */
    uint8_t queue[8]; /* bytes for the host to read */
    int queueHead, queueCount;
    uint8_t lastRead;
    uint8_t command; /* the command whose argument bytes are coming */
    int argsWanted, argsGot;
    uint8_t args[2];
    bool resetting;
    uint8_t timeConstant;
    bool speaker;
    /* the block */
    bool active, halted, waiting;    /* waiting: started while the channel was masked */
    uint32_t length;                 /* samples */
    uint64_t startNs, dueNs, heldNs; /* heldNs: what is left while halted */
    uint32_t done;                   /* samples transferred when it ended or was held */
    /* the 8237's channel 1 */
    bool flipFlop;
    bool masked;
    uint8_t mode, page;
    uint16_t address, count;
} s = {.present = true, .base = SB_BASE, .irq = SB_IRQ, .version = SB_VERSION, .masked = true};

static AudioSource *s_source;
static bool s_sourceFailed;

/* ---- the audio source: blocks played at their rate ---------------------------------------- */

typedef struct SbBlock {
    uint8_t *samples;
    uint32_t length;
    uint32_t period; /* microseconds a sample */
} SbBlock;

static struct {
    SbBlock *block;
    uint32_t index;
    uint64_t phase; /* in 1/(AUDIO_RATE * period) of a sample */
    bool held, speaker;
} a;

static void block_free(SbBlock *b) {
    if (b != NULL) {
        SDL_free(b->samples);
        SDL_free(b);
    }
}

static void sb_render(void *user, void *out, int frames) {
    int16_t *o = out;
    (void)user;
    for (int i = 0; i < frames; i++) {
        SbBlock *b = a.block;
        if (b == NULL || a.held || a.index >= b->length) {
            o[i] = 0;
            continue;
        }
        uint64_t den = (uint64_t)AUDIO_RATE * b->period;
        int s0 = b->samples[a.index];
        int s1 = a.index + 1 < b->length ? b->samples[a.index + 1] : s0;
        int v = s0 * 256 + (int)((int64_t)(s1 - s0) * 256 * (int64_t)a.phase / (int64_t)den);
        o[i] = a.speaker ? (int16_t)(v - SILENCE * 256) : 0;
        a.phase += 1000000;
        while (a.phase >= den) {
            a.phase -= den;
            a.index++;
        }
    }
}

static void sb_apply(void *user, const AudioEvent *e) {
    (void)user;
    switch (e->a) {
    case EV_BLOCK:
        block_free(a.block);
        a.block = e->p;
        a.index = 0;
        a.phase = 0;
        a.held = false;
        break;
    case EV_STOP:
        block_free(a.block);
        a.block = NULL;
        break;
    case EV_HOLD:
        a.held = true;
        break;
    case EV_GO_ON:
        a.held = false;
        break;
    case EV_SPEAKER_ON:
        a.speaker = true;
        break;
    case EV_SPEAKER_OFF:
        a.speaker = false;
        break;
    }
}

static AudioSource *source(void) {
    static const AudioSourceDesc desc = {.name = "Sound Blaster",
                                         .format = SDL_AUDIO_S16,
                                         .channels = 1,
                                         .rate = AUDIO_RATE,
                                         .volume = AUDIO_VOLUME_EFFECTS,
                                         .gain = 1.0f,
                                         .render = sb_render,
                                         .apply = sb_apply};
    if (s_source == NULL && !s_sourceFailed && (s_source = audio_source_add(&desc)) == NULL) {
        fprintf(stderr, "no Sound Blaster: its samples stay silent\n");
        s_sourceFailed = true;
    }
    return s_source;
}

static void post(uint32_t kind, void *p) {
    AudioSource *src = source();
    if (src != NULL)
        audio_post(src, kind, 0, p);
    else if (kind == EV_BLOCK)
        block_free(p);
}

void sbdsp_close(void) {
    block_free(a.block);
    memset(&a, 0, sizeof a);
    s_source = NULL;
    s_sourceFailed = false;
}

/* ---- the 8237's channel 1 -------------------------------------------------------------------- */

/* The block's bytes from the channel's address: runs within the 64 KB page (the address wraps,
 * the page stays), each through the segment table; silence past the count or where nothing is
 * mapped. */
static void dma_read(uint8_t *out, uint32_t n) {
    uint32_t avail = (uint32_t)s.count + 1;
    uint16_t at = s.address;
    uint32_t i = 0;
    while (i < n) {
        uint32_t run = n - i;
        if (run > 0x10000u - at)
            run = 0x10000u - at;
        uint32_t phys = (uint32_t)s.page << 16 | at;
        uint16_t seg = (uint16_t)(phys >> 4);
        uint32_t extent = seg != 0 ? bak_seg_extent(seg) : 0;
        uint32_t mapped = extent > (phys & 15) ? extent - (phys & 15) : 0;
        if (mapped > run)
            mapped = run;
        if (i + mapped > avail)
            mapped = i < avail ? avail - i : 0;
        if (mapped > 0)
            memcpy(out + i, (uint8_t *)bak_seg_ptr(seg) + (phys & 15), mapped);
        if (mapped < run) {
            memset(out + i + mapped, SILENCE, run - mapped);
            if (i + mapped < avail)
                fprintf(stderr, "Sound Blaster: DMA from unmapped memory at %05Xh\n",
                        (unsigned)(phys + mapped));
        }
        i += run;
        at = (uint16_t)(at + run);
    }
}

static uint64_t period_ns(void) {
    return (uint64_t)(256 - s.timeConstant) * 1000u;
}

/* The samples a block has transferred by nowNs. */
static uint32_t transferred(uint64_t nowNs) {
    if (!s.active || s.halted || s.waiting)
        return s.done;
    uint64_t n = nowNs > s.startNs ? (nowNs - s.startNs) / period_ns() : 0;
    return n < s.length ? (uint32_t)n : s.length;
}

/* ---- the DSP ------------------------------------------------------------------------------ */

static void push(uint8_t v) {
    if (s.queueCount < (int)sizeof s.queue) {
        s.queue[(s.queueHead + s.queueCount) % (int)sizeof s.queue] = v;
        s.queueCount++;
    }
}

static void begin_block(uint64_t nowNs) {
    uint8_t *copy = SDL_malloc(s.length);
    SbBlock *b = SDL_malloc(sizeof *b);
    s.waiting = false;
    s.halted = false;
    s.done = 0;
    s.startNs = nowNs;
    s.dueNs = nowNs + s.length * period_ns();
    if (copy == NULL || b == NULL) {
        SDL_free(copy);
        SDL_free(b);
        return; /* the block plays silent, its interrupt still comes */
    }
    dma_read(copy, s.length);
    b->samples = copy;
    b->length = s.length;
    b->period = 256u - s.timeConstant;
    post(EV_BLOCK, b);
}

static void command(uint8_t c, const uint8_t *arg) {
    switch (c) {
    case 0x14: /* 8-bit single-cycle DMA output: count - 1 */
        s.active = true;
        s.done = 0;
        s.length = (uint32_t)(arg[0] | arg[1] << 8) + 1;
        if (s.masked) /* the DSP asks for bytes the channel does not give yet */
            s.waiting = true;
        else
            begin_block(machine_event_ns());
        break;
    case 0x40:
        s.timeConstant = arg[0];
        break;
    case 0xD0: /* hold the block */
        if (s.active && !s.halted && !s.waiting) {
            uint64_t now = machine_event_ns();
            s.done = transferred(now);
            s.heldNs = s.dueNs > now ? s.dueNs - now : 0;
            s.halted = true;
            post(EV_HOLD, NULL);
        }
        break;
    case 0xD4: /* go on with it */
        if (s.active && s.halted) {
            uint64_t now = machine_event_ns();
            s.halted = false;
            s.dueNs = now + s.heldNs;
            s.startNs = s.dueNs - s.length * period_ns();
            post(EV_GO_ON, NULL);
        }
        break;
    case 0xD1:
    case 0xD3:
        s.speaker = c == 0xD1;
        post(s.speaker ? EV_SPEAKER_ON : EV_SPEAKER_OFF, NULL);
        break;
    case 0xE0: /* identification: the byte inverted */
        push((uint8_t)~arg[0]);
        break;
    case 0xE1:
        push((uint8_t)(s.version >> 8));
        push((uint8_t)s.version);
        break;
    default: /* 10h direct output, 48h block size, 1Ch auto-init: the driver's file streaming
                only, which the game never uses */
        break;
    }
}

static void write_byte(uint8_t v) {
    if (s.argsWanted > 0) {
        s.args[s.argsGot++] = v;
        if (--s.argsWanted == 0)
            command(s.command, s.args);
        return;
    }
    s.command = v;
    s.argsGot = 0;
    switch (v) {
    case 0x10:
    case 0x40:
    case 0xE0:
        s.argsWanted = 1;
        break;
    case 0x14:
    case 0x48:
        s.argsWanted = 2;
        break;
    default:
        command(v, NULL);
        break;
    }
}

/* Writing 1 then 0 to the reset port: whatever was under way stops, and the DSP says AAh. */
static void reset(void) {
    if (s.active)
        post(EV_STOP, NULL);
    s.active = s.halted = s.waiting = false;
    s.queueCount = 0;
    s.argsWanted = 0;
    s.speaker = false;
    post(EV_SPEAKER_OFF, NULL);
    push(0xAA);
}

/* The DSP's ports: reset, read data, command and write status, read status. */
static bool is_dsp_port(uint16_t port) {
    uint16_t r = (uint16_t)(port - s.base);
    return port >= s.base && (r == 0x06 || r == 0x0A || r == 0x0C || r == 0x0E);
}

void sbdsp_configure(bool present, uint16_t base, int irq, uint16_t version) {
    memset(&s, 0, sizeof s);
    s.present = present;
    s.base = base;
    s.irq = irq;
    s.version = version;
    s.masked = true;
}

bool sbdsp_port_in8(uint16_t port, uint8_t *value) {
    if (port == DMA_ADDRESS1 || port == DMA_COUNT1) {
        uint32_t n = transferred(machine_event_ns());
        uint16_t v = port == DMA_ADDRESS1 ? (uint16_t)(s.address + n) : (uint16_t)(s.count - n);
        *value = s.flipFlop ? (uint8_t)(v >> 8) : (uint8_t)v;
        s.flipFlop = !s.flipFlop;
        return true;
    }
    if (!is_dsp_port(port))
        return false;
    if (!s.present) {
        *value = 0xFF; /* an empty bus */
        return true;
    }
    switch (port - s.base) {
    case 0x0A:
        if (s.queueCount > 0) {
            s.lastRead = s.queue[s.queueHead];
            s.queueHead = (s.queueHead + 1) % (int)sizeof s.queue;
            s.queueCount--;
        }
        *value = s.lastRead;
        return true;
    case 0x0C:
        *value = READY;
        return true;
    case 0x0E: /* (reading it acknowledges the interrupt, which the platform delivers once) */
        *value = s.queueCount > 0 ? HAS_DATA : NO_DATA;
        return true;
    default: /* the reset port */
        *value = 0xFF;
        return true;
    }
}

bool sbdsp_port_out8(uint16_t port, uint8_t value) {
    switch (port) {
    case DMA_ADDRESS1:
    case DMA_COUNT1: {
        uint16_t *r = port == DMA_ADDRESS1 ? &s.address : &s.count;
        *r =
            s.flipFlop ? (uint16_t)((*r & 0x00FF) | value << 8) : (uint16_t)((*r & 0xFF00) | value);
        s.flipFlop = !s.flipFlop;
        return true;
    }
    case DMA_MASK:
        if ((value & 3) == 1) {
            s.masked = (value & 4) != 0;
            if (!s.masked && s.waiting)
                begin_block(machine_event_ns());
        }
        return true;
    case DMA_MODE:
        if ((value & 3) == 1)
            s.mode = value;
        return true;
    case DMA_FLIPFLOP:
        s.flipFlop = false;
        return true;
    case DMA_PAGE1:
        s.page = value;
        return true;
    default:
        break;
    }
    if (!is_dsp_port(port))
        return false;
    if (!s.present)
        return true;
    if (port == s.base + 0x06) {
        if (value & 1)
            s.resetting = true;
        else if (s.resetting) {
            s.resetting = false;
            reset();
        }
    } else if (port == s.base + 0x0C) {
        write_byte(value);
    }
    return true;
}

/* ---- the block's end ---------------------------------------------------------------------- */

static bool finish(uint64_t dueNs) {
    if (!s.active || s.halted || s.waiting)
        return false;
    s.active = false;
    s.done = s.length;
    machine_raise_irq(s.irq, dueNs);
    return true;
}

bool sbdsp_poll(uint64_t nowNs) {
    return s.active && !s.halted && !s.waiting && nowNs >= s.dueNs && finish(s.dueNs);
}

uint64_t sbdsp_next_due_ns(void) {
    return s.active && !s.halted && !s.waiting ? s.dueNs : 0;
}

bool sbdsp_complete_now(void) {
    return finish(machine_event_ns());
}

void sbdsp_state(SbdspState *out) {
    out->timeConstant = s.timeConstant;
    out->speaker = s.speaker;
    out->active = s.active;
    out->halted = s.halted;
    out->length = s.length;
    out->dmaMode = s.mode;
    out->dmaPage = s.page;
    out->dmaAddress = s.address;
    out->dmaCount = s.count;
    out->dmaMasked = s.masked;
}
