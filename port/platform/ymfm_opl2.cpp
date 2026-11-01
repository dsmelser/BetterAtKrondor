// A YM3812 (OPL2) emulated by ymfm, through the C interface of ymfm_opl2.h. The AdLib driver
// never starts the chip's timers, so the interface ymfm calls back keeps its defaults.
#include "ymfm_opl2.h"

#include <new>

#include "ymfm_opl.h"

struct YmfmOpl2 {
    ymfm::ymfm_interface intf;
    ymfm::ym3812 chip{intf};
};

extern "C" {

YmfmOpl2 *ymfm_opl2_create(void) {
    YmfmOpl2 *opl = new (std::nothrow) YmfmOpl2;
    if (opl != nullptr)
        opl->chip.reset();
    return opl;
}

void ymfm_opl2_destroy(YmfmOpl2 *opl) {
    delete opl;
}

uint32_t ymfm_opl2_rate(uint32_t clockHz) {
    ymfm::ymfm_interface intf;
    ymfm::ym3812 chip{intf};
    return chip.sample_rate(clockHz);
}

void ymfm_opl2_write(YmfmOpl2 *opl, uint8_t reg, uint8_t value) {
    opl->chip.write_address(reg);
    opl->chip.write_data(value);
}

uint8_t ymfm_opl2_status(YmfmOpl2 *opl) {
    return opl->chip.read_status();
}

void ymfm_opl2_generate(YmfmOpl2 *opl, int16_t *out, int frames) {
    ymfm::ym3812::output_data data;
    for (int i = 0; i < frames; i++) {
        opl->chip.generate(&data);
        int32_t v = data.data[0];
        out[i] = static_cast<int16_t>(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}
}
