#include <algorithm>
#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <cpu_frequency.h>

namespace {

AString readTrimmed(const APath& path) {
    try {
        return AString(AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path))).trim(" \t\r\n"));
    } catch (const AException&) {
        return {};
    }
}

}   // namespace

AOptional<float> cpuFrequencyGHz() {
    // cpufreq: scaling_cur_freq in kHz, one directory per logical processor
    AOptional<float> fastest;
    try {
        for (const auto& cpu : APath("/sys/devices/system/cpu").listDir()) {
            if (const auto khz = readTrimmed(cpu / "cpufreq" / "scaling_cur_freq").toInt()) {
                fastest = std::max(fastest.valueOr(0.f), float(*khz) / 1e6f);
            }
        }
    } catch (const AException&) {
    }
    if (fastest) {
        return fastest;
    }

    // no cpufreq (VMs, some ARM boards): "cpu MHz" of /proc/cpuinfo
    const auto cpuinfo = readTrimmed("/proc/cpuinfo");
    for (const auto& line : cpuinfo.split('\n')) {
        if (!line.startsWith("cpu MHz")) {
            continue;
        }
        if (const auto colon = line.find(':'); colon != AString::NPOS) {
            if (const auto mhz = AString(line.substr(colon + 1)).trim(" \t").toDouble()) {
                fastest = std::max(fastest.valueOr(0.f), float(*mhz) / 1000.f);
            }
        }
    }
    return fastest;
}
