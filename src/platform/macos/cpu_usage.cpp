#include <algorithm>
#include <cstdint>
#include <mach/mach.h>
#include <cpu_usage.h>

AOptional<float> cpuUsageFraction() {
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&info), &count) != KERN_SUCCESS) {
        return std::nullopt;
    }
    uint64_t total = 0;
    for (unsigned i = 0; i < CPU_STATE_MAX; ++i) {
        total += info.cpu_ticks[i];
    }
    const uint64_t idle = info.cpu_ticks[CPU_STATE_IDLE];

    static uint64_t previousTotal = 0;
    static uint64_t previousIdle = 0;
    static float last = 0.f;
    const auto deltaTotal = total - previousTotal;
    const auto deltaIdle = idle - previousIdle;
    previousTotal = total;
    previousIdle = idle;
    if (deltaTotal > 0) {
        last = std::clamp(1.f - float(deltaIdle) / float(deltaTotal), 0.f, 1.f);
    }
    return last;
}
