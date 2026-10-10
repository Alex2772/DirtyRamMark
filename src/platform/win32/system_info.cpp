#include <windows.h>
#include <string>
#include <vector>
#include <dxgi.h>
#include <wrl/client.h>

#include <AUI/Common/AException.h>
#include <system_info.h>

#pragma comment(lib, "dxgi.lib")

namespace {

std::optional<AString> registryString(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        return std::nullopt;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return AString(std::u16string_view(reinterpret_cast<const char16_t*>(value.data()), value.size()));
}

std::optional<DWORD> registryDword(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

AString registryOrEmpty(const wchar_t* path, const wchar_t* name) {
    return registryString(HKEY_LOCAL_MACHINE, path, name).value_or(AString {});
}

void addOs(AJson::Object& result) {
    constexpr auto CURRENT_VERSION = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    auto product = registryOrEmpty(CURRENT_VERSION, L"ProductName");
    const auto build = registryOrEmpty(CURRENT_VERSION, L"CurrentBuildNumber");
    // Windows 11 still says "Windows 10" in ProductName; builds starting from 22000 are Windows 11
    if (product.startsWith("Windows 10") && build.toInt().valueOr(0) >= 22000) {
        product = product.replacedAll("Windows 10", "Windows 11");
    }
    const auto displayVersion = registryOrEmpty(CURRENT_VERSION, L"DisplayVersion");
    const auto ubr = registryDword(HKEY_LOCAL_MACHINE, CURRENT_VERSION, L"UBR");
    SYSTEM_INFO si {};
    GetNativeSystemInfo(&si);
    const char* arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64   ? "x86_64"
                       : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
                       : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86"
                                                                                   : "";
    result["OS"] = system_info::joinMeaningful({ product, displayVersion, arch });
    if (!build.empty()) {
        result["Kernel"] = "NT {}{}"_format(build, ubr ? ".{}"_format(*ubr) : AString {});
    }
}

void addFirmware(AJson::Object& result) {
    constexpr auto BIOS = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    const auto host = system_info::joinMeaningful(
        { registryOrEmpty(BIOS, L"SystemManufacturer"), registryOrEmpty(BIOS, L"SystemProductName"),
          registryOrEmpty(BIOS, L"SystemVersion") });
    if (!host.empty()) {
        result["Host"] = host;
    }
    const auto board = system_info::joinMeaningful(
        { registryOrEmpty(BIOS, L"BaseBoardManufacturer"), registryOrEmpty(BIOS, L"BaseBoardProduct") });
    if (!board.empty()) {
        result["Board"] = board;
    }
    const auto bios = system_info::joinMeaningful({ registryOrEmpty(BIOS, L"BIOSVendor"),
                                                    registryOrEmpty(BIOS, L"BIOSVersion"),
                                                    registryOrEmpty(BIOS, L"BIOSReleaseDate") });
    if (!bios.empty()) {
        result["BIOS"] = bios;
    }
}

void addCpu(AJson::Object& result) {
    constexpr auto CPU0 = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    AString text = registryOrEmpty(CPU0, L"ProcessorNameString").trim();
    if (text.empty()) {
        text = "Unknown";
    }

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length > 0) {
        std::vector<char> buffer(length);
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length)) {
            unsigned cores = 0;
            unsigned threads = 0;
            for (DWORD offset = 0; offset < length;) {
                auto info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
                ++cores;
                threads += info->Processor.Flags & LTP_PC_SMT ? 2 : 1;
                offset += info->Size;
            }
            text += cores != threads ? " ({} cores, {} threads)"_format(cores, threads) : " ({} cores)"_format(cores);
        }
    }
    if (auto mhz = registryDword(HKEY_LOCAL_MACHINE, CPU0, L"~MHz")) {
        text += " @ {:.2f} GHz"_format(double(*mhz) / 1000.0);
    }
    result["CPU"] = text;
}

void addGpus(AJson::Object& result) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return;
    }
    AVector<AString> gpus;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i, adapter.Reset()) {
        DXGI_ADAPTER_DESC1 desc {};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            continue;   // "Microsoft Basic Render Driver"
        }
        auto text = AString(reinterpret_cast<const char16_t*>(desc.Description));
        if (desc.DedicatedVideoMemory > 0) {
            text += " ({})"_format(system_info::formatBytes(desc.DedicatedVideoMemory));
        }
        gpus << text;
    }
    for (size_t i = 0; i < gpus.size(); ++i) {
        result[gpus.size() == 1 ? AString("GPU") : "GPU {}"_format(i + 1)] = gpus[i];
    }
}

void addMemory(AJson::Object& result) {
    MEMORYSTATUSEX ms { .dwLength = sizeof(ms) };
    if (!GlobalMemoryStatusEx(&ms)) {
        return;
    }
    result["Memory"] = system_info::formatUsage(ms.ullTotalPhys - ms.ullAvailPhys, ms.ullTotalPhys);
    // the page file is included into ullTotalPageFile (commit limit)
    if (ms.ullTotalPageFile > ms.ullTotalPhys) {
        const auto total = ms.ullTotalPageFile - ms.ullTotalPhys;
        const auto free = ms.ullAvailPageFile > ms.ullAvailPhys ? ms.ullAvailPageFile - ms.ullAvailPhys : 0;
        result["Swap"] = system_info::formatUsage(total - std::min(free, total), total);
    }
}

void addDisk(AJson::Object& result) {
    wchar_t systemDir[MAX_PATH] {};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) < 3) {
        return;
    }
    const wchar_t root[] = { systemDir[0], L':', L'\\', 0 };   // "C:\"
    ULARGE_INTEGER freeToCaller {}, total {}, totalFree {};
    if (GetDiskFreeSpaceExW(root, &freeToCaller, &total, &totalFree)) {
        result["Disk ({}:)"_format(char(systemDir[0]))] =
            system_info::formatUsage(total.QuadPart - totalFree.QuadPart, total.QuadPart);
    }
}

}   // namespace

AJson::Object systemInfo() {
    AJson::Object result;
    addOs(result);
    addFirmware(result);
    result["Uptime"] = system_info::formatUptime(GetTickCount64() / 1000);
    addCpu(result);
    addGpus(result);
    addMemory(result);
    addDisk(result);
    return result;
}
