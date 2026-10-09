#pragma once

#include <AUI/Common/AByteBuffer.h>
#include <AUI/Common/AMap.h>
#include "generic_key_value_cloud.h"

AJson::Object memoryInfo();
AJson::Object parseDmidecodeOutput(const AString& dmidecodeOutput);

/**
 * @brief Parses the raw SMBIOS structure table (what dmidecode reads, without the RawSMBIOSData header).
 * @return same layout as parseDmidecodeOutput().
 */
AJson::Object parseSmbiosTable(const AByteBuffer& table);

namespace memory_info {

/// Placeholders that firmware puts instead of real values.
bool isPlaceholder(const AString& value);

/// Only DDR modules are reported as slots.
bool isDdr(const AString& type);

/// Marketing name guessed from the manufacturer and part number (SMBIOS has no such field); empty if unknown.
AString productName(const AString& manufacturer, const AString& partNumber);

/// Builds the report: "Memory Array - <key>" entries and "Slot N" objects.
AJson::Object assemble(const AMap<AString, AString>& memoryArray, const AVector<AMap<AString, AString>>& devices);

}   // namespace memory_info
