#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <device_tree.h>

using namespace ui;

namespace {

/// Empty if the file can't be read: some attributes are write-only or fail while the device sleeps.
std::string readTrimmed(const APath& path) {
    try {
        return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path))).trim(" \t\r\n").toStdString();
    } catch (const AException&) {
        return {};
    }
}

/// hwmon names a channel "<kind><number>" (temp1, in0, fan2), and its attributes "<kind><number>_<attribute>".
/// https://www.kernel.org/doc/html/latest/hwmon/sysfs-interface.html
struct Attribute {
    std::string kind;      // "temp"; empty if the file doesn't belong to a channel ("update_interval")
    std::string channel;   // "temp1"
    std::string name;      // "input"; "value" for the bare "pwm1"
};

Attribute parseFileName(const std::string& file) {
    size_t digits = 0;
    while (digits < file.size() && std::isalpha(static_cast<unsigned char>(file[digits]))) {
        ++digits;
    }
    size_t end = digits;
    while (end < file.size() && std::isdigit(static_cast<unsigned char>(file[end]))) {
        ++end;
    }
    if (digits == 0 || end == digits) {
        return {};
    }
    if (end == file.size()) {
        return { file.substr(0, digits), file, "value" };
    }
    if (file[end] != '_') {
        return {};
    }
    return { file.substr(0, digits), file.substr(0, end), file.substr(end + 1) };
}

struct Unit {
    double divisor;
    const char* suffix;
    int precision;
};

/// What the kernel reports in: millidegrees, millivolts, milliamperes, microwatts, microjoules, RPM, Hz and 0.001 %.
const Unit* unitOf(const std::string& kind) {
    static const struct {
        std::string_view kind;
        Unit unit;
    } UNITS[] = {
        { "temp", { 1e3, " °C", 1 } },
        { "in", { 1e3, " V", 3 } },
        { "curr", { 1e3, " A", 3 } },
        { "power", { 1e6, " W", 3 } },
        { "energy", { 1e6, " J", 3 } },
        { "fan", { 1, " RPM", 0 } },
        { "freq", { 1e6, " MHz", 1 } },
        { "humidity", { 1e3, " %", 1 } },
    };
    for (const auto& known : UNITS) {
        if (known.kind == kind) {
            return &known.unit;
        }
    }
    return nullptr;
}

/// The attributes that hold a measurement or a limit; the others are flags, modes and the like and are shown as they are.
bool isMeasurement(const std::string& attribute) {
    static constexpr std::string_view NAMES[] = {
        "input", "min", "max", "crit", "lcrit", "emergency", "lowest", "highest", "average", "average_min", "average_max",
        "average_lowest", "average_highest", "offset", "max_hyst", "min_hyst", "crit_hyst", "lcrit_hyst", "emergency_hyst",
        "cap", "cap_min", "cap_max", "cap_hyst", "rated_min", "rated_max", "target",
    };
    return std::find(std::begin(NAMES), std::end(NAMES), attribute) != std::end(NAMES);
}

AString format(const Attribute& attribute, const std::string& raw) {
    if (const auto* unit = unitOf(attribute.kind); unit != nullptr && isMeasurement(attribute.name)) {
        long long number = 0;
        const auto [ptr, error] = std::from_chars(raw.data(), raw.data() + raw.size(), number);
        if (error == std::errc {} && ptr == raw.data() + raw.size()) {
            return AString::fromUtf8(fmt::format("{:.{}f}{}", double(number) / unit->divisor, unit->precision, unit->suffix));
        }
    }
    return AString::fromUtf8(raw);
}

/// One hwmon device: a subtree per channel ("temp1 (Tctl)": input, max, crit...), and the rest of the files as they are.
void writeDevice(DeviceTreeWriter& device, const APath& hwmon) {
    struct Channel {
        std::string label;
        AVector<std::pair<Attribute, std::string>> attributes;
    };
    AMap<std::string, Channel> channels;
    for (const auto& file : hwmon.listDir()) {
        if (!file.isRegularFileExists()) {   // device/, subsystem/, power/
            continue;
        }
        const auto fileName = file.filename().toStdString();
        if (fileName == "name" || fileName == "uevent") {
            continue;
        }
        const auto value = readTrimmed(file);
        if (value.empty()) {
            continue;
        }
        const auto attribute = parseFileName(fileName);
        if (attribute.kind.empty()) {
            device.set(AString::fromUtf8(fileName), AString::fromUtf8(value));
        } else if (attribute.name == "label") {
            channels[attribute.channel].label = value;
        } else {
            channels[attribute.channel].attributes.push_back({ attribute, value });
        }
    }
    for (const auto& [name, channel] : channels) {
        const auto key = channel.label.empty() ? name : name + " (" + channel.label + ")";
        device.group(AString::fromUtf8(key), [&](DeviceTreeWriter& group) {
            for (const auto& [attribute, value] : channel.attributes) {
                group.set(AString::fromUtf8(attribute.name), format(attribute, value));
            }
        });
    }
}

}   // namespace

bool ui::queryDeviceTree(Tree& out) {
    DeviceTreeWriter root(out);
    try {
        for (const auto& hwmon : APath("/sys/class/hwmon").listDir()) {
            const auto driver = readTrimmed(hwmon / "name");
            // hwmon indices change between boots, and there can be a few devices of a kind (nvme): so both are in the key
            const auto key = "{} ({})"_format(driver.empty() ? "?" : driver.c_str(), hwmon.filename());
            root.group(key, [&](DeviceTreeWriter& device) { writeDevice(device, hwmon); });
        }
    } catch (const AException&) {
        // no /sys/class/hwmon: the tree is empty
    }
    return root.finish();
}
