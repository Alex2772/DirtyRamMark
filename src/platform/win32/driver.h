#pragma once

#include <cstdint>

/**
 * @brief The temporary kernel driver (driver/dirtyrammark.c) that reads the memory controller on Windows.
 * @details
 * The driver is embedded as an asset (assets/driver/dirtyrammark.sys). It is deployed (copied to System32\drivers,
 * registered in the service manager and started) the first time it's needed and removed when the app exits. Needs an
 * elevated process (see privileges.h). The driver is not signed by Microsoft, so Windows loads it only in test-signing
 * mode (`bcdedit /set testsigning on` and a reboot; Secure Boot must be off).
 */
namespace driver {

class Device {
public:
    /// Deploys the driver.
    /// @throws AException with a hint if that's impossible
    Device();
    /// Unloads the driver and deletes its files.
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    uint32_t pciRead(uint32_t bus, uint32_t device, uint32_t function, uint32_t offset) const;

    /// AMD System Management Network register.
    uint32_t smnRead(uint32_t address) const;

    /// Dword of the physical memory-mapped I/O.
    uint32_t mmioRead(uint64_t physicalAddress) const;

private:
    void* mDevice = nullptr;
    void cleanup() noexcept;
};

/// The driver is deployed on the first call and removed on exit of the process.
/// @throws privileges::Required if the process isn't elevated
const Device& instance();

}   // namespace driver
