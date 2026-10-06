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

