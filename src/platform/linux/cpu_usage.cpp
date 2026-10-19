#include <algorithm>
#include <cstdint>
#include <AUI/IO/AFileInputStream.h>
#include <AUI/Common/AByteBuffer.h>
#include <cpu_usage.h>

AOptional<float> cpuUsageFraction() {
    // first line of /proc/stat: "cpu user nice system idle iowait irq softirq steal guest guest_nice", in USER_HZ ticks
    // for all cores together
    AString content;
    try {
        content = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream("/proc/stat")));
    } catch (const AException&) {
        return std::nullopt;
    }
    const auto lines = content.split('\n');
    if (lines.empty() || !lines.first().startsWith("cpu ")) {
        return std::nullopt;
    }
    uint64_t total = 0;
    uint64_t idle = 0;
    int field = 0;
    for (const auto& part : lines.first().split(' ')) {
        if (part.empty() || part == "cpu") {
            continue;
        }
        const auto value = part.toLong();
        if (!value) {
            return std::nullopt;
        }
        if (field < 8) {   // guest time is already included in user/nice
            total += uint64_t(*value);
        }
        if (field == 3 || field == 4) {   // idle + iowait
            idle += uint64_t(*value);
        }
        ++field;
    }
    if (field < 4) {
        return std::nullopt;
    }

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
