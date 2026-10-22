#include <array>
#include <cstring>

#include <intrin.h>

#include <AUI/Common/AException.h>
#include <AUI/Logging/ALogger.h>
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
    // The memory controller registers are reachable only from ring 0 on Windows: see pawnio.h. Its driver can be
    // installed and opened by administrators only.
    if (!privileges::isGranted()) {
        ALogger::warn("MemoryTimings") << "PawnIO driver not loaded: the process is not running as administrator";
        throw privileges::Required("The memory controller registers can be read by administrator only: the PawnIO "
                                   "driver can't be loaded without administrator rights");
    }
    auto cpu = readCpuId();
    auto timings = [&] {
        if (cpu.vendor == "AuthenticAMD") {
            auto result = readAmdTimings(cpu);
            ALogger::info("MemoryTimings") << "AMD family " << cpu.family << " model " << cpu.model << ": "
                                           << AJson::toString(result);
            return result;
        }
        if (cpu.vendor == "GenuineIntel") {
            auto layout = intelLayoutOf(cpu.family, cpu.model);
            if (!layout) {
                throw AException("Unsupported Intel CPU: family {}, model {}"_format(cpu.family, cpu.model));
            }
            auto result = readIntelTimings(*layout);
            ALogger::info("MemoryTimings") << "Intel family " << cpu.family << " model " << cpu.model << ": "
                                           << AJson::toString(result);
            return result;
        }
        throw AException("Unsupported CPU family \"{}\""_format(cpu.vendor));
    }();

    AJson::Object result;
    for (auto& [channel, t] : timings) {
        result["Timings - {}"_format(channel)] = std::move(t);
    }
    return result;
}
