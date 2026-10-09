#include <cstdlib>

#include <memory_timings.h>

using namespace memory_timings;

// Register layouts are taken from Intel datasheets, volume 2 (Host Bridge and DRAM Controller, MCHBAR registers):
//  - 10th Gen Core (Comet Lake), doc 341078: TC_PRE 4000h, TC_ACT 4004h, TC_ODT 4070h, TC_RFTP 423Ch, MC_BIOS_DATA
//    5E04h; channel 1 is at +0400h.
//  - 12th Gen Core (Alder Lake), doc 655259: TC_PRE E000h, TC_ACT E008h, TC_ODT E070h, TC_RFTP E43Ch; channel 1 is at
//    +0800h, memory controller 1 is at +10000h.

namespace {

uint64_t bits(uint64_t value, int hi, int lo) {
    return (value >> lo) & ((uint64_t(1) << (hi - lo + 1)) - 1);
}

AString clocks(uint64_t value) {
    return value == 1 ? "1 clock" : "{} clocks"_format(value);
}

// channel-relative offsets
constexpr uint32_t SKL_PRE = 0x0, SKL_ACT = 0x4, SKL_ODT = 0x70, SKL_RFTP = 0x23c;
constexpr uint32_t ADL_PRE = 0x0, ADL_ACT = 0x8, ADL_ODT = 0x70, ADL_RFTP = 0x43c;

// reset values of TC_PRE: a channel that was never trained still holds them
constexpr uint32_t SKL_PRE_DEFAULT = 0x18863808;
constexpr uint64_t ADL_PRE_DEFAULT = 0x104070180040C008ull;

uint32_t reg(const AMap<uint32_t, uint32_t>& registers, uint32_t offset) {
    auto it = registers.find(offset);
    return it == registers.end() ? 0 : it->second;
}

}   // namespace

std::optional<IntelLayout> memory_timings::intelLayoutOf(unsigned family, unsigned model) {
    if (family != 6) {
        return std::nullopt;
    }
    switch (model) {
        case 0x4e: case 0x5e:                 // Skylake
        case 0x8e: case 0x9e:                 // Kaby Lake, Coffee Lake, Whiskey Lake, Amber Lake
        case 0xa5: case 0xa6:                 // Comet Lake
            return IntelLayout::SKYLAKE;
        case 0x97: case 0x9a: case 0xbe:      // Alder Lake S, P/M, N
        case 0xb7: case 0xba: case 0xbf:      // Raptor Lake S, P, S refresh
            return IntelLayout::ALDER;
        default:
            return std::nullopt;
    }
}

const AVector<uint32_t>& memory_timings::intelChannelBases(IntelLayout layout) {
    static const AVector<uint32_t> skylake = { 0x4000, 0x4400 };
    static const AVector<uint32_t> alder = { 0xe000, 0xe800, 0x1e000, 0x1e800 };
    return layout == IntelLayout::SKYLAKE ? skylake : alder;
}

const AVector<uint32_t>& memory_timings::intelChannelRegisters(IntelLayout layout) {
    // Alder Lake's TC_PRE is a 64-bit register: tRAS and tRCD live in its upper dword.
    static const AVector<uint32_t> skylake = { SKL_PRE, SKL_ACT, SKL_ODT, SKL_RFTP };
    static const AVector<uint32_t> alder = { ADL_PRE, ADL_PRE + 4, ADL_ACT, ADL_ODT, ADL_RFTP };
    return layout == IntelLayout::SKYLAKE ? skylake : alder;
}

uint64_t memory_timings::intelMchbarBase(IntelLayout layout, uint32_t low, uint32_t high) {
    const uint64_t value = (uint64_t(high) << 32) | low;
    if (!(value & 1)) {
        return 0;   // MCHBAREN is clear
    }
    return value & (layout == IntelLayout::SKYLAKE ? 0x7FFFFF0000ull : 0x3FFFFFE0000ull);
}

