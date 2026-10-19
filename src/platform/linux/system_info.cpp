#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <cstdlib>
#include <regex>
#include <set>

#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <AUI/Platform/AProcess.h>
#include <system_info.h>

namespace {

AOptional<AString> readFile(const APath& path) {
    try {
        return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path)));
    } catch (const AException&) {
        return std::nullopt;
    }
}

AString readTrimmed(const APath& path) { return AString(readFile(path).valueOr(AString {}).trim(" \t\r\n")); }

AOptional<AString> env(const char* name) {
    if (const char* v = std::getenv(name); v && *v) {
        return AString::fromUtf8(v);
    }
    return std::nullopt;
}

/// stdout of a command from PATH; std::nullopt if it failed
AOptional<AString> run(AStringVector args) {
    try {
        auto process = AProcess::create({
          .executable = "/usr/bin/env",
          .args = AProcess::ArgStringList { .list = std::move(args) },
        });
        AByteBuffer out;
        AObject::connect(process->stdOut, AObject::GENERIC_OBSERVER, [&](AByteBuffer o) { out << o; });
        process->run();
        if (process->waitForExitCode() != 0) {
            return std::nullopt;
        }
        return AString::fromUtf8(out);
    } catch (const AException&) {
        return std::nullopt;
    }
}

AOptional<AString> findVersion(const AString& text);

AOptional<AString> commandVersion(AStringVector args) {
    if (auto out = run(std::move(args))) {
        return findVersion(*out);
    }
    return std::nullopt;
}

/// First "1.2.3"-like token.
AOptional<AString> findVersion(const AString& text) {
    static const std::regex VERSION(R"(\d+(\.\d+)+)");
    const auto s = text.toStdString();
    if (std::smatch m; std::regex_search(s, m, VERSION)) {
        return AString::fromUtf8(m.str());
    }
    return std::nullopt;
}

AString unquote(const AString& gsettingsValue) {
    return AString(gsettingsValue.trim(" \t\r\n'"));
}

AOptional<AString> gsetting(const char* schema, const char* key) {
    if (auto v = run({ "gsettings", "get", schema, key })) {
        auto text = unquote(*v);
        if (!text.empty()) {
            return text;
        }
    }
    return std::nullopt;
}

uint16_t readHex(const APath& path) {
    const auto text = readTrimmed(path);
    return uint16_t(std::strtoul(text.toStdString().c_str(), nullptr, 16));
}

void addOs(AJson::Object& result) {
    const auto osRelease = system_info::parseOsRelease(
        readFile("/etc/os-release").valueOr(readFile("/usr/lib/os-release").valueOr(AString {})));
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
    const auto cpu = system_info::parseCpuInfo(readFile("/proc/cpuinfo").valueOr(AString {}));
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
    const auto mem = system_info::parseMeminfo(readFile("/proc/meminfo").valueOr(AString {}));
    if (mem.total > 0) {
        result["Memory"] = system_info::formatMemoryTotal(mem.total);
    }
    if (mem.swapTotal > 0) {
        result["Swap"] = "{} GB"_format(std::lround(double(mem.swapTotal) / (1024.0 * 1024.0 * 1024.0)));
    }
}

void addDesktop(AJson::Object& result) {
    const auto desktop = env("XDG_CURRENT_DESKTOP");
    if (!desktop) {
        return;
    }
    const auto name = desktop->substr(desktop->rfind(':') == AString::npos ? 0 : desktop->rfind(':') + 1);
    const bool gnome = name.uppercase() == "GNOME";
    const bool kde = name.uppercase() == "KDE";

    AString text = name;
    if (gnome) {
        if (auto v = commandVersion({ "gnome-shell", "--version" })) {
            text += " " + *v;
        }
    } else if (kde) {
        if (auto v = commandVersion({ "plasmashell", "--version" })) {
            text += " " + *v;
        }
    }
    result["Desktop"] = text;

    const auto session = env("XDG_SESSION_TYPE");
    const AString sessionName = session ? (*session == "wayland" ? AString("Wayland") : *session == "x11" ? AString("X11") : *session) : AString {};
    const AString wm = gnome ? "Mutter" : kde ? "KWin" : "";
    if (!wm.empty()) {
        result["WM"] = sessionName.empty() ? wm : "{} ({})"_format(wm, sessionName);
    }

    if (gnome) {
        if (auto theme = gsetting("org.gnome.desktop.interface", "gtk-theme")) {
            result["WM Theme"] = *theme;
        }
        if (auto font = gsetting("org.gnome.desktop.interface", "font-name")) {
            // "SF UI Display Medium 11" -> "SF UI Display Medium (11pt)"
            const auto space = font->rfind(' ');
            if (space != AString::npos && font->substr(space + 1).toDouble()) {
                result["Font"] = "{} ({}pt)"_format(font->substr(0, space), font->substr(space + 1));
            } else {
                result["Font"] = *font;
            }
        }
    }
}

