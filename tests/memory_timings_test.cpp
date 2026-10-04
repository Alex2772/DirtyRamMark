#include <gtest/gtest.h>
#include <memory_timings.h>

using namespace memory_timings;

TEST(MemoryTimings, AmdDdr4) {
    // DDR4-3600 CL16-19-19-39
    AMap<uint32_t, uint32_t> r;
    r[0x50200] = 54 | (1 << 10);   // ratio 18 (x1/3), 2T
    r[0x50204] = 16 | (39 << 8) | (19 << 16) | (19 << 24);
    r[0x50208] = 58 | (19 << 16);
    r[0x5020C] = 4 | (6 << 8) | (12 << 24);
    r[0x50210] = 24;
    r[0x50214] = 4 | (8 << 8) | (14 << 16) | (0 << 0) | 14;
    r[0x50218] = 26;
    r[0x50230] = 14040;
    r[0x50260] = 630 | (467 << 11) | (288 << 22);
    r[0x50264] = 0x21060138;

    auto t = decodeAmdUmc(r, AmdDdrType::DDR4);
    EXPECT_EQ(t["Data rate"].asString(), "3600 MT/s");
    EXPECT_EQ(t["CAS Latency (CL)"].asString(), "16 clocks");
    EXPECT_EQ(t["RAS to CAS Delay, Read (tRCDRD)"].asString(), "19 clocks");
    EXPECT_EQ(t["RAS to CAS Delay, Write (tRCDWR)"].asString(), "19 clocks");
    EXPECT_EQ(t["Row Active Time (tRAS)"].asString(), "39 clocks");
    EXPECT_EQ(t["Row Cycle Time (tRC)"].asString(), "58 clocks");
    EXPECT_EQ(t["Row Precharge Time (tRP)"].asString(), "19 clocks");
    EXPECT_EQ(t["Row to Row Delay, Short (tRRDS)"].asString(), "4 clocks");
    EXPECT_EQ(t["Row to Row Delay, Long (tRRDL)"].asString(), "6 clocks");
    EXPECT_EQ(t["Four Activate Window (tFAW)"].asString(), "24 clocks");
    EXPECT_EQ(t["Write Recovery (tWR)"].asString(), "26 clocks");
    EXPECT_EQ(t["Refresh Interval (tREFI)"].asString(), "14040 clocks");
    EXPECT_EQ(t["Refresh Cycle Time (tRFC)"].asString(), "630 clocks");
    EXPECT_EQ(t["Command rate"].asString(), "2T");
}

TEST(MemoryTimings, AmdDdr5) {
    AMap<uint32_t, uint32_t> r;
    r[0x50200] = 3000;   // MEMCLK 3000 MHz
    r[0x50204] = 30 | (60 << 8) | (38 << 16) | (38 << 24);
    auto t = decodeAmdUmc(r, AmdDdrType::DDR5);
    EXPECT_EQ(t["Data rate"].asString(), "6000 MT/s");
    EXPECT_EQ(t["CAS Latency (CL)"].asString(), "30 clocks");
    EXPECT_EQ(t["RAS to CAS Delay, Read (tRCDRD)"].asString(), "38 clocks");
}

TEST(MemoryTimings, AbsentChannel) {
    EXPECT_TRUE(decodeAmdUmc({}, AmdDdrType::DDR4).empty());
    EXPECT_TRUE(decodeAmdUmc({ { 0x50200, 0xffffffff } }, AmdDdrType::DDR4).empty());
}

TEST(CpuInfo, AmdMultiCore) {
    // tab separated keys, as the kernel prints them; two logical processors
    auto cpu = parseCpuInfo(
        "processor\t: 0\n"
        "vendor_id\t: AuthenticAMD\n"
        "cpu family\t: 25\n"
        "model\t\t: 33\n"
        "model name\t: AMD Ryzen 9 5950X 16-Core Processor\n"
        "\n"
        "processor\t: 1\n"
        "vendor_id\t: GenuineIntel\n"
        "cpu family\t: 6\n"
        "model\t\t: 151\n");
    EXPECT_EQ(cpu.vendor, "AuthenticAMD");
    EXPECT_EQ(cpu.family, 25u);
    EXPECT_EQ(cpu.model, 33u);   // the second block must be ignored
}

TEST(CpuInfo, Intel) {
    auto cpu = parseCpuInfo("vendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 151\n");
    EXPECT_EQ(cpu.vendor, "GenuineIntel");
    EXPECT_EQ(cpu.family, 6u);
    EXPECT_EQ(cpu.model, 151u);
}

