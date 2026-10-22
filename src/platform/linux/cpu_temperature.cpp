#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <algorithm>
#include <cstdlib>
#include <string_view>
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

/// hwmon indices change between boots and machines, so the drivers are told apart by name: nzxt-kraken3 (X53, Z53,
/// 2023), nzxt-kraken2 (X42..X72), aquacomputer_d5next, asus_rog_ryujin.
bool isCoolerDriver(const AString& name) {
    static constexpr std::string_view NAMES[] = { "x53", "z53", "kraken2023", "kraken2", "d5next", "octo", "quadro",
                                                  "highflow", "ryujin" };
    return std::find(std::begin(NAMES), std::end(NAMES), name.toStdString()) != std::end(NAMES);
}

/// "<hwmon name>:<tempN>" of the VRM sensor on the boards whose monitoring chip doesn't label it. Not verified against
/// a vendor tool: it's the input that follows the CPU load slowly, like a heatsink does.
AString knownVrmSensor() {
    static const struct {
        std::string_view board;   // /sys/class/dmi/id/board_name
        std::string_view sensor;
    } BOARDS[] = {
        { "MEG X570 UNIFY (MS-7C35)", "nct6797:temp3" },   // AUXTIN0
    };
    const auto board = readTrimmed(APath("/sys/class/dmi/id/board_name")).toStdString();
    for (const auto& known : BOARDS) {
        if (known.board == board) {
            return AString(std::string(known.sensor));
        }
    }
    return {};
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

AOptional<float> coolantTemperatureCelsius() {
    try {
        for (const auto& hwmon : APath("/sys/class/hwmon").listDir()) {
            const auto name = readTrimmed(hwmon / "name");
            if (!isCoolerDriver(name)) {
                continue;
            }
            // prefer the sensor that says so (these devices have a few), temp1 is the liquid one on the Krakens
            for (int i = 1; i <= 8; ++i) {
                const auto prefix = "temp{}"_format(i);
                const auto label = readTrimmed(hwmon / (prefix + "_label")).lowercase();
                if (label.contains("coolant") || label.contains("liquid")) {
                    if (auto t = readTemp(hwmon, prefix)) {
                        return t;
                    }
                }
            }
            if (auto t = readTemp(hwmon, "temp1")) {
                return t;
            }
        }
    } catch (const AException&) {
    }
    return std::nullopt;
}

AOptional<float> vrmTemperatureCelsius() {
    // for the boards whose monitoring chip doesn't tell which input is the VRM (MSI + nct6797: AUXTIN0..3 are just
    // temp3..temp6): DIRTY_VRM_SENSOR=<hwmon name>:<tempN>, e.g. "nct6797:temp3"; overrides the built-in table
    AString override;
    if (const char* env = std::getenv("DIRTY_VRM_SENSOR")) {
        override = AString::fromUtf8(env);
    } else {
        override = knownVrmSensor();
    }
    if (!override.empty()) {
        const auto& spec = override;
        const auto colon = spec.find(':');
        if (colon == AString::NPOS) {
            return std::nullopt;
        }
        const auto wantedName = spec.substr(0, colon).trim();
        const auto prefix = spec.substr(colon + 1).trim();
        try {
            for (const auto& hwmon : APath("/sys/class/hwmon").listDir()) {
                if (readTrimmed(hwmon / "name") == wantedName) {
                    return readTemp(hwmon, prefix);
                }
            }
        } catch (const AException&) {
        }
        return std::nullopt;
    }
    // the boards' own monitoring chips label the sensors (asus-ec-sensors: "VRM", nct6775/it87 with user labels:
    // "VRM", "MOSFET"...)
    try {
        for (const auto& hwmon : APath("/sys/class/hwmon").listDir()) {
            const bool driverIsVrm = readTrimmed(hwmon / "name").lowercase().contains("vrm");
            for (int i = 1; i <= 16; ++i) {
                const auto prefix = "temp{}"_format(i);
                const auto label = readTrimmed(hwmon / (prefix + "_label")).lowercase();
                if (driverIsVrm || label.contains("vrm") || label.contains("mosfet")) {
                    if (auto t = readTemp(hwmon, prefix)) {
                        return t;
                    }
                }
            }
        }
    } catch (const AException&) {
    }
    return std::nullopt;
}

AOptional<float> pumpRpm() {
    try {
        for (const auto& hwmon : APath("/sys/class/hwmon").listDir()) {
            const bool cooler = isCoolerDriver(readTrimmed(hwmon / "name"));
            for (int i = 1; i <= 8; ++i) {
                const auto prefix = "fan{}"_format(i);
                const auto label = readTrimmed(hwmon / (prefix + "_label")).lowercase();
                // the AIO drivers label the pump; boards label it themselves ("AIO Pump", "W_PUMP+"...)
                if (label.contains("pump")) {
                    if (auto rpm = readTrimmed(hwmon / (prefix + "_input")).toInt()) {
                        return float(*rpm);
                    }
                }
            }
            // the cooler's driver doesn't label it: fan1 is the pump on the Krakens
            if (cooler) {
                if (auto rpm = readTrimmed(hwmon / "fan1_input").toInt()) {
                    return float(*rpm);
                }
            }
        }
    } catch (const AException&) {
    }
    return std::nullopt;
}
