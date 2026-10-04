#pragma once

#include <cstdint>
#include <optional>
#include <AUI/Common/AMap.h>
#include <AUI/Json/AJson.h>

/**
 * @brief Actual (currently programmed) DRAM timings, read from the memory controller.
 * @details
 * Unlike SPD, this is what the BIOS really configured. Requires root (pkexec). Supported: AMD Zen 2..5 (UMC registers
 * over SMN). Returns an empty object if unsupported or access is denied.
 */
AJson::Object memoryTimings();

namespace memory_timings {

enum class AmdDdrType { DDR4, DDR5 };

struct CpuId {
    AString vendor;
    unsigned family = 0;
    unsigned model = 0;
};

/// Parses the first processor block of /proc/cpuinfo.
CpuId parseCpuInfo(const std::string& cpuinfo);

/// Reads and parses /proc/cpuinfo of this machine.
CpuId readCpuId();

/// The memory controller lives in the CPU, so its vendor is the CPU's: "AMD", "Intel" or the raw vendor_id.
AString controllerVendor(const CpuId& cpu);

/// @return {"vendor", "family", "model"} describing the memory controller.
AJson::Object describeController(const CpuId& cpu);

/// Zen 4/5 use DDR5, everything older uses DDR4.
AmdDdrType amdDdrTypeOf(unsigned family, unsigned model);

/// Register addresses (relative to the UMC channel) that decodeAmdUmc() needs.
const AVector<uint32_t>& amdUmcRegisters();

/// SMN address stride between UMC channels.
constexpr uint32_t AMD_UMC_CHANNEL_STRIDE = 0x100000;

/**
 * @param registers channel-relative UMC address (0x50xxx) -> value.
 * @return decoded timings; empty if the channel is not populated.
 */
AJson::Object decodeAmdUmc(const AMap<uint32_t, uint32_t>& registers, AmdDdrType type);

/// Layout of the Intel memory controller registers (MCHBAR). Differs between CPU generations.
enum class IntelLayout {
    SKYLAKE,   ///< Skylake, Kaby Lake, Coffee Lake, Comet Lake (10th Gen datasheet vol. 2)
    ALDER,     ///< Alder Lake, Raptor Lake (12th Gen datasheet vol. 2)
};

/// @return layout for the CPU or std::nullopt if the generation is not supported.
std::optional<IntelLayout> intelLayoutOf(unsigned family, unsigned model);

/// Offsets (relative to MCHBAR) of the channels' register blocks.
const AVector<uint32_t>& intelChannelBases(IntelLayout layout);

/// Dword offsets (relative to the channel base) that decodeIntelChannel() needs.
const AVector<uint32_t>& intelChannelRegisters(IntelLayout layout);

/// MC_BIOS_DATA (MCHBAR-relative): memory controller frequency and gear.
constexpr uint32_t INTEL_MC_BIOS_DATA = 0x5E04;

/// @return MCHBAR base address from the PCI config space dwords 0x48 (low) and 0x4c (high); 0 if disabled.
uint64_t intelMchbarBase(IntelLayout layout, uint32_t low, uint32_t high);

/// @return data rate and gear decoded from MC_BIOS_DATA.
AJson::Object decodeIntelClock(uint32_t mcBiosData, IntelLayout layout);

/**
 * @param registers channel-relative offset -> value (see intelChannelRegisters()).
 * @return decoded timings; empty if the channel is not populated (registers hold reset values).
 */
AJson::Object decodeIntelChannel(const AMap<uint32_t, uint32_t>& registers, IntelLayout layout);

/// Reads timings of all channels. Needs root (pkexec). Throws AException with a hint on failure.
AJson::Object readIntelTimings(IntelLayout layout);

}   // namespace memory_timings
