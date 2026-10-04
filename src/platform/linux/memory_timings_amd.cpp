#include <cstdlib>

#include <AUI/Common/AByteBuffer.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Platform/AProcess.h>
#include <memory_timings.h>

using namespace memory_timings;

namespace {

constexpr unsigned AMD_CHANNELS_TO_PROBE = 2;

/// Reads SMN registers through the index/data pair of the root complex (00:00.0, config 0x60/0x64) with a single
/// privileged setpci invocation, so the index/data sequence is not interleaved with anything of ours.
std::optional<AVector<uint32_t>> readSmn(const AVector<uint32_t>& addresses) {
    AStringVector args = { "setpci", "-s", "00:00.0" };
    for (auto address : addresses) {
        args << "60.l={:x}"_format(address) << "64.l";
    }
    auto process = AProcess::create({
      .executable = "/usr/bin/pkexec",
      .args = AProcess::ArgStringList { .list = std::move(args) },
    });
    AByteBuffer output;
    AObject::connect(process->stdOut, AObject::GENERIC_OBSERVER, [&](AByteBuffer o) { output << o; });
    process->run();
    if (process->waitForExitCode() != 0) {
        ALogger::warn("MemoryTimings") << "pkexec setpci failed";
        return std::nullopt;
    }
    AVector<uint32_t> result;
    for (const auto& line : AString::fromUtf8(output).split('\n')) {
        auto trimmed = line.trim();
        if (trimmed.empty()) {
            continue;
        }
        result << uint32_t(std::strtoul(trimmed.toStdString().c_str(), nullptr, 16));
    }
    if (result.size() != addresses.size()) {
        ALogger::warn("MemoryTimings") << "unexpected setpci output";
        return std::nullopt;
    }
    return result;
}

}   // namespace

AJson::Object memory_timings::readAmdTimings(const CpuId& cpu) {
    auto type = amdDdrTypeOf(cpu.family, cpu.model);
    AVector<uint32_t> addresses;
    for (unsigned channel = 0; channel < AMD_CHANNELS_TO_PROBE; ++channel) {
        for (auto address : amdUmcRegisters()) {
            addresses << ((channel * AMD_UMC_CHANNEL_STRIDE) | address);
        }
    }
    auto values = readSmn(addresses);
    if (!values) {
        return {};
    }

    AJson::Object result;
    size_t i = 0;
    for (unsigned channel = 0; channel < AMD_CHANNELS_TO_PROBE; ++channel) {
        AMap<uint32_t, uint32_t> registers;
        for (auto address : amdUmcRegisters()) {
            registers[address] = (*values)[i++];
        }
        auto decoded = decodeAmdUmc(registers, type);
        if (!decoded.empty()) {
            result["Channel {}"_format(channel)] = std::move(decoded);
        }
    }
    return result;
}
