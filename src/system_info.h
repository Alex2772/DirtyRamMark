#pragma once

#include <cstdint>
#include <AUI/Common/AOptional.h>
#include <AUI/Common/AMap.h>
#include <AUI/Common/AString.h>
#include <AUI/Common/AStringVector.h>
#include <AUI/Json/AJson.h>

/**
 * @brief General information about the system (aka fastfetch): OS, host, kernel, uptime, CPU, GPU, memory, disk...
 * @return flat object of "<name>": "<value>"; what can't be determined is omitted.
 * @details
 * Doesn't need elevated privileges. Implemented per platform (src/platform/<os>/system_info.cpp) on top of the
 * helpers of the system_info namespace.
 */
AJson::Object systemInfo();

namespace system_info {

/// Contents of /etc/os-release: KEY=value or KEY="value" lines, comments ignored.
AMap<AString, AString> parseOsRelease(const AString& osRelease);

struct CpuInfo {
    AString model;
    unsigned threads = 0;   ///< logical processors
    unsigned cores = 0;     ///< physical cores
    double mhz = 0;         ///< current frequency of the first processor; 0 if unknown
};

/// Contents of /proc/cpuinfo (x86 and ARM flavours).
CpuInfo parseCpuInfo(const AString& cpuInfo);

struct MemInfo {
    uint64_t total = 0;       ///< bytes
    uint64_t available = 0;   ///< bytes
    uint64_t swapTotal = 0;   ///< bytes
    uint64_t swapFree = 0;    ///< bytes
};

/// Contents of /proc/meminfo.
MemInfo parseMeminfo(const AString& memInfo);

struct Mount {
    AString device;
    AString mountPoint;
    AString fileSystem;
};

/// Contents of /proc/mounts: only the mounts of real (disk) filesystems, one per device, without boot partitions.
AVector<Mount> parseMounts(const AString& mounts);

/// Vendor and device names from pci.ids (https://pci-ids.ucw.cz); std::nullopt if the vendor is unknown.
struct PciName {
    AString vendor;
    AString device;   ///< empty if the device is unknown
};
AOptional<PciName> lookupPci(const AString& pciIds, uint16_t vendor, uint16_t device);

/// Vendor name as it is commonly written ("Advanced Micro Devices, Inc. [AMD/ATI]" -> "AMD").
AString shortVendorName(const AString& vendor);

/// "NVIDIA GA104 [GeForce RTX 3070]" -> "GeForce RTX 3070" (the marketing name is in the last square brackets).
AString marketingDeviceName(const AString& device);

/// "15.6 GiB" (binary units).
AString formatBytes(uint64_t bytes);

/// "2 days, 3 hours, 5 mins"; seconds are shown only if the uptime is less than a minute.
AString formatUptime(uint64_t seconds);

/// "used / total (NN%)", e.g. "4.2 GiB / 15.6 GiB (27%)".
AString formatUsage(uint64_t used, uint64_t total);

/// "128 GB": total RAM rounded up to the nominal installed size.
AString formatMemoryTotal(uint64_t total);

struct Display {
    AString connector;     ///< "DP-1"
    AString name;          ///< "27GN950"
    int x = 0;             ///< position in the logical layout
    int width = 0;
    int height = 0;
    double refresh = 0;    ///< Hz
    double inches = 0;     ///< 0 if unknown
    bool primary = false;
    bool builtin = false;
};

/// Output of `gdbus call ... org.gnome.Mutter.DisplayConfig.GetCurrentState`; sorted left to right.
AVector<Display> parseMutterDisplays(const AString& state);

/// Marketing (decimal) size of a disk: "1.75 TB", "500 GB".
AString formatDiskBytes(uint64_t bytes);

/// Joins the non-empty parts with a space (SMBIOS placeholders such as "To be filled by O.E.M." are dropped).
AString joinMeaningful(const AStringVector& parts);

}   // namespace system_info
