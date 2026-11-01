/* The Sound Blaster's DSP and its DMA channel: the reset handshake and identification, a block's
 * interrupt at its moment on the platform's clock, its samples in the mix, the speaker, holding
 * a block, a channel masked, a reset under way. */
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/audio.h"
#include "../../platform/platform.h"
#include "bak_port.h"

#define T0 1000000000ull
#define MS 1000000ull
#define US 1000ull
#define BASE 0x220

static uint64_t s_irqAt[8];
static int s_irqs;

static void count_irq(void) {
    if (s_irqs < 8)
        s_irqAt[s_irqs] = machine_event_ns();
    s_irqs++;
}

static uint8_t dsp_in(int reg) {
    uint8_t v = 0;
    UNIT_CHECK(sbdsp_port_in8((uint16_t)(BASE + reg), &v));
    return v;
}

static void dsp_out(uint8_t v) {
    UNIT_CHECK(sbdsp_port_out8(BASE + 0x0C, v));
}

static void dsp_reset(void) {
    sbdsp_port_out8(BASE + 0x06, 1);
    sbdsp_port_out8(BASE + 0x06, 0);
}

/* Channel 1 at the buffer's 20-bit address (its segment's paragraph), count bytes. */
static void dma_program(const void *buffer, uint16_t count) {
    uint32_t phys = (uint32_t)bak_ptr_seg(buffer) * 16 + bak_ptr_off(buffer);
    sbdsp_port_out8(0x0A, 5);
    sbdsp_port_out8(0x0C, 0);
    sbdsp_port_out8(0x0B, 0x49);
    sbdsp_port_out8(0x02, (uint8_t)phys);
    sbdsp_port_out8(0x02, (uint8_t)(phys >> 8));
    sbdsp_port_out8(0x03, (uint8_t)(count - 1));
    sbdsp_port_out8(0x03, (uint8_t)((count - 1) >> 8));
    sbdsp_port_out8(0x83, (uint8_t)(phys >> 16));
    sbdsp_port_out8(0x0A, 1);
}

/* 40h tc, then 14h count - 1. */
static void dsp_play(uint8_t tc, uint16_t count) {
    dsp_out(0x40);
    dsp_out(tc);
    dsp_out(0x14);
    dsp_out((uint8_t)(count - 1));
    dsp_out((uint8_t)((count - 1) >> 8));
}

static uint8_t s_savedMask;

static void begin(void) {
    sbdsp_configure(true, BASE, 7, 0x0201);
    s_irqs = 0;
    bak_set_vector(0x0F, count_irq);
    machine_port_in8(0x21, &s_savedMask);
    machine_port_out8(0x21, s_savedMask & 0x7F); /* IRQ 7 on */
}

static void end(void) {
    bak_set_vector(0x0F, NULL);
    machine_port_out8(0x21, s_savedMask);
    audio_close();
    sbdsp_close();
    sbdsp_configure(true, BASE, 7, 0x0201);
}

/* At the moment t on the virtual clock, a poll: what the game's waits do. */
static void poll_at(uint64_t t) {
    clock_use_virtual(t - CLOCK_POLL_NS);
    bak_poll();
}

