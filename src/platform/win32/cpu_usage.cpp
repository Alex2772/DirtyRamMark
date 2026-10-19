#include <algorithm>
#include <cstdint>
#include <windows.h>
#include <cpu_usage.h>

namespace {

uint64_t toTicks(const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

}   // namespace

AOptional<float> cpuUsageFraction() {
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return std::nullopt;
    }
    // kernel time includes idle time
    const uint64_t idle = toTicks(idleTime);
    const uint64_t total = toTicks(kernelTime) + toTicks(userTime);

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
