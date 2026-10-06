#include <windows.h>

#include <AUI/Common/AException.h>
#include <memory_info.h>

AJson::Object memoryInfo() {
    // 'RSMB' is the raw SMBIOS table - the very data dmidecode reads on Linux. Doesn't need admin rights.
    constexpr DWORD RSMB = 'RSMB';
    const UINT size = GetSystemFirmwareTable(RSMB, 0, nullptr, 0);
    if (size == 0) {
        throw AException("GetSystemFirmwareTable(RSMB) failed: error {}"_format(GetLastError()));
    }
    AByteBuffer raw(size);
    raw.resize(GetSystemFirmwareTable(RSMB, 0, raw.data(), size));

    // struct RawSMBIOSData { BYTE Used20CallingMethod, MajorVersion, MinorVersion, DmiRevision; DWORD Length; BYTE Table[]; }
    constexpr size_t HEADER_SIZE = 8;
    if (raw.size() < HEADER_SIZE) {
        throw AException("SMBIOS table is truncated");
    }
    AByteBuffer table;
    table.write(raw.data() + HEADER_SIZE, raw.size() - HEADER_SIZE);
    return parseSmbiosTable(table);
}