void unit_sbdsp_resets_and_identifies(void) {
    uint8_t v;
    begin();
    UNIT_CHECK(dsp_in(0x0E) == 0x7F); /* nothing to read */
    sbdsp_port_out8(BASE + 0x06, 0);  /* a 0 alone resets nothing */
    UNIT_CHECK(dsp_in(0x0E) == 0x7F);
    dsp_reset();
    UNIT_CHECK(dsp_in(0x0E) == 0xFF);
    UNIT_CHECK(dsp_in(0x0A) == 0xAA);
    UNIT_CHECK(dsp_in(0x0E) == 0x7F);
    UNIT_CHECK(dsp_in(0x0C) == 0x7F); /* ready for a byte */
    dsp_out(0xE1);
    UNIT_CHECK(dsp_in(0x0A) == 0x02 && dsp_in(0x0A) == 0x01); /* version 2.01 */
    dsp_out(0xE0);
    dsp_out(0xAA);
    UNIT_CHECK(dsp_in(0x0E) == 0xFF && dsp_in(0x0A) == 0x55);
    UNIT_CHECK(dsp_in(0x0A) == 0x55);             /* nothing new: the last byte again */
    UNIT_CHECK(!sbdsp_port_in8(BASE + 0x08, &v)); /* not the DSP's: left to the others */
    UNIT_CHECK(!sbdsp_port_out8(BASE + 0x09, 0));
    sbdsp_configure(false, BASE, 7, 0x0201); /* no card: the bus reads FFh */
    dsp_reset();
    UNIT_CHECK(dsp_in(0x0E) == 0xFF && dsp_in(0x0A) == 0xFF);
    sbdsp_configure(true, 0x240, 5, 0x0105); /* another base */
    UNIT_CHECK(!sbdsp_port_in8(BASE + 0x0E, &v));
    sbdsp_port_out8(0x246, 1);
    sbdsp_port_out8(0x246, 0);
    sbdsp_port_out8(0x24C, 0xE1);
    UNIT_CHECK(sbdsp_port_in8(0x24A, &v) && v == 0xAA);
    UNIT_CHECK(sbdsp_port_in8(0x24A, &v) && v == 0x01);
    UNIT_CHECK(sbdsp_port_in8(0x24A, &v) && v == 0x05);
    end();
}

void unit_sbdsp_block_interrupt_comes_on_time(void) {
    static uint8_t samples[100];
    uint8_t lo, hi;
    begin();
    clock_use_virtual(T0);
    uint16_t seg = bak_seg_register(samples, sizeof samples);
    dsp_reset();
    dma_program(samples, 100);
    SbdspState st;
    sbdsp_state(&st);
    UNIT_CHECK(st.dmaMode == 0x49 && !st.dmaMasked && st.dmaCount == 99);
    UNIT_CHECK(st.dmaPage == seg >> 12 && st.dmaAddress == (uint16_t)(seg << 4));
    sbdsp_port_out8(0x0C, 0);
    sbdsp_port_out8(0x02, 0x56); /* the low byte alone: the high one stays */
    sbdsp_port_out8(0x0C, 0);
    sbdsp_port_out8(0x0B, 0x4A); /* channel 2's mode */
    sbdsp_state(&st);
    UNIT_CHECK(st.dmaAddress == (uint16_t)((seg << 4 & 0xFF00) | 0x56) && st.dmaMode == 0x49);
    dma_program(samples, 100);
    dsp_play(0x9C, 100); /* 100 samples of 100 us at T0: due at T0 + 10 ms */
    sbdsp_state(&st);
    UNIT_CHECK(st.active && st.length == 100 && st.timeConstant == 0x9C);
    UNIT_CHECK(sbdsp_next_due_ns() == T0 + 10 * MS);
    poll_at(T0 + 5 * MS + 50 * US);
    sbdsp_port_in8(0x03, &lo); /* the flip-flop left on the high byte, */
    sbdsp_port_out8(0x0C, 0);  /* which this clears */
    sbdsp_port_in8(0x03, &lo);
    sbdsp_port_in8(0x03, &hi);
    UNIT_CHECK(lo == 99 - 50 && hi == 0); /* the count follows the clock */
    poll_at(T0 + 10 * MS - 1);
    UNIT_CHECK(s_irqs == 0);
    poll_at(T0 + 10 * MS);
    UNIT_CHECK(s_irqs == 1 && s_irqAt[0] == T0 + 10 * MS); /* at its moment, stamped so */
    UNIT_CHECK(sbdsp_next_due_ns() == 0);
    poll_at(T0 + 20 * MS);
    UNIT_CHECK(s_irqs == 1); /* once */
    /* Polled late, it still carries its moment; idle waits jump to the next block's end. */
    dsp_play(0x9C, 10); /* due at T0 + 21 ms */
    poll_at(T0 + 23 * MS);
    UNIT_CHECK(s_irqs == 2 && s_irqAt[1] == T0 + 21 * MS);
    dsp_play(0xFF, 1); /* 1 us */
    UNIT_CHECK(machine_next_due_ns() == T0 + 23 * MS + 1000);
    poll_at(T0 + 23 * MS + 1000);
    UNIT_CHECK(s_irqs == 3);
    /* Masked at the 8259, the interrupt is lost. */
    dsp_play(0x9C, 10);
    machine_port_out8(0x21, s_savedMask | 0x80);
    poll_at(T0 + 30 * MS);
    UNIT_CHECK(s_irqs == 3 && sbdsp_next_due_ns() == 0);
    bak_seg_unregister(seg);
    end();
}

