#include <gtest/gtest.h>
#include <AUI/IO/AFileInputStream.h>
#include <memory_info.h>

TEST(DmidecodeParser, ParseSampleOutput) {
    AString dmidecodeOutput = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(APath(__FILE__).parent() / "data" / "dmidecode_output.txt")));

    auto entries = parseDmidecodeOutput(dmidecodeOutput);
    
    // Verify we have some entries
    EXPECT_GT(entries.size(), 0);
    
    // Check for specific expected entries
    bool foundBiosVendor = false;
    bool foundSystemManufacturer = false;
    bool foundProcessor = false;
    bool foundMemoryCapacity = false;
    bool foundMemoryDevice1 = false;
    bool foundMemoryDevice2 = false;
    
    for (const auto& entry : entries) {
        // Check BIOS information
        if (entry.key.contains("BIOS") && entry.key.contains("Vendor")) {
            if (entry.value == "American Megatrends International, LLC.") {
                foundBiosVendor = true;
            }
        }
        
        // Check System information
        if (entry.key.contains("System") && entry.key.contains("Manufacturer")) {
            if (entry.value == "Micro-Star International Co., Ltd.") {
                foundSystemManufacturer = true;
            }
        }
        
        // Check Processor information
        if (entry.key.contains("Processor") && entry.key.contains("Version")) {
            if (entry.value.contains("AMD Ryzen 9 5950X")) {
                foundProcessor = true;
            }
        }
        
        // Check Memory Array capacity
        if (entry.key.contains("Memory Array") && entry.key.contains("Maximum Capacity")) {
            if (entry.value == "128 GB") {
                foundMemoryCapacity = true;
            }
        }
        
        // Check Memory Device 1
        if (entry.key.contains("Memory Device 1") && entry.key.contains("Manufacturer")) {
            if (entry.value == "Kingston") {
                foundMemoryDevice1 = true;
            }
        }
        
        // Check Memory Device 2
        if (entry.key.contains("Memory Device 2") && entry.key.contains("Manufacturer")) {
            if (entry.value == "Kingston") {
                foundMemoryDevice2 = true;
            }
        }
    }
    
    EXPECT_TRUE(foundBiosVendor) << "BIOS Vendor not found";
    EXPECT_TRUE(foundSystemManufacturer) << "System Manufacturer not found";
    EXPECT_TRUE(foundProcessor) << "Processor information not found";
    EXPECT_TRUE(foundMemoryCapacity) << "Memory capacity not found";
    EXPECT_TRUE(foundMemoryDevice1) << "Memory Device 1 not found";
    EXPECT_TRUE(foundMemoryDevice2) << "Memory Device 2 not found";
    
    // Verify memory device details
    for (const auto& entry : entries) {
        if (entry.key.contains("Memory Device 1") && entry.key.contains("Size")) {
            EXPECT_EQ(entry.value, "32 GB");
        }
        if (entry.key.contains("Memory Device 1") && entry.key.contains("Type")) {
            EXPECT_EQ(entry.value, "DDR4");
        }
        if (entry.key.contains("Memory Device 1") && entry.key.contains("Speed")) {
            EXPECT_EQ(entry.value, "3600 MT/s");
        }
        if (entry.key.contains("Memory Device 1") && entry.key.contains("Part Number")) {
            EXPECT_EQ(entry.value, "KF3600C18D4/32GX");
        }
    }
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
    EXPECT_GT(entries.size(), 0);
    
    bool foundBiosVendor = false;
    bool foundMemoryDevice = false;
    
    for (const auto& entry : entries) {
        if (entry.key.contains("BIOS") && entry.key.contains("Vendor")) {
            if (entry.value == "Test Vendor") {
                foundBiosVendor = true;
            }
        }
        if (entry.key.contains("Memory Device") && entry.key.contains("Manufacturer")) {
            if (entry.value == "Test Manufacturer") {
                foundMemoryDevice = true;
            }
        }
    }
    
    EXPECT_TRUE(foundBiosVendor);
    EXPECT_TRUE(foundMemoryDevice);
}

TEST(DmidecodeParser, SimpleTest) {
    // Very simple dmidecode output
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
    
    EXPECT_GT(entries.size(), 0);
    
    // Check for expected entries
    bool foundBios = false;
    bool foundSystem = false;
    bool foundMemory = false;
    
    for (const auto& entry : entries) {
        if (entry.key.contains("BIOS") && entry.key.contains("Vendor")) {
            if (entry.value == "Test BIOS Vendor") {
                foundBios = true;
            }
        }
        if (entry.key.contains("System") && entry.key.contains("Manufacturer")) {
            if (entry.value == "Test System Manufacturer") {
                foundSystem = true;
            }
        }
        if (entry.key.contains("Memory Device") && entry.key.contains("Manufacturer")) {
            if (entry.value == "Test Memory Manufacturer") {
                foundMemory = true;
            }
        }
    }
    

    
    EXPECT_TRUE(foundBios);
    EXPECT_TRUE(foundSystem);
    EXPECT_TRUE(foundMemory);
}