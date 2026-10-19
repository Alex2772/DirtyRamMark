#include <algorithm>
#include <set>
#include <cmath>
#include <regex>
#include <range/v3/all.hpp>

#include <memory_info.h>
#include <system_info.h>

namespace {

/// Whitespace of /proc and /sys: AString::trim() with a single symbol would leave tabs and line breaks.
constexpr AStringView WHITESPACE = " \t\r\n";

/// Splits "key<sep>value" at the first separator; both parts are trimmed.
AOptional<std::pair<AString, AString>> splitOnce(const AString& line, char separator) {
    const auto pos = line.find(separator);
    if (pos == AString::npos) {
        return std::nullopt;
    }
    return std::make_pair(AString(line.substr(0, pos).trim(WHITESPACE)), AString(line.substr(pos + 1).trim(WHITESPACE)));
}

uint64_t parseUnsigned(const AString& s) {
    uint64_t result = 0;
    for (auto c : s) {
        if (c < '0' || c > '9') {
            break;
        }
        result = result * 10 + (c - '0');
    }
    return result;
}

/// "/proc/meminfo" value: "16384 kB".
uint64_t parseKilobytes(const AString& value) { return parseUnsigned(value) * 1024; }

}   // namespace

AMap<AString, AString> system_info::parseOsRelease(const AString& osRelease) {
    AMap<AString, AString> result;
    for (const auto& rawLine : osRelease.replacedAll("\r", "").split('\n')) {
        const AString line(rawLine.trim(WHITESPACE));
        if (line.empty() || line.startsWith('#')) {
            continue;
        }
        auto kv = splitOnce(line, '=');
        if (!kv) {
            continue;
        }
        auto& [key, value] = *kv;
        if (value.length() >= 2 && (value.startsWith('"') || value.startsWith('\'')) && value.endsWith(value[0])) {
            value = value.substr(1, value.length() - 2);
        }
        result[key] = value;
    }
    return result;
}

system_info::CpuInfo system_info::parseCpuInfo(const AString& cpuInfo) {
    CpuInfo result;
    std::set<std::pair<AString, AString>> uniqueCores;   // (physical id, core id)
    AString physicalId = "0";
    AString coreId;
    bool hasCoreId = false;
    bool inProcessor = false;
    auto flushProcessor = [&] {
        if (inProcessor && hasCoreId) {
            uniqueCores.insert({ physicalId, coreId });
        }
        physicalId = "0";
        coreId.clear();
        hasCoreId = false;
    };

    for (const auto& rawLine : cpuInfo.replacedAll("\r", "").split('\n')) {
        auto kv = splitOnce(rawLine, ':');
        if (!kv) {
            continue;
        }
        const auto& [key, value] = *kv;
        if (key == "processor") {
            flushProcessor();
            inProcessor = true;
            ++result.threads;
        } else if (key == "model name" || key == "Model Name" || key == "Hardware" || key == "cpu model") {
            if (result.model.empty() || key == "model name") {
                result.model = value;
            }
        } else if (key == "physical id") {
            physicalId = value;
        } else if (key == "core id") {
            coreId = value;
            hasCoreId = true;
        } else if (key == "cpu MHz" && result.mhz == 0) {
            result.mhz = value.toDouble().valueOr(0.0);
        }
    }
    flushProcessor();
    result.cores = uniqueCores.empty() ? result.threads : unsigned(uniqueCores.size());
    return result;
}

system_info::MemInfo system_info::parseMeminfo(const AString& memInfo) {
    MemInfo result;
    bool hasAvailable = false;
    uint64_t free = 0;
    uint64_t buffers = 0;
    uint64_t cached = 0;
    for (const auto& line : memInfo.replacedAll("\r", "").split('\n')) {
        auto kv = splitOnce(line, ':');
        if (!kv) {
            continue;
        }
        const auto& [key, value] = *kv;
        if (key == "MemTotal") {
            result.total = parseKilobytes(value);
        } else if (key == "MemAvailable") {
            result.available = parseKilobytes(value);
            hasAvailable = true;
        } else if (key == "MemFree") {
            free = parseKilobytes(value);
        } else if (key == "Buffers") {
            buffers = parseKilobytes(value);
        } else if (key == "Cached") {
            cached = parseKilobytes(value);
        } else if (key == "SwapTotal") {
            result.swapTotal = parseKilobytes(value);
        } else if (key == "SwapFree") {
            result.swapFree = parseKilobytes(value);
        }
    }
    if (!hasAvailable) {   // kernels older than 3.14
        result.available = free + buffers + cached;
    }
    return result;
}