void unit_sbdsp_block_reaches_the_mix(void) {
    static uint8_t samples[480];
    static float mix[9600 * 2];
    begin();
    clock_use_virtual(T0);
    uint16_t seg = bak_seg_register(samples, sizeof samples);
    memset(samples, 0xC0, sizeof samples);
    samples[479] = 0x40;
    audio_open_dump(NULL, T0);
    dsp_reset(); /* the speaker off */
    dma_program(samples, 480);
    dsp_play(0x9C, 480); /* at T0: 48 ms of 10 kHz, silent */
    clock_use_virtual(T0 + 100 * MS);
    dsp_out(0xD1);             /* the speaker on */
    dma_program(samples, 240); /* at 100 ms: the DSP takes 480, the channel gives 240 */
    dsp_play(0x9C, 480);
    audio_mix(mix, 9600); /* 200 ms */
    bool silentBefore = true, silentFirst = true;
    for (int i = 0; i < 7200; i++) {
        silentBefore &= mix[2 * i] == 0.0f;
        if (i >= 2400 && i < 4704)
            silentFirst &= mix[2 * i] == 0.0f;
    }
    UNIT_CHECK(silentBefore && silentFirst);
    UNIT_CHECK(mix[2 * 7200] == 0.5f && mix[2 * 7200 + 1] == 0.5f); /* 0xC0 at 150 ms */
    UNIT_CHECK(mix[2 * (7200 + 1147)] == 0.5f); /* sample 238, 4.8 frames a sample */
    /* 239 slides towards 240: 1150 frames are 239 samples and 2800000 of 4800000 */
    UNIT_CHECK(mix[2 * (7200 + 1150)] ==
               (float)(0xC0 * 256 - 16384 * 2800000ll / 4800000 - 32768) / 32768.0f);
    UNIT_CHECK(mix[2 * (7200 + 1152)] == 0.0f); /* past the channel's 240: silence */
    bak_seg_unregister(seg);
    end();
}

/* A held block is silent until it goes on, from where it was. */
void unit_sbdsp_held_block_is_silent(void) {
    static uint8_t samples[100];
    static float mix[4800 * 2];
    begin();
    clock_use_virtual(T0);
    uint16_t seg = bak_seg_register(samples, sizeof samples);
    memset(samples, 0xC0, sizeof samples);
    audio_open_dump(NULL, T0);
    dsp_reset();
    dsp_out(0xD1);
    dma_program(samples, 100);
    dsp_play(0x9C, 100); /* 10 ms from T0 */
    clock_use_virtual(T0 + 4 * MS);
    dsp_out(0xD0);
    clock_use_virtual(T0 + 20 * MS);
    dsp_out(0xD4);
    audio_mix(mix, 4800);              /* heard 50 ms later: 4 ms, held to 20 ms, the last 6 ms */
    UNIT_CHECK(mix[2 * 2496] == 0.5f); /* 52 ms */
    UNIT_CHECK(mix[2 * 2880] == 0.0f); /* 60 ms: held */
    UNIT_CHECK(mix[2 * 3456] == 0.5f); /* 72 ms: going on */
    UNIT_CHECK(mix[2 * 3840] == 0.0f); /* 80 ms: ended */
    bak_seg_unregister(seg);
    end();
}

