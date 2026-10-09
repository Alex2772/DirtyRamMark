#include <range/v3/all.hpp>

#include <memory_info.h>

bool memory_info::isPlaceholder(const AString& value) {
    return value.empty() || value == "Unknown" || value == "Not Specified" || value == "To be filled by O.E.M." ||
           value == "To be filled by O.E.M" || value == "None";
}

bool memory_info::isDdr(const AString& type) {
    return type == "DDR" || type == "DDR2" || type == "DDR3" || type == "DDR4" || type == "DDR5";
}

AJson::Object memory_info::assemble(const AMap<AString, AString>& memoryArray,
                                    const AVector<AMap<AString, AString>>& devices) {
    AJson::Object result;
    for (const auto& [key, value] : memoryArray) {
        result["Memory Array - " + key] = value;
    }
    for (const auto& [i, device] : devices | ranges::views::enumerate) {
        auto& slot = result["Slot {}"_format(i)];
        for (const auto& [key, value] : device) {
            slot[key] = value;
        }
        if (auto name = productName(device.contains("Manufacturer") ? device.at("Manufacturer") : AString {},
                                    device.contains("Part Number") ? device.at("Part Number") : AString {});
            !name.empty()) {
            slot["Name"] = name;
        }
    }
    return result;
}

AJson::Object parseDmidecodeOutput(const AString& dmidecodeOutput) {
    AJson::Object result;
    auto lines = dmidecodeOutput.replacedAll("\r", "").split('\n');
    
    // Parse different sections
    int currentDmiType = -1;
    AMap<AString, AString> currentDevice;
    AVector<AMap<AString, AString>> memoryDevices;
    AMap<AString, AString> systemInfo;
    AMap<AString, AString> biosInfo;
    AMap<AString, AString> processorInfo;
    AMap<AString, AString> memoryArrayInfo;
    
    for (const auto& line : lines) {
        // Skip empty lines
        AString trimmedLine = line.trim().trim('\t');
        if (trimmedLine.empty()) {
            continue;
        }
        
        // Check for section headers - Handle 0x0000, DMI type 0, 26 bytes
        // Also handle lines like "Handle 0x0000, DMI type 0, 26 bytes"
        if (trimmedLine.startsWith("Handle 0x") && trimmedLine.contains("DMI type")) {
            // Save previous device if it's a memory device
            if (!currentDevice.empty() && currentDevice.contains("Type") && 
                memory_info::isDdr(currentDevice["Type"])) {
                memoryDevices << currentDevice;
            }
            currentDevice.clear();
            
            // Extract DMI type number - handle various formats
            size_t typePos = trimmedLine.find("DMI type ");
            if (typePos != AString::npos) {
                // Skip past "DMI type "
                size_t typeStart = typePos + 9;
                
                // Find where the type number ends (comma, space, or end of line)
                size_t typeEnd = typeStart;
                while (typeEnd < trimmedLine.length() && 
                       trimmedLine[typeEnd] != ',' && 
                       trimmedLine[typeEnd] != ' ') {
                    typeEnd++;
                }
                
                if (typeEnd > typeStart) {
                    AString typeStr = trimmedLine.substr(typeStart, typeEnd - typeStart);
                    auto typeOpt = typeStr.toInt();
                    if (typeOpt) {
                        currentDmiType = *typeOpt;
                    } else {
                        currentDmiType = -1;
                    }
                } else {
                    currentDmiType = -1;
                }
            } else {
                currentDmiType = -1;
            }
        }
        // Parse key-value pairs (lines containing colon that are not section headers)
        else if (trimmedLine.contains(':') && !trimmedLine.startsWith("Handle 0x")) {
            size_t colonPos = trimmedLine.find(':');
            AString key = trimmedLine.substr(0, colonPos).trim().trim('\t');
            AString value = trimmedLine.substr(colonPos + 1).trim().trim('\t');
            
            // Debug logging
            // std::cout << "DEBUG: Parsing DMI type " << currentDmiType << " - " << key << ": " << value << std::endl;
            
            // Skip empty values and placeholder values
            if (memory_info::isPlaceholder(value)) {
                continue;
            }
            
            // Store information based on DMI type
            switch (currentDmiType) {
                case 0: // BIOS Information
                    if (key == "Vendor" || key == "Version" || key == "Release Date" || 
                        key == "BIOS Revision") {
                        biosInfo[key] = value;
                    }
                    break;
                    
                case 1: // System Information
                    if (key == "Manufacturer" || key == "Product Name" || key == "Version" || 
                        key == "Serial Number" || key == "UUID" || key == "SKU Number") {
                        systemInfo[key] = value;
                    }
                    break;
                    
                case 4: // Processor Information
                    if (key == "Manufacturer" || key == "Version" || key == "Max Speed" || 
                        key == "Current Speed" || key == "Core Count" || key == "Thread Count") {
                        processorInfo[key] = value;
                    }
                    break;
                    
                case 16: // Physical Memory Array
                    if (key == "Maximum Capacity" || key == "Number Of Devices") {
                        memoryArrayInfo[key] = value;
                    }
                    break;
                    
                case 17: // Memory Device
                    if (key == "Manufacturer" || key == "Part Number" || key == "Type" || 
                        key == "Speed" || key == "Size" || key == "Locator" || 
                        key == "Configured Memory Speed" || key == "Rank" || 
                        key == "Serial Number" || key == "Bank Locator") {
                        currentDevice[key] = value;
                    }
                    break;
                    
                case 19: // Memory Array Mapped Address
                    if (key == "Range Size") {
                        result["Memory Mapped Range Size"] = value;
                    }
                    break;
            }
        }
        // Parse characteristics (multi-line values with indentation)
        else if (trimmedLine != line && line.startsWith("\t\t")) { // 8 spaces or equivalent tabs
            // This is a characteristic line (indented more than regular key-value pairs)
            AString characteristic = trimmedLine;
            if (!characteristic.empty()) {
                if (currentDmiType == 0) { // BIOS characteristics
                    if (!biosInfo.contains("Characteristics")) {
                        biosInfo["Characteristics"] = characteristic;
                    } else {
                        biosInfo["Characteristics"] += ", " + characteristic;
                    }
                }
            }
        }
    }
    
    // Save the last memory device if it exists
    if (!currentDevice.empty() && currentDevice.contains("Type") && 
        memory_info::isDdr(currentDevice["Type"])) {
        memoryDevices << currentDevice;
    }
    
    // Add BIOS information
    // for (const auto& [key, value] : biosInfo) {
    //     result << generic_key_value_cloud::Entry{"BIOS - " + key, value};
    // }
    
    // // Add System information
    // for (const auto& [key, value] : systemInfo) {
    //     result << generic_key_value_cloud::Entry{"System - " + key, value};
    // }
    
    // // Add Processor information
    // for (const auto& [key, value] : processorInfo) {
    //     result << generic_key_value_cloud::Entry{"Processor - " + key, value};
    // }
    
    for (auto& [key, value] : memory_info::assemble(memoryArrayInfo, memoryDevices)) {
        result[key] = std::move(value);
    }

    return result;
}