AOptional<system_info::PciName> system_info::lookupPci(const AString& pciIds, uint16_t vendor, uint16_t device) {
    const auto vendorId = "{:04x}"_format(vendor);
    const auto deviceId = "{:04x}"_format(device);

    AOptional<PciName> result;
    for (const auto& rawLine : pciIds.replacedAll("\r", "").split('\n')) {
        if (rawLine.empty() || rawLine.startsWith('#')) {
            continue;
        }
        if (rawLine.startsWith('\t')) {
            // device line ("\t1234  Name"); sub-devices (two tabs) are of no interest
            if (!result || rawLine.startsWith("\t\t") || !result->device.empty()) {
                continue;
            }
            if (rawLine.length() > 6 && rawLine.substr(1, 4).lowercase() == deviceId) {
                result->device = rawLine.substr(5).trim();
            }
            continue;
        }
        if (result) {
            break;   // the next vendor: ours is finished
        }
        if (rawLine.length() > 6 && rawLine.substr(0, 4).lowercase() == vendorId) {
            result = PciName { .vendor = rawLine.substr(4).trim() };
        }
    }
    return result;
}

AVector<system_info::Mount> system_info::parseMounts(const AString& mounts) {
    static const std::set<AString> REAL = { "ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "zfs", "vfat", "exfat",
                                            "ntfs", "ntfs3", "fuseblk", "jfs", "reiserfs", "bcachefs" };
    AVector<Mount> result;
    for (const auto& line : mounts.replacedAll("\r", "").split('\n')) {
        const auto fields = line.split(' ');
        if (fields.size() < 3 || !fields[0].startsWith('/') || !REAL.contains(fields[2])) {
            continue;
        }
        // /proc/mounts escapes spaces as \040
        auto mountPoint = fields[1].replacedAll("\\040", " ");
        if (mountPoint.startsWith("/boot") || mountPoint.startsWith("/efi")) {
            continue;
        }
        // btrfs subvolumes and bind mounts of the same device: keep the shortest mount point
        auto same = std::find_if(result.begin(), result.end(), [&](const Mount& m) { return m.device == fields[0]; });
        if (same != result.end()) {
            if (mountPoint.length() < same->mountPoint.length()) {
                same->mountPoint = std::move(mountPoint);
            }
            continue;
        }
        result << Mount { .device = fields[0], .mountPoint = std::move(mountPoint), .fileSystem = fields[2] };
    }
    return result;
}

AString system_info::shortVendorName(const AString& vendor) {
    if (vendor.contains("[AMD") || vendor.startsWith("Advanced Micro Devices")) {
        return "AMD";
    }
    if (vendor.startsWith("NVIDIA")) {
        return "NVIDIA";
    }
    if (vendor.startsWith("Intel")) {
        return "Intel";
    }
    return vendor;
}

AString system_info::marketingDeviceName(const AString& device) {
    const auto close = device.rfind(']');
    if (close == AString::npos) {
        return device;
    }
    const auto open = device.rfind('[', close);
    if (open == AString::npos || close <= open + 1) {
        return device;
    }
    return device.substr(open + 1, close - open - 1);
}

AString system_info::formatBytes(uint64_t bytes) {
    constexpr const char* UNITS[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB" };
    double value = double(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(UNITS)) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return "{} B"_format(bytes);
    }
    return "{:.2f} {}"_format(value, UNITS[unit]);
}

AString system_info::formatUptime(uint64_t seconds) {
    const auto days = seconds / 86400;
    const auto hours = seconds / 3600 % 24;
    const auto minutes = seconds / 60 % 60;
    AStringVector parts;
    auto add = [&](uint64_t n, const char* singular, const char* plural) {
        if (n > 0) {
            parts << "{} {}"_format(n, n == 1 ? singular : plural);
        }
    };
    add(days, "day", "days");
    add(hours, "hour", "hours");
    add(minutes, "min", "mins");
    if (seconds < 60) {
        return "{} secs"_format(seconds);
    }
    return parts.join(", ");
}

