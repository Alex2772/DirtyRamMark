#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <cstdlib>

#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <system_info.h>

namespace {

std::optional<AString> readFile(const APath& path) {
    try {
        return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path)));
    } catch (const AException&) {
        return std::nullopt;
    }
}

AString readTrimmed(const APath& path) { return system_info::trim(readFile(path).value_or(AString {})); }

std::optional<AString> env(const char* name) {
    if (const char* v = std::getenv(name); v && *v) {
        return AString::fromUtf8(v);
    }
    return std::nullopt;
}

uint16_t readHex(const APath& path) {
    const auto text = readTrimmed(path);
    return uint16_t(std::strtoul(text.toStdString().c_str(), nullptr, 16));
}

void addOs(AJson::Object& result) {
    const auto osRelease = system_info::parseOsRelease(
        readFile("/etc/os-release").value_or(readFile("/usr/lib/os-release").value_or(AString {})));
    AString name;
    if (auto it = osRelease.find("PRETTY_NAME"); it != osRelease.end()) {
        name = it->second;
    } else if (auto it = osRelease.find("NAME"); it != osRelease.end()) {
        name = it->second;
    }
    utsname un {};
    if (uname(&un) == 0) {
        if (name.empty()) {
            name = AString::fromUtf8(un.sysname);
        }
        name += " " + AString::fromUtf8(un.machine);
        result["Kernel"] = "{} {}"_format(un.sysname, un.release);
    }
    result["OS"] = name;
}

void addDmi(AJson::Object& result) {
    const APath dmi = "/sys/class/dmi/id";
    const auto host = system_info::joinMeaningful({ readTrimmed(dmi / "sys_vendor"), readTrimmed(dmi / "product_name"),
                                                    readTrimmed(dmi / "product_version") });
    if (!host.empty()) {
        result["Host"] = host;
    }
    const auto board = system_info::joinMeaningful({ readTrimmed(dmi / "board_vendor"), readTrimmed(dmi / "board_name") });
    if (!board.empty()) {
        result["Board"] = board;
    }
    const auto bios = system_info::joinMeaningful(
        { readTrimmed(dmi / "bios_vendor"), readTrimmed(dmi / "bios_version"), readTrimmed(dmi / "bios_date") });
    if (!bios.empty()) {
        result["BIOS"] = bios;
    }
}

void addCpu(AJson::Object& result) {
    const auto cpu = system_info::parseCpuInfo(readFile("/proc/cpuinfo").value_or(AString {}));
    AString text = cpu.model.empty() ? AString("Unknown") : cpu.model;
    if (cpu.threads > 0) {
        text += cpu.cores != cpu.threads ? " ({} cores, {} threads)"_format(cpu.cores, cpu.threads)
                                         : " ({} cores)"_format(cpu.cores);
    }
    // cpufreq reports kHz; /proc/cpuinfo only has the current (fluctuating) frequency
    const auto maxKhz = readTrimmed("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq").toInt().valueOr(0);
    if (maxKhz > 0) {
        text += " @ {:.2f} GHz"_format(double(maxKhz) / 1'000'000.0);
    } else if (cpu.mhz > 0) {
        text += " @ {:.2f} GHz"_format(cpu.mhz / 1000.0);
    }
    result["CPU"] = text;
}

void addGpus(AJson::Object& result) {
    const auto pciIds = [] {
        for (const char* path : { "/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids" }) {
            if (auto text = readFile(path)) {
                return *text;
            }
        }
        return AString {};
    }();

    AVector<AString> gpus;
    for (const auto& card : APath("/sys/bus/pci/devices").listDir()) {
        // 0x03xxxx: display controller
        const auto deviceClass = readTrimmed(card / "class");
        if (!deviceClass.startsWith("0x03")) {
            continue;
        }
        const auto vendorId = readHex(card / "vendor");
        const auto deviceId = readHex(card / "device");
        if (auto name = system_info::lookupPci(pciIds, vendorId, deviceId)) {
            gpus << "{} {}"_format(system_info::shortVendorName(name->vendor),
                                   name->device.empty() ? "{:04x}"_format(deviceId)
                                                        : system_info::marketingDeviceName(name->device));
        } else {
            gpus << "{:04x}:{:04x}"_format(vendorId, deviceId);
        }
    }
    gpus.sort();
    for (size_t i = 0; i < gpus.size(); ++i) {
        result[gpus.size() == 1 ? AString("GPU") : "GPU {}"_format(i + 1)] = gpus[i];
    }
}

void addMemory(AJson::Object& result) {
    const auto mem = system_info::parseMeminfo(readFile("/proc/meminfo").value_or(AString {}));
    if (mem.total > 0) {
        result["Memory"] = system_info::formatUsage(mem.total - std::min(mem.available, mem.total), mem.total);
    }
    if (mem.swapTotal > 0) {
        result["Swap"] = system_info::formatUsage(mem.swapTotal - std::min(mem.swapFree, mem.swapTotal), mem.swapTotal);
    }
}

void addDisk(AJson::Object& result) {
    // composefs/overlay roots (immutable distros) are tiny and say nothing: list real filesystems
    for (const auto& mount : system_info::parseMounts(readFile("/proc/mounts").value_or(AString {}))) {
        struct statvfs st {};
        if (statvfs(mount.mountPoint.toStdString().c_str(), &st) != 0 || st.f_blocks == 0) {
            continue;
        }
        const uint64_t total = uint64_t(st.f_blocks) * st.f_frsize;
        const uint64_t used = total - uint64_t(st.f_bfree) * st.f_frsize;
        result["Disk ({})"_format(mount.mountPoint)] =
            system_info::formatUsage(used, total) + " - " + mount.fileSystem;
    }
}

}   // namespace

AJson::Object systemInfo() {
    AJson::Object result;
    addOs(result);
    addDmi(result);
    if (struct sysinfo si {}; sysinfo(&si) == 0) {
        result["Uptime"] = system_info::formatUptime(uint64_t(si.uptime));
    }
    addCpu(result);
    addGpus(result);
    addMemory(result);
    addDisk(result);

    if (auto desktop = env("XDG_CURRENT_DESKTOP")) {
        auto session = env("XDG_SESSION_TYPE");
        result["Desktop"] = session ? "{} ({})"_format(*desktop, *session) : *desktop;
    }
    if (auto shell = env("SHELL")) {
        result["Shell"] = *shell;
    }
    if (auto locale = env("LANG")) {
        result["Locale"] = *locale;
    }
    return result;
}