void unit_sbdsp_hold_and_go_on(void) {
    static uint8_t samples[100];
    begin();
    clock_use_virtual(T0);
    uint16_t seg = bak_seg_register(samples, sizeof samples);
    dsp_reset();
    dma_program(samples, 100);
    dsp_play(0x9C, 100); /* due at T0 + 10 ms */
    clock_use_virtual(T0 + 4 * MS);
    dsp_out(0xD0); /* held 6 ms from the end */
    UNIT_CHECK(sbdsp_next_due_ns() == 0);
    poll_at(T0 + 15 * MS);
    UNIT_CHECK(s_irqs == 0);
    clock_use_virtual(T0 + 20 * MS);
    dsp_out(0xD4);
    UNIT_CHECK(sbdsp_next_due_ns() == T0 + 26 * MS);
    poll_at(T0 + 26 * MS);
    UNIT_CHECK(s_irqs == 1 && s_irqAt[0] == T0 + 26 * MS);
    /* A channel masked when the DSP starts: the block begins once it is unmasked. */
    clock_use_virtual(T0 + 30 * MS);
    sbdsp_port_out8(0x0A, 5);
    dsp_play(0x9C, 10);
    UNIT_CHECK(sbdsp_next_due_ns() == 0);
    clock_use_virtual(T0 + 33 * MS);
    sbdsp_port_out8(0x0A, 1);
    UNIT_CHECK(sbdsp_next_due_ns() == T0 + 34 * MS);
    /* A reset ends it, without its interrupt. */
    dsp_reset();
    UNIT_CHECK(sbdsp_next_due_ns() == 0);
    poll_at(T0 + 40 * MS);
    UNIT_CHECK(s_irqs == 1);
    bak_seg_unregister(seg);
    end();
}

static int s_hookCalls;

static void hook(void) {
    s_hookCalls++;
    sbdsp_complete_now();
}

/* Free-running (the differential tests), time does not pass: a poll is where the runner ends a
 * block. */
void unit_sbdsp_free_running_polls_end_blocks(void) {
    static uint8_t samples[10];
    begin();
    clock_use_virtual(T0);
    uint16_t seg = bak_seg_register(samples, sizeof samples);
    dsp_reset();
    dma_program(samples, 10);
    dsp_play(0x9C, 10);
    dsp_out(0xD0); /* held: no end */
    UNIT_CHECK(!sbdsp_complete_now() && s_irqs == 0);
    dsp_out(0xD4);
    g_platformFreeRunning = true;
    g_platformFreeRunningPoll = hook;
    s_hookCalls = 0;
    bak_poll();
    bak_idle();
    g_platformFreeRunning = false;
    g_platformFreeRunningPoll = NULL;
    UNIT_CHECK(s_hookCalls == 2);
    UNIT_CHECK(s_irqs == 1 && s_irqAt[0] == T0); /* at once, at the present */
    bak_seg_unregister(seg);
    end();
}

/* The 8237's address wraps within its 64 KB page: a block from 9FFF0h on continues at 90000h,
 * not at A0000h. */
void unit_sbdsp_dma_wraps_within_its_page(void) {
    static uint8_t a[32], b[32];
    static float mix[4800 * 2];
    begin();
    clock_use_virtual(T0);
    memset(a, 0xC0, sizeof a);
    for (int i = 0; i < 32; i++)
        b[i] = (uint8_t)(0x40 + 2 * i);
    bak_seg_register_at(0x9FFF, a, sizeof a);
    bak_seg_register_at(0x9000, b, sizeof b);
    audio_open_dump(NULL, T0);
    dsp_reset();
    dsp_out(0xD1);
    dma_program(a, 32);
    dsp_play(0x9C, 32);
    audio_mix(mix, 4800);
    UNIT_CHECK(mix[2 * (2400 + 40)] == 0.5f);                             /* sample 8: 9FFF8h */
    UNIT_CHECK(mix[2 * (2400 + 120)] == (0x52 * 256 - 32768) / 32768.0f); /* 25: 90009h */
    bak_seg_unregister(0x9FFF);
    bak_seg_unregister(0x9000);
    end();
}