void addDisplays(AJson::Object& result) {
    auto state = run({ "gdbus", "call", "--session", "--dest", "org.gnome.Mutter.DisplayConfig", "--object-path",
                       "/org/gnome/Mutter/DisplayConfig", "--method",
                       "org.gnome.Mutter.DisplayConfig.GetCurrentState" });
    if (!state) {
        return;
    }
    size_t i = 0;
    for (const auto& d : system_info::parseMutterDisplays(*state)) {
        AString text = "{}: {}x{}"_format(d.name, d.width, d.height);
        if (d.inches > 0) {
            text += " in {}\""_format(int(d.inches));
        }
        text += ", {} Hz"_format(std::lround(d.refresh));
        AStringVector tags;
        if (d.builtin) {
            tags << "Built-in";
        }
        if (d.primary) {
            tags << "Primary";
        }
        if (!tags.empty()) {
            text += " ({})"_format(tags.join(", "));
        }
        result["Display {}"_format(++i)] = text;
    }
}

/// "nvme0n1p3" -> "nvme0n1", "sda2" -> "sda"; empty for virtual devices (/dev/mapper/...).
AString parentDisk(const AString& device) {
    const auto name = device.substr(device.rfind('/') + 1);
    static const std::regex PARTITION(R"((nvme\d+n\d+|mmcblk\d+)p\d+|([a-z]+)\d+)");
    const auto s = name.toStdString();
    if (std::smatch m; std::regex_match(s, m, PARTITION)) {
        return AString::fromUtf8(m[1].matched ? m[1].str() : m[2].str());
    }
    return (APath("/sys/block") / name).exists() ? AString(name) : AString {};
}

uint64_t diskBytes(const AString& disk) {
    return uint64_t(readTrimmed(APath("/sys/block") / disk / "size").toLong().valueOr(0)) * 512;
}

void addDisk(AJson::Object& result) {
    // "Disk NN (label)": NN keeps the order, the label is what is shown
    size_t index = 0;
    const auto add = [&](const AString& label, const AString& text) {
        result["Disk {:02} ({})"_format(++index, label)] = text;
    };
    const auto modelOf = [](const AString& disk) {
        return disk.empty() ? AString {} : readTrimmed(APath("/sys/block") / disk / "device" / "model");
    };

    std::set<AString> listed;
    for (const auto& mount : system_info::parseMounts(readFile("/proc/mounts").valueOr(AString {}))) {
        struct statvfs st {};
        if (statvfs(mount.mountPoint.toStdString().c_str(), &st) != 0 || st.f_blocks == 0) {
            continue;
        }
        const uint64_t fsTotal = uint64_t(st.f_blocks) * st.f_frsize;
        const uint64_t used = fsTotal - uint64_t(st.f_bfree) * st.f_frsize;
        const auto disk = parentDisk(mount.device);
        listed.insert(disk);
        const auto nominal = disk.empty() ? 0 : diskBytes(disk);
        // immutable distros bind the real root volume to these
        const bool isRoot = mount.mountPoint == "/" || mount.mountPoint == "/etc" || mount.mountPoint == "/sysroot" ||
                            mount.mountPoint == "/var";
        const auto model = modelOf(disk);
        add(isRoot ? AString("/") : mount.mountPoint,
            "{}{} / {}"_format(model.empty() ? AString {} : model + ": ", system_info::formatDiskBytes(used),
                               system_info::formatDiskBytes(nominal > 0 ? nominal : fsTotal)));
    }

    // the rest of the physical disks
    AVector<AString> names;
    for (const auto& entry : APath("/sys/block").listDir()) {
        const auto name = entry.filename();
        if (listed.contains(name) || name.startsWith("loop") || name.startsWith("zram") || name.startsWith("ram") ||
            name.startsWith("dm-") || name.startsWith("sr") || name.startsWith("fd") || name.startsWith("md") ||
            diskBytes(name) == 0) {
            continue;
        }
        names << name;
    }
    names.sort();
    for (const auto& name : names) {
        const auto model = modelOf(name);
        add(name, "{}{}"_format(model.empty() ? AString {} : model + ": ", system_info::formatDiskBytes(diskBytes(name))));
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

    addDesktop(result);
    addDisplays(result);
    if (auto shell = env("SHELL")) {
        AString text = shell->substr(shell->rfind('/') == AString::npos ? 0 : shell->rfind('/') + 1);
        if (auto version = commandVersion({ *shell, "--version" })) {
            text += " " + *version;
        }
        result["Shell"] = text;
    }
    if (auto locale = env("LANG")) {
        result["Locale"] = *locale;
    }
    return result;
}
