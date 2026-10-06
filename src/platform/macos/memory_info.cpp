#include <cstdint>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <AUI/Common/AException.h>
#include <memory_info.h>

AJson::Object memoryInfo() {
    // macOS has no SMBIOS access, so only what sysctl exposes is reported.
    uint64_t memsize = 0;
    size_t size = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0) != 0) {
        throw AException("sysctl hw.memsize failed");
    }
    AJson::Object result;
    result["Memory Array - Total Size"] = "{} MB"_format(memsize / (1024 * 1024));
    return result;
}
