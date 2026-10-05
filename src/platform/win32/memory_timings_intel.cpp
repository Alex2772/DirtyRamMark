#include <AUI/Common/AException.h>
#include <memory_timings.h>

#include "driver.h"

using namespace memory_timings;

AJson::Object memory_timings::readIntelTimings(IntelLayout layout) {
    const auto& device = driver::instance();

    // MCHBAR is in the PCI config space of the host bridge, 00:00.0
    const auto mchbar = intelMchbarBase(layout, device.pciRead(0, 0, 0, 0x48), device.pciRead(0, 0, 0, 0x4c));
    if (mchbar == 0) {
        throw AException("MCHBAR is disabled");
    }
    auto read = [&](uint32_t offset) { return device.mmioRead(mchbar + offset); };

    const AJson::Object clock = decodeIntelClock(read(INTEL_MC_BIOS_DATA), layout);
    AJson::Object result;
    unsigned index = 0;
    for (auto base : intelChannelBases(layout)) {
        AMap<uint32_t, uint32_t> registers;
        for (auto offset : intelChannelRegisters(layout)) {
            registers[offset] = read(base + offset);
        }
        auto channel = decodeIntelChannel(registers, layout);
        if (!channel.empty()) {
            // frequency is shared by the whole memory controller
            auto& target = result["Channel {}"_format(index)];
            for (auto& [k, v] : clock) {
                target[k] = v;
            }
            for (auto& [k, v] : channel) {
                target[k] = v;
            }
        }
        ++index;
    }
    return result;
}
