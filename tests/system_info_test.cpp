#include <gtest/gtest.h>
#include <AUI/Common/AByteBuffer.h>
#include <AUI/IO/AFileInputStream.h>
#include <system_info.h>

using namespace system_info;

static AString readData(const char* name) {
    return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(APath(__FILE__).parent() / "data" / name)));
}

TEST(SystemInfo, OsRelease) {
    auto r = parseOsRelease(readData("os_release.txt"));
    EXPECT_EQ(r["PRETTY_NAME"], "Fedora Linux 44 (Workstation Edition)");
    EXPECT_EQ(r["ID"], "fedora");
    EXPECT_EQ(r["SINGLE"], "quoted value");
    EXPECT_EQ(r["EMPTY"], "");
    EXPECT_FALSE(r.contains("not a key value line"));
}

TEST(SystemInfo, CpuInfoX86) {
    auto cpu = parseCpuInfo(readData("cpuinfo_x86.txt"));
    EXPECT_EQ(cpu.model, "AMD Ryzen 7 5800X 8-Core Processor");
    EXPECT_EQ(cpu.threads, 4u);
    EXPECT_EQ(cpu.cores, 2u);
    EXPECT_NEAR(cpu.mhz, 3792.345, 0.001);
}

TEST(SystemInfo, CpuInfoArm) {
    auto cpu = parseCpuInfo(readData("cpuinfo_arm.txt"));
    EXPECT_EQ(cpu.model, "BCM2711");
    EXPECT_EQ(cpu.threads, 4u);
    EXPECT_EQ(cpu.cores, 4u);   // no core ids: every thread is a core
}

TEST(SystemInfo, CpuInfoEmpty) {
    auto cpu = parseCpuInfo("");
    EXPECT_TRUE(cpu.model.empty());
    EXPECT_EQ(cpu.threads, 0u);
    EXPECT_EQ(cpu.cores, 0u);
}

TEST(SystemInfo, Meminfo) {
    auto m = parseMeminfo(readData("meminfo.txt"));
    EXPECT_EQ(m.total, 16384000ull * 1024);
    EXPECT_EQ(m.available, 8192000ull * 1024);
    EXPECT_EQ(m.swapTotal, 8388608ull * 1024);
    EXPECT_EQ(m.swapFree, 6291456ull * 1024);
}

TEST(SystemInfo, MeminfoWithoutAvailable) {
    auto m = parseMeminfo(readData("meminfo_old.txt"));
    EXPECT_EQ(m.total, 1048576ull * 1024);
    EXPECT_EQ(m.available, (100000ull + 20000 + 30000) * 1024);
    EXPECT_EQ(m.swapTotal, 0u);
}

TEST(SystemInfo, PciLookup) {
    const auto ids = readData("pci.ids");
    auto nvidia = lookupPci(ids, 0x10de, 0x2484);
    ASSERT_TRUE(nvidia);
    EXPECT_EQ(nvidia->vendor, "NVIDIA Corporation");
    EXPECT_EQ(nvidia->device, "GA104 [GeForce RTX 3070]");
    EXPECT_EQ(marketingDeviceName(nvidia->device), "GeForce RTX 3070");
    EXPECT_EQ(shortVendorName(nvidia->vendor), "NVIDIA");

    auto amd = lookupPci(ids, 0x1002, 0x73bf);   // not confused with the sub-device lines of the previous device
    ASSERT_TRUE(amd);
    EXPECT_EQ(shortVendorName(amd->vendor), "AMD");
    EXPECT_EQ(marketingDeviceName(amd->device), "Radeon RX 6800/6800 XT / 6900 XT");

    auto intel = lookupPci(ids, 0x8086, 0x9a49);   // the last vendor in the file
    ASSERT_TRUE(intel);
    EXPECT_EQ(marketingDeviceName(intel->device), "Iris Xe Graphics");

    auto unknownDevice = lookupPci(ids, 0x10de, 0xffff);
    ASSERT_TRUE(unknownDevice);
    EXPECT_TRUE(unknownDevice->device.empty());

    EXPECT_FALSE(lookupPci(ids, 0xdead, 0x0001));
    EXPECT_FALSE(lookupPci("", 0x10de, 0x2484));
}

TEST(SystemInfo, MarketingNameWithoutBrackets) {
    EXPECT_EQ(marketingDeviceName("Some Device"), "Some Device");
    EXPECT_EQ(marketingDeviceName("Broken ]["), "Broken ][");
}

TEST(SystemInfo, FormatBytes) {
    EXPECT_EQ(formatBytes(0), "0 B");
    EXPECT_EQ(formatBytes(1023), "1023 B");
    EXPECT_EQ(formatBytes(1024), "1.00 KiB");
    EXPECT_EQ(formatBytes(16ull * 1024 * 1024 * 1024), "16.00 GiB");
    EXPECT_EQ(formatBytes(1536ull * 1024 * 1024), "1.50 GiB");
}

TEST(SystemInfo, FormatUptime) {
    EXPECT_EQ(formatUptime(5), "5 secs");
    EXPECT_EQ(formatUptime(60), "1 min");
    EXPECT_EQ(formatUptime(3600 + 120), "1 hour, 2 mins");
    EXPECT_EQ(formatUptime(2 * 86400 + 3 * 3600 + 5 * 60 + 9), "2 days, 3 hours, 5 mins");
}

TEST(SystemInfo, FormatUsage) {
    EXPECT_EQ(formatUsage(1024ull * 1024 * 1024, 4ull * 1024 * 1024 * 1024), "1.00 GiB / 4.00 GiB (25%)");
    EXPECT_EQ(formatUsage(0, 0), "0 B / 0 B (0%)");
}

TEST(SystemInfo, JoinMeaningful) {
    EXPECT_EQ(joinMeaningful({ "ASUS", "To be filled by O.E.M.", "ROG STRIX", "" }), "ASUS ROG STRIX");
    EXPECT_EQ(joinMeaningful({ "System manufacturer", "System Product Name" }), "");
}

TEST(SystemInfo, LiveReportIsSane) {
    auto info = systemInfo();
    EXPECT_TRUE(info.contains("OS"));
    EXPECT_TRUE(info.contains("CPU"));
    EXPECT_TRUE(info.contains("Memory"));
}

TEST(SystemInfo, Mounts) {
    auto mounts = parseMounts(readData("proc_mounts.txt"));
    ASSERT_EQ(mounts.size(), 2u);   // composefs, tmpfs, /boot*, and the second subvolume are skipped
    EXPECT_EQ(mounts[0].mountPoint, "/var");
    EXPECT_EQ(mounts[0].fileSystem, "btrfs");
    EXPECT_EQ(mounts[1].mountPoint, "/run/media/user/My Disk");
}

TEST(SystemInfo, Trim) {
    EXPECT_EQ(trim("\t a b \r\n"), "a b");
    EXPECT_EQ(trim("   "), "");
}