TEST(CpuInfo, ModelZero) {
    auto cpu = parseCpuInfo("vendor_id\t: AuthenticAMD\ncpu family\t: 23\nmodel\t\t: 0\n");
    EXPECT_EQ(cpu.family, 23u);
    EXPECT_EQ(cpu.model, 0u);
}

TEST(CpuInfo, EmptyAndGarbage) {
    EXPECT_TRUE(parseCpuInfo("").vendor.empty());
    auto cpu = parseCpuInfo("no colons here\n:\n\n\t\n");
    EXPECT_TRUE(cpu.vendor.empty());
    EXPECT_EQ(cpu.family, 0u);
}

TEST(CpuInfo, DdrTypeByFamily) {
    EXPECT_EQ(amdDdrTypeOf(23, 113), AmdDdrType::DDR4);   // Zen 2 Matisse
    EXPECT_EQ(amdDdrTypeOf(25, 33), AmdDdrType::DDR4);    // Zen 3 Vermeer
    EXPECT_EQ(amdDdrTypeOf(25, 97), AmdDdrType::DDR5);    // Zen 4 Raphael
    EXPECT_EQ(amdDdrTypeOf(26, 68), AmdDdrType::DDR5);    // Zen 5
}

// Intel: the expected values below are the register reset values from the datasheets (vol. 2), where the default of
// every field is documented, so they check bit positions independently of the decoder.

TEST(MemoryTimingsIntel, SkylakeDatasheetDefaults) {
    AMap<uint32_t, uint32_t> r;
    r[0x0] = 0x18863808 + 1;   // TC_PRE reset value with tRP = 9 (an untouched reset value means "not trained")
    r[0x4] = 0x01088410;       // TC_ACT
    r[0x70] = 0x01850000;      // TC_ODT
    r[0x23c] = 0x00B41004;     // TC_RFTP
    auto t = decodeIntelChannel(r, IntelLayout::SKYLAKE);
    EXPECT_EQ(t["CAS Latency (CL)"].asString(), "5 clocks");
    EXPECT_EQ(t["CAS Write Latency (tCWL)"].asString(), "6 clocks");
    EXPECT_EQ(t["Row Precharge Time (tRP)"].asString(), "9 clocks");
    EXPECT_EQ(t["RAS to CAS Delay, Read (tRCDRD)"].asString(), "9 clocks");   // shares the tRP field
    EXPECT_EQ(t["RAS to CAS Delay, Write (tRCDWR)"].asString(), "8 clocks");
    EXPECT_EQ(t["Row Active Time (tRAS)"].asString(), "28 clocks");
    EXPECT_EQ(t["Read to Precharge (tRTP)"].asString(), "6 clocks");
    EXPECT_EQ(t["Write to Precharge (tWRPRE)"].asString(), "24 clocks");
    EXPECT_EQ(t["Four Activate Window (tFAW)"].asString(), "16 clocks");
    EXPECT_EQ(t["Row to Row Delay, Long (tRRDL)"].asString(), "4 clocks");
    EXPECT_EQ(t["Row to Row Delay, Short (tRRDS)"].asString(), "4 clocks");
    EXPECT_EQ(t["Refresh Cycle Time (tRFC)"].asString(), "180 clocks");
    EXPECT_EQ(t["Refresh Interval (tREFI)"].asString(), "4100 clocks");
}

TEST(MemoryTimingsIntel, AlderDatasheetDefaults) {
    AMap<uint32_t, uint32_t> r;
    const uint64_t pre = 0x104070180040C008ull + 1;   // TC_PRE reset value with tRP = 9
    r[0x0] = uint32_t(pre);
    r[0x4] = uint32_t(pre >> 32);
    r[0x8] = 0x18020810;    // TC_ACT
    r[0x70] = 0x06050000;   // TC_ODT
    r[0x43c] = 0x02D01004;  // TC_RFTP
    auto t = decodeIntelChannel(r, IntelLayout::ALDER);
    EXPECT_EQ(t["CAS Latency (CL)"].asString(), "5 clocks");
    EXPECT_EQ(t["CAS Write Latency (tCWL)"].asString(), "6 clocks");
    EXPECT_EQ(t["Row Precharge Time (tRP)"].asString(), "9 clocks");
    EXPECT_EQ(t["RAS to CAS Delay, Read (tRCDRD)"].asString(), "8 clocks");
    EXPECT_EQ(t["Row Active Time (tRAS)"].asString(), "28 clocks");
    EXPECT_EQ(t["Read to Precharge (tRTP)"].asString(), "6 clocks");
    EXPECT_EQ(t["Write to Precharge (tWRPRE)"].asString(), "24 clocks");
    EXPECT_EQ(t["Four Activate Window (tFAW)"].asString(), "16 clocks");
    EXPECT_EQ(t["Row to Row Delay, Long (tRRDL)"].asString(), "4 clocks");
    EXPECT_EQ(t["Row to Row Delay, Short (tRRDS)"].asString(), "4 clocks");
    EXPECT_EQ(t["Refresh Cycle Time (tRFC)"].asString(), "180 clocks");
    EXPECT_EQ(t["Refresh Interval (tREFI)"].asString(), "4100 clocks");
}