AJson::Object memory_timings::decodeIntelClock(uint32_t mcBiosData, IntelLayout layout) {
    const auto ratio = bits(mcBiosData, 7, 0);
    if (ratio < 3) {
        return {};   // 0: MC PLL is shut down; 1-2: reserved
    }
    // MC_FREQ_TYPE / REQ_TYPE: 0 = 133.33 MHz granularity, 1 = 100 MHz. QCLK is the DDR data rate.
    const bool ref100 = bits(mcBiosData, 11, 8) == 1;
    const double dataRate = ratio * (ref100 ? 100.0 : 400.0 / 3.0);
    const auto gear = layout == IntelLayout::SKYLAKE ? bits(mcBiosData, 16, 16) : bits(mcBiosData, 13, 12);

    AJson::Object result;
    result["Data rate"] = "{:.0f} MT/s"_format(dataRate);
    result["Gear"] = "Gear {}"_format(gear == 0 ? 1 : gear == 1 ? 2 : 4);
    return result;
}

AJson::Object memory_timings::decodeIntelChannel(const AMap<uint32_t, uint32_t>& registers, IntelLayout layout) {
    AJson::Object result;
    if (layout == IntelLayout::SKYLAKE) {
        const auto pre = reg(registers, SKL_PRE);
        const auto act = reg(registers, SKL_ACT);
        const auto odt = reg(registers, SKL_ODT);
        const auto rftp = reg(registers, SKL_RFTP);
        if (pre == 0 || pre == 0xffffffff || pre == SKL_PRE_DEFAULT) {
            return {};
        }
        // tRP and tRCD (read) share one field; tRCD for writes has its own
        result["CAS Latency (CL)"] = clocks(bits(odt, 21, 16));
        result["RAS to CAS Delay, Read (tRCDRD)"] = clocks(bits(pre, 5, 0));
        result["RAS to CAS Delay, Write (tRCDWR)"] = clocks(bits(act, 26, 21));
        result["Row Active Time (tRAS)"] = clocks(bits(pre, 15, 9));
        result["Row Precharge Time (tRP)"] = clocks(bits(pre, 5, 0));
        result["Row to Row Delay, Short (tRRDS)"] = clocks(bits(act, 17, 13));
        result["Row to Row Delay, Long (tRRDL)"] = clocks(bits(act, 12, 8));
        result["Read to Precharge (tRTP)"] = clocks(bits(pre, 20, 16));
        result["Write to Precharge (tWRPRE)"] = clocks(bits(pre, 31, 24));
        result["Four Activate Window (tFAW)"] = clocks(bits(act, 6, 0));
        result["CAS Write Latency (tCWL)"] = clocks(bits(odt, 27, 22));
        result["Refresh Interval (tREFI)"] = clocks(bits(rftp, 15, 0));
        result["Refresh Cycle Time (tRFC)"] = clocks(bits(rftp, 25, 16));
    } else {
        const uint64_t pre = (uint64_t(reg(registers, ADL_PRE + 4)) << 32) | reg(registers, ADL_PRE);
        const auto act = reg(registers, ADL_ACT);
        const auto odt = reg(registers, ADL_ODT);
        const auto rftp = reg(registers, ADL_RFTP);
        if (pre == 0 || pre == ~uint64_t(0) || pre == ADL_PRE_DEFAULT) {
            return {};
        }
        result["CAS Latency (CL)"] = clocks(bits(odt, 22, 16));
        // a single tRCD field serves both reads and writes
        result["RAS to CAS Delay, Read (tRCDRD)"] = clocks(bits(pre, 58, 51));
        result["RAS to CAS Delay, Write (tRCDWR)"] = clocks(bits(pre, 58, 51));
        result["Row Active Time (tRAS)"] = clocks(bits(pre, 50, 42));
        result["Row Precharge Time (tRP)"] = clocks(bits(pre, 7, 0));
        result["Row to Row Delay, Short (tRRDS)"] = clocks(bits(act, 21, 15));
        result["Row to Row Delay, Long (tRRDL)"] = clocks(bits(act, 14, 9));
        result["Read to Precharge (tRTP)"] = clocks(bits(pre, 19, 13));
        result["Write to Precharge (tWRPRE)"] = clocks(bits(pre, 41, 32));
        result["Four Activate Window (tFAW)"] = clocks(bits(act, 8, 0));
        result["CAS Write Latency (tCWL)"] = clocks(bits(odt, 31, 24));
        result["Refresh Interval (tREFI)"] = clocks(bits(rftp, 17, 0));
        result["Refresh Cycle Time (tRFC)"] = clocks(bits(rftp, 30, 18));
    }
    return result;
}

