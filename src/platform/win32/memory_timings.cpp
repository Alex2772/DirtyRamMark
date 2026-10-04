#include <array>
#include <cstring>

#include <intrin.h>

#include <AUI/Common/AException.h>
#include <memory_timings.h>

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
    // The memory controller registers (AMD SMN, Intel MCHBAR) are reachable only from ring 0 on Windows, which needs a
    // signed kernel driver.
    throw AException("Reading the actual DRAM timings is not supported on Windows yet");
}
