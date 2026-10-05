#include <memory_timings.h>

#include "driver.h"

using namespace memory_timings;

namespace {

constexpr unsigned AMD_CHANNELS_TO_PROBE = 2;

}   // namespace

AJson::Object memory_timings::readAmdTimings(const CpuId& cpu) {
    const auto type = amdDdrTypeOf(cpu.family, cpu.model);
    const auto& device = driver::instance();

    AJson::Object result;
    for (unsigned channel = 0; channel < AMD_CHANNELS_TO_PROBE; ++channel) {
        AMap<uint32_t, uint32_t> registers;
        for (auto address : amdUmcRegisters()) {
            registers[address] = device.smnRead((channel * AMD_UMC_CHANNEL_STRIDE) | address);
        }
        auto decoded = decodeAmdUmc(registers, type);
        if (!decoded.empty()) {
            result["Channel {}"_format(channel)] = std::move(decoded);
        }
    }
    return result;
}
