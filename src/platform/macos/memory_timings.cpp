#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <AUI/Common/AException.h>
#include <memory_timings.h>

using namespace memory_timings;

CpuId memory_timings::readCpuId() {
    CpuId result;
    char brand[256] = {};
    size_t size = sizeof(brand) - 1;
    if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0) {
        result.vendor = AString::fromUtf8(std::string(brand));
    }
    // machdep.cpu.family / model exist on Intel Macs only; on Apple Silicon they are absent and stay 0.
    unsigned value = 0;
    size = sizeof(value);
    if (sysctlbyname("machdep.cpu.family", &value, &size, nullptr, 0) == 0) {
        result.family = value;
    }
    size = sizeof(value);
    if (sysctlbyname("machdep.cpu.model", &value, &size, nullptr, 0) == 0) {
        result.model = value;
    }
    return result;
}

AJson::Object memoryTimings() {
    // Apple Silicon (and macOS in general) doesn't expose the memory controller registers to userspace.
    throw AException("Reading the actual DRAM timings is not supported on macOS");
}
