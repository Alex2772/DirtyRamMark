#include <sstream>

#include <memory_timings.h>

using namespace memory_timings;

CpuId memory_timings::parseCpuInfo(const std::string& cpuinfo) {
    CpuId result;
    std::istringstream in(cpuinfo);
    for (std::string rawLine; std::getline(in, rawLine);) {
        AString line = AString::fromUtf8(rawLine);
        if (line.trim().empty()) {
            if (!result.vendor.empty()) {
                break;   // end of the first processor block; all cores report the same
            }
            continue;
        }
        auto colon = line.find(':');
        if (colon == AString::npos) {
            continue;
        }
        // keys look like "vendor_id\t: AuthenticAMD"
        auto key = line.substr(0, colon).trim().trim('\t');
        auto value = line.substr(colon + 1).trim().trim('\t');
        if (key == "vendor_id") {
            result.vendor = value;
        } else if (key == "cpu family") {
            result.family = value.toUInt().valueOr(0);
        } else if (key == "model") {
            result.model = value.toUInt().valueOr(0);
        }
    }
    return result;
}

AString memory_timings::controllerVendor(const CpuId& cpu) {
    if (cpu.vendor == "AuthenticAMD") {
        return "AMD";
    }
    if (cpu.vendor == "GenuineIntel") {
        return "Intel";
    }
    return cpu.vendor;
}

AJson::Object memory_timings::describeController(const CpuId& cpu) {
    AJson::Object result;
    result["vendor"] = controllerVendor(cpu);
    result["family"] = int(cpu.family);
    result["model"] = int(cpu.model);
    return result;
}

