#include <gtest/gtest.h>
#include <AUI/IO/AFileInputStream.h>
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
