#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <cpu_temperature.h>

namespace {

AString readTrimmed(const APath& path) {
    try {
        return AString(AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path))).trim(" \t\r\n"));
    } catch (const AException&) {
        return {};
    }
}

/// Millidegrees in `<hwmon>/<name>_input` -> degrees.
AOptional<float> readTemp(const APath& hwmon, const AString& prefix) {
    if (auto millis = readTrimmed(hwmon / (prefix + "_input")).toInt()) {
        return float(*millis) / 1000.f;
    }
    return std::nullopt;
}

}   // namespace

AOptional<float> cpuTemperatureCelsius() {
    // AMD: k10temp/zenpower report Tctl (temp1); Intel: coretemp reports "Package id 0".
    AOptional<float> fallback;
    try {
        for (const auto& hwmon : APath("/sys/class/hwmon").listDir()) {
            const auto name = readTrimmed(hwmon / "name");
            if (name == "k10temp" || name == "zenpower") {
                if (auto t = readTemp(hwmon, "temp1")) {
                    return t;
                }
            } else if (name == "coretemp") {
                for (int i = 1; i <= 16; ++i) {
                    const auto prefix = "temp{}"_format(i);
                    if (readTrimmed(hwmon / (prefix + "_label")).startsWith("Package")) {
                        if (auto t = readTemp(hwmon, prefix)) {
                            return t;
                        }
                    }
                }
                fallback = readTemp(hwmon, "temp1");
            } else if (name == "cpu_thermal" || name == "soc_thermal") {   // ARM boards
                fallback = readTemp(hwmon, "temp1");
            }
        }
    } catch (const AException&) {
    }
    return fallback;
}
