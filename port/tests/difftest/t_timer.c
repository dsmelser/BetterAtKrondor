/* INPUT/TIMER.ASM: the IRQ 0 driver and callback registry (port/asm/timer.c). Callbacks
 * are 16 probes that log their number; the BIOS handler the ISR chains to is a probe. */
#include <string.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/INPUT/TIMER.H"
#include "bak_game_end.h"

static unsigned char s_log[4096];
static unsigned s_logged;
static unsigned s_biosCalls;

#define PROBE(k)                                                                                   \
    static void probe_##k(void) {                                                                  \
        if (s_logged < sizeof s_log)                                                               \
            s_log[s_logged++] = k;                                                                 \
    }
PROBE(0)
PROBE(1) PROBE(2) PROBE(3) PROBE(4) PROBE(5) PROBE(6) PROBE(7) PROBE(8) PROBE(9) PROBE(10) PROBE(11)
    PROBE(12) PROBE(13) PROBE(14) PROBE(15) static void (*const s_probes[16])(void) = {
        probe_0, probe_1, probe_2,  probe_3,  probe_4,  probe_5,  probe_6,  probe_7,
        probe_8, probe_9, probe_10, probe_11, probe_12, probe_13, probe_14, probe_15};

static void bios_probe(void) {
    s_biosCalls++;
}

/* timer.alloc u8 probe, u16 interval -> s16 slot */
static void cmd_alloc(DtIn *in, DtOut *out) {
    unsigned k = dt_u8(in) & 15;
    s16 interval = (s16)dt_u16(in);
    dt_put_u16(out, (u16)registry_slot_alloc((FARFN)s_probes[k], interval));
}

static void cmd_free(DtIn *in, DtOut *out) {
    dt_put_u16(out, (u16)registry_slot_free((s16)dt_u16(in)));
}

/* timer.set_callbacks u8[16] probe per slot */
static void cmd_set_callbacks(DtIn *in, DtOut *out) {
    (void)out;
    for (unsigned slot = 0; slot < 16; slot++) {
        FARFN fn = (FARFN)s_probes[dt_u8(in) & 15];
        memcpy(&g_aTimerCallback[slot], &fn, sizeof fn);
    }
}

/* timer.hook_bios: install over a probe as the BIOS handler, which the ISR chains to */
static void cmd_hook_bios(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    bak_set_vector(0x08, bios_probe);
    g_timer_installed = 0;
    timer_install(13);
}

/* timer.ticks u16 n -> u16 BIOS chains, the callback log */
static void cmd_ticks(DtIn *in, DtOut *out) {
    u16 n = dt_u16(in);
    s_logged = 0;
    s_biosCalls = 0;
    for (u16 i = 0; i < n; i++)
        timer_irq0_dispatch();
    dt_put_u16(out, (u16)s_biosCalls);
    dt_put_bytes(out, s_log, s_logged);
}

/* timer.install u16 frequency (from uninstalled) */
static void cmd_install(DtIn *in, DtOut *out) {
    (void)out;
    g_timer_installed = 0;
    timer_install(dt_u16(in));
}

static void cmd_restore(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_u16(out, (u16)timer_restore_default());
}

const DtEntry dt_timer_entries[] = {
    DT_COMMAND("timer.alloc", cmd_alloc),
    DT_COMMAND("timer.free", cmd_free),
    DT_COMMAND("timer.set_callbacks", cmd_set_callbacks),
    DT_COMMAND("timer.hook_bios", cmd_hook_bios),
    DT_COMMAND("timer.ticks", cmd_ticks),
    DT_COMMAND("timer.install", cmd_install),
    DT_COMMAND("timer.restore", cmd_restore),
    DT_GLOBAL(g_timer_installed),
    DT_GLOBAL(g_nFrameTickCountdownValue),
    DT_GLOBAL(g_nTimerPitDivisor),
    DT_GLOBAL(g_nTimerBiosChainReload),
    DT_GLOBAL(g_nTimerBiosChainCountdown),
    DT_GLOBAL(g_registry_slot_mask),
    DT_GLOBAL(g_aTimerSched),
    DT_END,
};
