#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include <AUI/Common/AByteBuffer.h>
#include <AUI/Common/AException.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Url/AUrl.h>
#include <privileges.h>

#include "driver.h"
#include "driver_ioctl.h"

namespace {

constexpr auto SERVICE_NAME = L"DirtyRamMark";
constexpr auto DRIVER_ASSET = ":driver/dirtyrammark.sys";

AString winError(DWORD code) {
    return "error {}"_format(code);
}

struct ScHandle {
    SC_HANDLE handle = nullptr;
    ScHandle() = default;
    explicit ScHandle(SC_HANDLE h) : handle(h) {}
    ScHandle(const ScHandle&) = delete;
    ~ScHandle() {
        if (handle) {
            CloseServiceHandle(handle);
        }
    }
    operator SC_HANDLE() const { return handle; }
};

std::filesystem::path driverFile() {
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    // only administrators can write there, so the file can't be swapped before it's loaded
    return std::filesystem::path(system) / L"drivers" / L"dirtyrammark.sys";
}

/// Stops and deletes the service if it exists (the app's own or a leftover of a crashed run).
void removeService(SC_HANDLE manager) {
    ScHandle service(OpenServiceW(manager, SERVICE_NAME, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS));
    if (!service) {
        return;
    }
    SERVICE_STATUS status {};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    for (int i = 0; i < 50; ++i) {   // up to 5 seconds
        if (!QueryServiceStatus(service, &status) || status.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(100);
    }
    DeleteService(service);
}

/// Opens a console (elevated, since the app is) that turns the test signing mode on. The console stays open to let the
/// user see the outcome, i.e. that Secure Boot has to be turned off first.
void enableTestSigning() {
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", L"cmd.exe",
                      L"/k \"bcdedit /set testsigning on & echo. & echo Reboot your computer to apply the change.\"",
                      nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        throw AException("Unable to open the command prompt ({})"_format(winError(DWORD(result))));
    }
}

}   // namespace

driver::Device::Device() {
    try {
        ScHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE));
        if (!manager) {
            throw AException("Unable to open the service manager ({}). Run DirtyRamMark as administrator."_format(
                winError(GetLastError())));
        }
        removeService(manager);

        const auto file = driverFile();
        {
            const auto blob = AByteBuffer::fromStream(AUrl(DRIVER_ASSET).open());
            std::error_code ec;
            std::filesystem::remove(file, ec);
            std::ofstream out(file, std::ios::binary);
            out.write(blob.data(), std::streamsize(blob.size()));
            if (!out) {
                throw AException("Unable to write the driver to {}"_format(AString::fromUtf8(file.string())));
            }
        }

        ScHandle service(CreateServiceW(manager, SERVICE_NAME, L"DirtyRamMark memory controller access",
                                        SERVICE_START | DELETE | SERVICE_STOP, SERVICE_KERNEL_DRIVER,
                                        SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, file.c_str(), nullptr, nullptr,
                                        nullptr, nullptr, nullptr));
        if (!service) {
            throw AException("Unable to register the driver ({})"_format(winError(GetLastError())));
        }
        if (!StartServiceW(service, 0, nullptr)) {
            const auto error = GetLastError();
            if (error == ERROR_INVALID_IMAGE_HASH || error == ERROR_DRIVER_BLOCKED) {
                throw privileges::SettingRequired(
                    "Windows refused to load the driver because it isn't signed by Microsoft. Test signing mode has to "
                    "be turned on (needs Secure Boot off), then reboot and try again.",
                    "Run `bcdedit /set testsigning on` in the command prompt", enableTestSigning);
            }
            throw AException("Unable to start the driver ({})"_format(winError(error)));
        }

        mDevice = CreateFileW(DIRTYRAMMARK_DEVICE_PATH, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (mDevice == INVALID_HANDLE_VALUE) {
            mDevice = nullptr;
            throw AException("Unable to open the driver ({})"_format(winError(GetLastError())));
        }
        ALogger::info("Driver") << "Deployed";
    } catch (...) {
        cleanup();
        throw;
    }
}

driver::Device::~Device() { cleanup(); }

void driver::Device::cleanup() noexcept {
    if (mDevice) {
        CloseHandle(mDevice);
        mDevice = nullptr;
    }
    ScHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (manager) {
        removeService(manager);
    }
    std::error_code ec;
    std::filesystem::remove(driverFile(), ec);
}

uint32_t driver::Device::pciRead(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset) const {
    DirtyRamMarkPciRead in { bus, device, function, offset };
    ULONG out = 0;
    DWORD returned = 0;
    if (!DeviceIoControl(mDevice, IOCTL_DIRTYRAMMARK_PCI_READ, &in, sizeof(in), &out, sizeof(out), &returned,
                         nullptr)) {
        throw AException("PCI read failed ({})"_format(winError(GetLastError())));
    }
    return out;
}

uint32_t driver::Device::smnRead(uint32_t address) const {
    ULONG in = address;
    ULONG out = 0;
    DWORD returned = 0;
    if (!DeviceIoControl(mDevice, IOCTL_DIRTYRAMMARK_SMN_READ, &in, sizeof(in), &out, sizeof(out), &returned,
                         nullptr)) {
        throw AException("SMN read failed ({})"_format(winError(GetLastError())));
    }
    return out;
}

uint32_t driver::Device::mmioRead(uint64_t physicalAddress) const {
    DirtyRamMarkMmioRead in { physicalAddress, 1 };
    ULONG out = 0;
    DWORD returned = 0;
    if (!DeviceIoControl(mDevice, IOCTL_DIRTYRAMMARK_MMIO_READ, &in, sizeof(in), &out, sizeof(out), &returned,
                         nullptr)) {
        throw AException("MMIO read failed ({})"_format(winError(GetLastError())));
    }
    return out;
}

const driver::Device& driver::instance() {
    if (!privileges::isGranted()) {
        throw privileges::Required("Administrator rights are required to deploy the driver");
    }
    // destroyed on exit of the process, which removes the driver
    static const std::unique_ptr<Device> device = std::make_unique<Device>();
    return *device;
}