AString system_info::formatUsage(uint64_t used, uint64_t total) {
    const auto percent = total == 0 ? 0 : int(std::lround(double(used) * 100.0 / double(total)));
    return "{} / {} ({}%)"_format(formatBytes(used), formatBytes(total), percent);
}

AString system_info::formatMemoryTotal(uint64_t total) {
    constexpr double GIB = 1024.0 * 1024.0 * 1024.0;
    // the kernel reports slightly less than the installed amount (firmware reservations): round up to a "marketing" size.
    const double totalGb = double(total) / GIB;
    const double step = std::max(1.0, std::exp2(std::floor(std::log2(totalGb)) - 3.0));
    return "{} GB"_format(int(std::ceil(totalGb / step - 0.02) * step));
}

AString system_info::formatDiskBytes(uint64_t bytes) {
    if (bytes >= 1'000'000'000'000ull) {
        auto text = "{:.2f}"_format(double(bytes) / 1e12);
        while (text.endsWith('0')) {
            text = text.substr(0, text.length() - 1);
        }
        if (text.endsWith('.')) {
            text = text.substr(0, text.length() - 1);
        }
        return text + " TB";
    }
    const auto gb = double(bytes) / 1e9;
    return gb >= 10 ? "{} GB"_format(std::lround(gb)) : "{:.1f} GB"_format(gb);
}

AVector<system_info::Display> system_info::parseMutterDisplays(const AString& state) {
    const std::string text = state.toStdString();
    AVector<Display> result;

    // monitors: (('DP-1', 'GSM', 'LG ULTRAGEAR+', 'serial'), [modes...], {properties})
    static const std::regex HEADER(R"(\(\('([^']*)', '([^']*)', '([^']*)', '[^']*'\), \[\(')");
    static const std::regex CURRENT(R"(\('\d+x\d+@[^']*', (\d+), (\d+), ([\d.]+), [^{}]*\{[^}]*'is-current')");
    static const std::regex INCHES(R"(display-name': <'[^']*?(\d+)\\?"')");
    std::vector<std::pair<size_t, std::smatch>> headers;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), HEADER); it != std::sregex_iterator(); ++it) {
        headers.emplace_back(size_t(it->position()), *it);
    }
    const size_t layoutBegin = text.rfind("], [(");   // logical monitors follow the last monitor
    for (size_t i = 0; i < headers.size(); ++i) {
        const auto begin = headers[i].first;
        const auto end = i + 1 < headers.size() ? headers[i + 1].first : text.size();
        const std::string block = text.substr(begin, end - begin);
        Display d;
        d.connector = AString::fromUtf8(headers[i].second[1].str());
        d.name = AString::fromUtf8(headers[i].second[3].str());
        d.builtin = d.connector.startsWith("eDP") || d.connector.startsWith("LVDS") || d.connector.startsWith("DSI");
        std::smatch m;
        if (!std::regex_search(block, m, CURRENT)) {
            continue;   // disabled output
        }
        d.width = std::stoi(m[1].str());
        d.height = std::stoi(m[2].str());
        d.refresh = std::stod(m[3].str());
        if (std::regex_search(block, m, INCHES)) {
            d.inches = std::stod(m[1].str());
        }
        result << std::move(d);
    }

    // logical monitors: (x, y, scale, transform, primary, [('DP-1', ...)], {})
    static const std::regex LOGICAL(R"(\((-?\d+), -?\d+, [\d.]+, (?:uint32 )?\d+, (true|false), \[\('([^']*)')");
    if (layoutBegin != std::string::npos) {
        const auto tail = text.substr(layoutBegin);
        for (auto it = std::sregex_iterator(tail.begin(), tail.end(), LOGICAL); it != std::sregex_iterator(); ++it) {
            for (auto& d : result) {
                if (d.connector.toStdString() == (*it)[3].str()) {
                    d.x = std::stoi((*it)[1].str());
                    d.primary = (*it)[2].str() == "true";
                }
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const Display& a, const Display& b) { return a.x < b.x; });
    return result;
}

AString system_info::joinMeaningful(const AStringVector& parts) {
    AStringVector meaningful;
    for (const auto& p : parts) {
        const AString trimmed(p.trim(WHITESPACE));
        if (!memory_info::isPlaceholder(trimmed) && trimmed != "System Product Name" &&
            trimmed != "System manufacturer" && trimmed != "Default string" && trimmed != "O.E.M.") {
            meaningful << trimmed;
        }
    }
    return meaningful.join(' ');
}
