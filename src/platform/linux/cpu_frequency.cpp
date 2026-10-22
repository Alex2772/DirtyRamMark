#include <algorithm>
#include <charconv>
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

/// "cpu12" -> 12
AOptional<size_t> cpuIndex(const AString& name) {
    const auto utf8 = name.toStdString();
    if (utf8.size() < 4 || utf8.compare(0, 3, "cpu") != 0) {
        return std::nullopt;
    }
    size_t index = 0;
    const auto [end, error] = std::from_chars(utf8.data() + 3, utf8.data() + utf8.size(), index);
    if (error != std::errc() || end != utf8.data() + utf8.size()) {
        return std::nullopt;   // cpufreq, cpuidle, ...
    }
    return index;
}

void setFrequency(std::vector<float>& result, size_t index, float ghz) {
    if (result.size() <= index) {
        result.resize(index + 1, 0.f);
    }
    result[index] = ghz;
}

}   // namespace

std::vector<float> cpuFrequenciesGHz() {
    // cpufreq: scaling_cur_freq in kHz, one directory per logical processor
    std::vector<float> result;
    try {
        for (const auto& cpu : APath("/sys/devices/system/cpu").listDir()) {
            const auto index = cpuIndex(cpu.filename());
            if (!index) {
                continue;
            }
            if (const auto khz = readTrimmed(cpu / "cpufreq" / "scaling_cur_freq").toInt()) {
                setFrequency(result, *index, float(*khz) / 1e6f);
            }
        }
    } catch (const AException&) {
    }
    if (std::any_of(result.begin(), result.end(), [](float f) { return f > 0.f; })) {
        return result;
    }

    // no cpufreq (VMs, some ARM boards): "cpu MHz" of /proc/cpuinfo, one line per logical processor
    result.clear();
    const auto cpuinfo = readTrimmed("/proc/cpuinfo");
    for (const auto& line : cpuinfo.split('\n')) {
        if (!line.startsWith("cpu MHz")) {
            continue;
        }
        if (const auto colon = line.find(':'); colon != AString::NPOS) {
            if (const auto mhz = AString(line.substr(colon + 1)).trim(" \t").toDouble()) {
                result.push_back(float(*mhz) / 1000.f);
            }
        }
    }
    return result;
}

AOptional<float> cpuFrequencyGHz() {
    const auto frequencies = cpuFrequenciesGHz();
    const auto fastest = std::max_element(frequencies.begin(), frequencies.end());
    if (fastest == frequencies.end() || *fastest <= 0.f) {
        return std::nullopt;
    }
    return *fastest;
}
