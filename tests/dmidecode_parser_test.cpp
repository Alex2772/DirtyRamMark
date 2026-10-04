#include <gtest/gtest.h>
#include <AUI/IO/AFileInputStream.h>
#include <cstring>
#include <memory_info.h>

TEST(DmidecodeParser, ParseSampleOutput) {
    AString dmidecodeOutput = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(APath(__FILE__).parent() / "data" / "dmidecode_output.txt")));

    auto entries = parseDmidecodeOutput(dmidecodeOutput);
    EXPECT_GT(entries.size(), 0);

    ASSERT_TRUE(entries.contains("Memory Array - Maximum Capacity"));
    EXPECT_EQ(entries["Memory Array - Maximum Capacity"].asString(), "128 GB");

    // 4 populated DDR4 slots (2 channels x 2 DIMMs)
    for (const char* slotName : { "Slot 0", "Slot 1", "Slot 2", "Slot 3" }) {
        ASSERT_TRUE(entries.contains(slotName)) << slotName;
        const auto& slot = entries[slotName];
        EXPECT_EQ(slot["Manufacturer"].asString(), "Kingston");
        EXPECT_EQ(slot["Size"].asString(), "32 GB");
        EXPECT_EQ(slot["Type"].asString(), "DDR4");
        EXPECT_EQ(slot["Speed"].asString(), "3600 MT/s");
        EXPECT_EQ(slot["Part Number"].asString(), "KF3600C18D4/32GX");
    }
    EXPECT_FALSE(entries.contains("Slot 4"));
}

TEST(DmidecodeParser, EmptyOutput) {
    AString emptyOutput = "";
    auto entries = parseDmidecodeOutput(emptyOutput);
    EXPECT_EQ(entries.size(), 0);
}

TEST(DmidecodeParser, InvalidOutput) {
    AString invalidOutput = "This is not dmidecode output\nJust random text\nNo colons here";
    auto entries = parseDmidecodeOutput(invalidOutput);
    // Should handle gracefully without crashing
    EXPECT_EQ(entries.size(), 0);
}

TEST(DmidecodeParser, PartialOutput) {
    // BIOS/System/Processor sections are intentionally not reported anymore; the memory device must still be.
    AString partialOutput = R"(Handle 0x0000, DMI type 0, 26 bytes
BIOS Information
        Vendor: Test Vendor
        Version: 1.0

Handle 0x0019, DMI type 17, 40 bytes
Memory Device
        Size: 16 GB
        Type: DDR4
        Manufacturer: Test Manufacturer)";

    auto entries = parseDmidecodeOutput(partialOutput);
    ASSERT_EQ(entries.size(), 1);
    ASSERT_TRUE(entries.contains("Slot 0"));
    EXPECT_EQ(entries["Slot 0"]["Manufacturer"].asString(), "Test Manufacturer");
    EXPECT_EQ(entries["Slot 0"]["Size"].asString(), "16 GB");
}

TEST(DmidecodeParser, SimpleTest) {
    AString simpleOutput = R"(Handle 0x0000, DMI type 0, 26 bytes
BIOS Information
        Vendor: Test BIOS Vendor
        Version: 1.0

Handle 0x0001, DMI type 1, 27 bytes
System Information
        Manufacturer: Test System Manufacturer
        Product Name: Test Product

Handle 0x0019, DMI type 17, 40 bytes
Memory Device
        Size: 16 GB
        Type: DDR4
        Manufacturer: Test Memory Manufacturer)";

    auto entries = parseDmidecodeOutput(simpleOutput);
    ASSERT_TRUE(entries.contains("Slot 0"));
    EXPECT_EQ(entries["Slot 0"]["Manufacturer"].asString(), "Test Memory Manufacturer");
    // the memory device's Manufacturer must not be overwritten by the System one
    EXPECT_FALSE(entries.contains("System - Manufacturer"));
}

namespace {
/// Appends an SMBIOS structure: formatted area (header included) followed by its string set.
void addStructure(AByteBuffer& table, uint8_t type, std::vector<uint8_t> formatted, const std::vector<std::string>& strings) {
    formatted[0] = type;
    formatted[1] = uint8_t(formatted.size());
    table.write(reinterpret_cast<const char*>(formatted.data()), formatted.size());
    for (const auto& s : strings) {
        table.write(s.c_str(), s.size() + 1);
    }
    table.write("\0", 1);
    if (strings.empty()) {
        table.write("\0", 1);
    }
}
}   // namespace

TEST(SmbiosParser, MemoryArrayAndDevices) {
    AByteBuffer table;
    // type 16: Maximum Capacity 128 GB (in KB) at 0x07, Number Of Devices 4 at 0x0d
    std::vector<uint8_t> array(0x17, 0);
    uint32_t kb = 128u * 1024 * 1024;
    std::memcpy(&array[0x07], &kb, 4);
    array[0x0d] = 4;
    addStructure(table, 16, array, {});

    // type 17: 32 GB DDR4-3600 module
    std::vector<uint8_t> dev(0x22, 0);
    uint16_t size = 0x7fff, speed = 3600;   // 0x7fff: the real size is in Extended Size (MB)
    uint32_t extendedSize = 32 * 1024;
    std::memcpy(&dev[0x0c], &size, 2);
    std::memcpy(&dev[0x1c], &extendedSize, 4);
    dev[0x10] = 1;   // locator
    dev[0x12] = 0x1a;
    std::memcpy(&dev[0x15], &speed, 2);
    dev[0x17] = 2;   // manufacturer
    dev[0x1a] = 3;   // part number
    dev[0x1b] = 2;   // rank
    std::memcpy(&dev[0x20], &speed, 2);
    addStructure(table, 17, dev, {"DIMM_A1", "Corsair", "CMK64GX4M2D3600C18"});

    // empty socket must be skipped
    std::vector<uint8_t> empty(0x22, 0);
    empty[0x12] = 0x1a;
    addStructure(table, 17, empty, {});
    addStructure(table, 127, std::vector<uint8_t>(4, 0), {});

    auto entries = parseSmbiosTable(table);
    EXPECT_EQ(entries["Memory Array - Maximum Capacity"].asString(), "128 GB");
    EXPECT_EQ(entries["Memory Array - Number Of Devices"].asString(), "4");
    ASSERT_TRUE(entries.contains("Slot 0"));
    EXPECT_FALSE(entries.contains("Slot 1"));
    auto slot = entries["Slot 0"];
    EXPECT_EQ(slot["Size"].asString(), "32 GB");
    EXPECT_EQ(slot["Type"].asString(), "DDR4");
    EXPECT_EQ(slot["Speed"].asString(), "3600 MT/s");
    EXPECT_EQ(slot["Configured Memory Speed"].asString(), "3600 MT/s");
    EXPECT_EQ(slot["Locator"].asString(), "DIMM_A1");
    EXPECT_EQ(slot["Manufacturer"].asString(), "Corsair");
    EXPECT_EQ(slot["Part Number"].asString(), "CMK64GX4M2D3600C18");
    EXPECT_EQ(slot["Rank"].asString(), "2");
}

TEST(SmbiosParser, GarbageIsHandled) {
    AByteBuffer table;
    table.write("\x11\xff\x00", 3);
    EXPECT_EQ(parseSmbiosTable(table).size(), 0);
    EXPECT_EQ(parseSmbiosTable(AByteBuffer {}).size(), 0);
}
