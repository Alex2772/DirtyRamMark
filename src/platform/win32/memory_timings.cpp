#include <array>
#include <cstring>

#include <intrin.h>

#include <AUI/Common/AException.h>
#include <memory_timings.h>
#include <privileges.h>

using namespace memory_timings;

CpuId memory_timings::readCpuId() {
    CpuId result;
#if defined(_M_X64) || defined(_M_IX86)
    std::array<int, 4> regs {};
    __cpuid(regs.data(), 0);
    char vendor[13] = {};   // the vendor string is EBX, EDX, ECX
    std::memcpy(vendor, &regs[1], 4);
    std::memcpy(vendor + 4, &regs[3], 4);
    std::memcpy(vendor + 8, &regs[2], 4);
    result.vendor = AString::fromUtf8(vendor);

    __cpuid(regs.data(), 1);
    const unsigned eax = unsigned(regs[0]);
    const unsigned baseFamily = (eax >> 8) & 0xf;
    result.family = baseFamily == 0xf ? baseFamily + ((eax >> 20) & 0xff) : baseFamily;
    result.model = (eax >> 4) & 0xf;
    if (baseFamily == 0x6 || baseFamily == 0xf) {
        result.model |= ((eax >> 16) & 0xf) << 4;
    }
#endif
    return result;
}

AJson::Object memoryTimings() {
    if (!privileges::isGranted()) {
        throw privileges::Required("Administrator rights are required to read the memory controller registers");
    }
    // The memory controller registers (AMD SMN, Intel MCHBAR) are reachable only from ring 0 on Windows, hence the
    // temporary driver (see driver.h).
    const auto cpu = readCpuId();
    AJson::Object timings;
    if (cpu.vendor == "AuthenticAMD") {
        timings = readAmdTimings(cpu);
    } else if (cpu.vendor == "GenuineIntel") {
        auto layout = intelLayoutOf(cpu.family, cpu.model);
        if (!layout) {
            throw AException("Unsupported Intel CPU: family {}, model {}"_format(cpu.family, cpu.model));
        }
        timings = readIntelTimings(*layout);
    } else {
        throw AException("Unsupported CPU vendor \"{}\""_format(cpu.vendor));
    }

    AJson::Object result;
    for (auto& [channel, t] : timings) {
        result["Timings - {}"_format(channel)] = std::move(t);
    }
    return result;
}