TEST(MemoryTimingsIntel, UntrainedChannelIsSkipped) {
    AMap<uint32_t, uint32_t> skl;
    skl[0x0] = 0x18863808;
    EXPECT_TRUE(decodeIntelChannel(skl, IntelLayout::SKYLAKE).empty());
    AMap<uint32_t, uint32_t> adl;
    adl[0x0] = 0x0040C008;
    adl[0x4] = 0x10407018;
    EXPECT_TRUE(decodeIntelChannel(adl, IntelLayout::ALDER).empty());
    EXPECT_TRUE(decodeIntelChannel({}, IntelLayout::ALDER).empty());
    EXPECT_TRUE(decodeIntelChannel({ { 0x0, 0xffffffff } }, IntelLayout::SKYLAKE).empty());
}

TEST(MemoryTimingsIntel, Clock) {
    // DDR4-3200: QCLK ratio 24 in 133.33 MHz steps
    auto c = decodeIntelClock(24, IntelLayout::SKYLAKE);
    EXPECT_EQ(c["Data rate"].asString(), "3200 MT/s");
    EXPECT_EQ(c["Gear"].asString(), "Gear 1");
    // DDR5-6000: ratio 60 in 100 MHz steps, Gear 2 / Gear 4 encodings differ between layouts
    EXPECT_EQ(decodeIntelClock(60 | (1 << 8) | (1 << 12), IntelLayout::ALDER)["Data rate"].asString(), "6000 MT/s");
    EXPECT_EQ(decodeIntelClock(60 | (1 << 8) | (1 << 12), IntelLayout::ALDER)["Gear"].asString(), "Gear 2");
    EXPECT_EQ(decodeIntelClock(60 | (1 << 8) | (2 << 12), IntelLayout::ALDER)["Gear"].asString(), "Gear 4");
    EXPECT_EQ(decodeIntelClock(24 | (1 << 16), IntelLayout::SKYLAKE)["Gear"].asString(), "Gear 2");
    EXPECT_TRUE(decodeIntelClock(0, IntelLayout::SKYLAKE).empty());   // MC PLL is shut down
}

TEST(MemoryTimingsIntel, MchbarBase) {
    // Skylake family: bits 38:16, Alder Lake: bits 41:17; bit 0 is MCHBAREN
    EXPECT_EQ(intelMchbarBase(IntelLayout::SKYLAKE, 0xfedc0001, 0), 0xfedc0000u);
    EXPECT_EQ(intelMchbarBase(IntelLayout::ALDER, 0xfedc0001, 0x2), 0x2fedc0000ull);
    EXPECT_EQ(intelMchbarBase(IntelLayout::ALDER, 0xfedc0000, 0), 0u);   // disabled
}

TEST(MemoryTimingsIntel, LayoutByModel) {
    EXPECT_EQ(intelLayoutOf(6, 0xa5), IntelLayout::SKYLAKE);   // Comet Lake
    EXPECT_EQ(intelLayoutOf(6, 0x9e), IntelLayout::SKYLAKE);   // Coffee Lake
    EXPECT_EQ(intelLayoutOf(6, 0x97), IntelLayout::ALDER);     // Alder Lake S
    EXPECT_EQ(intelLayoutOf(6, 0xb7), IntelLayout::ALDER);     // Raptor Lake S
    EXPECT_FALSE(intelLayoutOf(6, 0xa7));                      // Rocket Lake: not verified
    EXPECT_FALSE(intelLayoutOf(6, 0xaa));                      // Meteor Lake: different layout
    EXPECT_FALSE(intelLayoutOf(23, 0x97));
}

TEST(CpuInfo, ControllerVendor) {
    EXPECT_EQ(controllerVendor(parseCpuInfo("vendor_id\t: AuthenticAMD\n")), "AMD");
    EXPECT_EQ(controllerVendor(parseCpuInfo("vendor_id\t: GenuineIntel\n")), "Intel");
    EXPECT_EQ(controllerVendor(parseCpuInfo("vendor_id\t: HygonGenuine\n")), "HygonGenuine");
    auto d = describeController(parseCpuInfo("vendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 151\n"));
    EXPECT_EQ(d["vendor"].asString(), "Intel");
    EXPECT_EQ(d["family"].asInt(), 6);
    EXPECT_EQ(d["model"].asInt(), 151);
}
