#include <cstdlib>
#include <fstream>
#include <sstream>

#include <AUI/Common/AByteBuffer.h>
#include <AUI/IO/AFileInputStream.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Platform/AProcess.h>
#include <memory_timings.h>

using namespace memory_timings;

namespace {

struct Field {
    const char* name;
    uint32_t reg;
    int hi;
    int lo;
};

// Layout is taken from ZenStates-Core (irusanov/ZenStates-Core, Dictionaries/DDR4Dictionary.cs, DDR5Dictionary.cs).
// Values are in memory clocks (MEMCLK), except where stated otherwise.
constexpr Field DDR4_FIELDS[] = {
    { "CAS Latency (CL)", 0x50204, 5, 0 },
    { "RAS to CAS Delay, Read (tRCDRD)", 0x50204, 21, 16 },
    { "RAS to CAS Delay, Write (tRCDWR)", 0x50204, 29, 24 },
    { "Row Active Time (tRAS)", 0x50204, 14, 8 },
    { "Row Cycle Time (tRC)", 0x50208, 7, 0 },
    { "Row Precharge Time (tRP)", 0x50208, 21, 16 },
    { "Row to Row Delay, Short (tRRDS)", 0x5020C, 4, 0 },
    { "Row to Row Delay, Long (tRRDL)", 0x5020C, 12, 8 },
    { "Read to Precharge (tRTP)", 0x5020C, 28, 24 },
    { "Four Activate Window (tFAW)", 0x50210, 6, 0 },
    { "Write to Read, Short (tWTRS)", 0x50214, 12, 8 },
    { "Write to Read, Long (tWTRL)", 0x50214, 22, 16 },
    { "CAS Write Latency (tCWL)", 0x50214, 5, 0 },
    { "Write Recovery (tWR)", 0x50218, 6, 0 },
    { "Refresh Interval (tREFI)", 0x50230, 15, 0 },
};

constexpr Field DDR5_FIELDS[] = {
    { "CAS Latency (CL)", 0x50204, 5, 0 },
    { "RAS to CAS Delay, Read (tRCDRD)", 0x50204, 21, 16 },
    { "RAS to CAS Delay, Write (tRCDWR)", 0x50204, 29, 24 },
    { "Row Active Time (tRAS)", 0x50204, 14, 8 },
    { "Row Cycle Time (tRC)", 0x50208, 7, 0 },
    { "Row Precharge Time (tRP)", 0x50208, 21, 16 },
    { "Row to Row Delay, Short (tRRDS)", 0x5020C, 4, 0 },
    { "Row to Row Delay, Long (tRRDL)", 0x5020C, 12, 8 },
    { "Read to Precharge (tRTP)", 0x5020C, 28, 24 },
    { "Four Activate Window (tFAW)", 0x50210, 7, 0 },
    { "Write to Read, Short (tWTRS)", 0x50214, 12, 8 },
    { "Write to Read, Long (tWTRL)", 0x50214, 22, 16 },
    { "CAS Write Latency (tCWL)", 0x50214, 5, 0 },
    { "Write Recovery (tWR)", 0x50218, 7, 0 },
    { "Refresh Interval (tREFI)", 0x50230, 15, 0 },
};

constexpr uint32_t REG_CONFIG = 0x50200;   // ratio, command rate, gear down mode
constexpr uint32_t REG_TRFC0 = 0x50260;    // DDR4 only
constexpr uint32_t REG_TRFC1 = 0x50264;    // DDR4 only

uint32_t bits(uint32_t value, int hi, int lo) {
    return (value >> lo) & (uint32_t((uint64_t(1) << (hi - lo + 1)) - 1));
}

AString clocks(uint32_t value) {
    return value == 1 ? "1 clock" : "{} clocks"_format(value);
}

uint32_t reg(const AMap<uint32_t, uint32_t>& registers, uint32_t address) {
    auto it = registers.find(address);
    return it == registers.end() ? 0 : it->second;
}

constexpr unsigned AMD_CHANNELS_TO_PROBE = 2;

/// Reads SMN registers through the index/data pair of the root complex (00:00.0, config 0x60/0x64) with a single
/// privileged setpci invocation, so the index/data sequence is not interleaved with anything of ours.
std::optional<AVector<uint32_t>> readSmn(const AVector<uint32_t>& addresses) {
    AStringVector args = { "setpci", "-s", "00:00.0" };
    for (auto address : addresses) {
        args << "60.l={:x}"_format(address) << "64.l";
    }
    auto process = AProcess::create({
      .executable = "/usr/bin/pkexec",
      .args = AProcess::ArgStringList { .list = std::move(args) },
    });
    AByteBuffer output;
    AObject::connect(process->stdOut, AObject::GENERIC_OBSERVER, [&](AByteBuffer o) { output << o; });
    process->run();
    if (process->waitForExitCode() != 0) {
        ALogger::warn("MemoryTimings") << "pkexec setpci failed";
        return std::nullopt;
    }
    AVector<uint32_t> result;
    for (const auto& line : AString::fromUtf8(output).split('\n')) {
        auto trimmed = line.trim();
        if (trimmed.empty()) {
            continue;
        }
        result << uint32_t(std::strtoul(trimmed.toStdString().c_str(), nullptr, 16));
    }
    if (result.size() != addresses.size()) {
        ALogger::warn("MemoryTimings") << "unexpected setpci output";
        return std::nullopt;
    }
    return result;
}

AJson::Object amdTimings(const CpuId& cpu) {
    auto type = amdDdrTypeOf(cpu.family, cpu.model);
    AVector<uint32_t> addresses;
    for (unsigned channel = 0; channel < AMD_CHANNELS_TO_PROBE; ++channel) {
        for (auto address : amdUmcRegisters()) {
            addresses << ((channel * AMD_UMC_CHANNEL_STRIDE) | address);
        }
    }
    auto values = readSmn(addresses);
    if (!values) {
        return {};
    }

    AJson::Object result;
    size_t i = 0;
    for (unsigned channel = 0; channel < AMD_CHANNELS_TO_PROBE; ++channel) {
        AMap<uint32_t, uint32_t> registers;
        for (auto address : amdUmcRegisters()) {
            registers[address] = (*values)[i++];
        }
        auto decoded = decodeAmdUmc(registers, type);
        if (!decoded.empty()) {
            result["Channel {}"_format(channel)] = std::move(decoded);
        }
    }
    return result;
}

}   // namespace

