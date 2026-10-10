#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <ctime>
#include <string>

#include <system_info.h>

namespace {

std::optional<std::string> sysctlString(const char* name) {
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return std::nullopt;
    }
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) {
        return std::nullopt;
    }
    value.resize(strnlen(value.c_str(), size));
    return value;
}

template <class T>
std::optional<T> sysctlValue(const char* name) {
    T value {};
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) {
        return std::nullopt;
    }
    return value;
}

}   // namespace

AJson::Object systemInfo() {
    AJson::Object result;
    utsname un {};
    if (uname(&un) == 0) {
        result["OS"] = "macOS {}"_format(un.machine);
        result["Kernel"] = "{} {}"_format(un.sysname, un.release);
    }
    if (auto model = sysctlString("hw.model")) {
        result["Host"] = AString::fromUtf8(*model);
    }
    if (auto boot = sysctlValue<timeval>("kern.boottime")) {
        result["Uptime"] = system_info::formatUptime(uint64_t(std::time(nullptr) - boot->tv_sec));
    }
    if (auto brand = sysctlString("machdep.cpu.brand_string")) {
        AString cpu = AString::fromUtf8(*brand);
        if (auto cores = sysctlValue<int>("hw.physicalcpu"), threads = sysctlValue<int>("hw.logicalcpu"); cores && threads) {
            cpu += " ({} cores, {} threads)"_format(*cores, *threads);
        }
        result["CPU"] = cpu;
    }
    if (auto memsize = sysctlValue<uint64_t>("hw.memsize")) {
        result["Memory"] = system_info::formatBytes(*memsize);
    }
    if (struct statfs st {}; statfs("/", &st) == 0) {
        const uint64_t total = uint64_t(st.f_blocks) * st.f_bsize;
        result["Disk (/)"] = system_info::formatUsage(total - uint64_t(st.f_bfree) * st.f_bsize, total);
    }
    return result;
}