AmdDdrType memory_timings::amdDdrTypeOf(unsigned family, unsigned model) {
    if (family == 0x1a) {
        return AmdDdrType::DDR5;
    }
    if (family == 0x19 && ((model >= 0x10 && model <= 0x1f) || (model >= 0x40 && model <= 0x4f) ||
                           (model >= 0x60 && model <= 0x7f) || (model >= 0xa0 && model <= 0xaf))) {
        return AmdDdrType::DDR5;
    }
    return AmdDdrType::DDR4;
}

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

const AVector<uint32_t>& memory_timings::amdUmcRegisters() {
    static const AVector<uint32_t> registers = [] {
        AVector<uint32_t> result = { REG_CONFIG, REG_TRFC0, REG_TRFC1 };
        for (const auto& fields : { std::pair(std::begin(DDR4_FIELDS), std::end(DDR4_FIELDS)),
                                    std::pair(std::begin(DDR5_FIELDS), std::end(DDR5_FIELDS)) }) {
            for (auto it = fields.first; it != fields.second; ++it) {
                if (!result.contains(it->reg)) {
                    result << it->reg;
                }
            }
        }
        return result;
    }();
    return registers;
}

AJson::Object memory_timings::decodeAmdUmc(const AMap<uint32_t, uint32_t>& registers, AmdDdrType type) {
    const auto config = reg(registers, REG_CONFIG);
    if (config == 0 || config == 0xffffffff) {
        return {};   // channel is absent
    }

    // MEMCLK = ratio * 100 MHz (BCLK); data rate is twice MEMCLK.
    const double ratio = type == AmdDdrType::DDR4 ? bits(config, 6, 0) / 3.0 : bits(config, 15, 0) / 100.0;
    if (ratio <= 0) {
        return {};
    }
    const double dataRate = ratio * 100.0 * 2.0;

    AJson::Object result;
    result["Data rate"] = "{:.0f} MT/s"_format(dataRate);

    const Field* fields = type == AmdDdrType::DDR4 ? DDR4_FIELDS : DDR5_FIELDS;
    const size_t count = type == AmdDdrType::DDR4 ? std::size(DDR4_FIELDS) : std::size(DDR5_FIELDS);
    for (size_t i = 0; i < count; ++i) {
        result[fields[i].name] = clocks(bits(reg(registers, fields[i].reg), fields[i].hi, fields[i].lo));
    }

    if (type == AmdDdrType::DDR4) {
        // tRFC is stored in one of two registers; one of them holds a reset value while unused
        // (selection logic as in ZenStates-Core, Ddr4Timings.cs).
        const auto t0 = reg(registers, REG_TRFC0);
        const auto t1 = reg(registers, REG_TRFC1);
        const auto trfc = t0 != t1 ? (t0 != 0x21060138 ? t0 : t1) : t0;
        if (trfc != 0) {
            result["Refresh Cycle Time (tRFC)"] = clocks(bits(trfc, 10, 0));
            result["Refresh Cycle Time, 2x mode (tRFC2)"] = clocks(bits(trfc, 21, 11));
            result["Refresh Cycle Time, 4x mode (tRFC4)"] = clocks(bits(trfc, 31, 22));
        }
        result["Command rate"] = bits(config, 10, 10) ? "2T" : "1T";
        result["Gear down mode"] = bits(config, 11, 11) ? "Enabled" : "Disabled";
    } else {
        result["Command rate"] = bits(config, 17, 17) ? "2T" : "1T";
        result["Gear down mode"] = bits(config, 18, 18) ? "Enabled" : "Disabled";
    }
    return result;
}

CpuId memory_timings::readCpuId() {
    // procfs files report zero size, so stream-to-buffer helpers would read nothing
    std::ifstream cpuinfoFile("/proc/cpuinfo");
    return parseCpuInfo(std::string(std::istreambuf_iterator<char>(cpuinfoFile), {}));
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

AJson::Object memoryTimings() {
    auto cpu = readCpuId();
    auto timings = [&] {
        if (cpu.vendor == "AuthenticAMD") {
            auto result = amdTimings(cpu);
            ALogger::info("MemoryTimings") << "AMD family " << cpu.family << " model " << cpu.model << ": "
                                           << AJson::toString(result);
            return result;
        }
        if (cpu.vendor == "GenuineIntel") {
            auto layout = intelLayoutOf(cpu.family, cpu.model);
            if (!layout) {
                throw AException("Unsupported Intel CPU: family {}, model {}"_format(cpu.family, cpu.model));
            }
            auto result = readIntelTimings(*layout);
            ALogger::info("MemoryTimings") << "Intel family " << cpu.family << " model " << cpu.model << ": "
                                           << AJson::toString(result);
            return result;
        }
        throw AException("Unsupported CPU family \"{}\""_format(cpu.vendor));
    }();

    auto result = [&] {
        AJson::Object result;
        for (auto& [channel, t] : timings) {
            result["Timings - {}"_format(channel)] = std::move(t);
        }
        return result;
    }();
    return result;
}
