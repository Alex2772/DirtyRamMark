#include <cstdlib>

#include <AUI/Common/AByteBuffer.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Platform/AProcess.h>
#include <memory_timings.h>

using namespace memory_timings;

namespace {

/// One privileged invocation: reads MCHBAR from PCI config space of 00:00.0, then every requested dword from
/// /dev/mem. Prints one hex dword per line, in order of the requested offsets.
std::vector<uint32_t> readMchbar(IntelLayout layout, const AVector<uint32_t>& offsets) {
    static constexpr auto SCRIPT = R"(
lo=$(setpci -s 00:00.0 48.l) && hi=$(setpci -s 00:00.0 4c.l) || { echo "setpci failed" >&2; exit 2; }
mask=$1; shift
base=$(( ((0x$hi << 32) | 0x$lo) ))
[ $((base & 1)) -eq 1 ] || { echo "MCHBAR is disabled" >&2; exit 3; }
base=$(( base & mask ))
for off in "$@"; do
    dd if=/dev/mem bs=4 count=1 skip=$(( (base + off) / 4 )) 2>/dev/null | od -An -v -tx4 | tr -d ' ' || exit 4
done
)";
    AStringVector args = { "/bin/sh", "-c", SCRIPT, "sh" };
    // MCHBAR field: bits 38:16 (Skylake family) or 41:17 (Alder Lake)
    args << (layout == IntelLayout::SKYLAKE ? "0x7FFFFF0000" : "0x3FFFFFE0000");
    for (auto offset : offsets) {
        args << "{}"_format(offset);
    }

    auto process = AProcess::create({
      .executable = "/usr/bin/pkexec",
      .args = AProcess::ArgStringList { .list = std::move(args) },
    });
    AByteBuffer out, err;
    AObject::connect(process->stdOut, AObject::GENERIC_OBSERVER, [&](AByteBuffer o) { out << o; });
    AObject::connect(process->stdErr, AObject::GENERIC_OBSERVER, [&](AByteBuffer o) { err << o; });
    process->run();
    const int exitCode = process->waitForExitCode();
    if (exitCode != 0) {
        throw AException("Failed to read MCHBAR (exit code {}): {}"_format(exitCode, AString::fromUtf8(err).trim()));
    }

    std::vector<uint32_t> result;
    for (const auto& line : AString::fromUtf8(out).split('\n')) {
        auto trimmed = line.trim();
        if (!trimmed.empty()) {
            result.push_back(uint32_t(std::strtoul(trimmed.toStdString().c_str(), nullptr, 16)));
        }
    }
    if (result.size() != offsets.size()) {
        // dd prints nothing when /dev/mem refuses the read (CONFIG_STRICT_DEVMEM)
        throw AException("Unable to read MCHBAR through /dev/mem (got {} of {} registers). Is CONFIG_STRICT_DEVMEM "
                         "enabled? Try booting with iomem=relaxed."_format(result.size(), offsets.size()));
    }
    return result;
}

}   // namespace

AJson::Object memory_timings::readIntelTimings(IntelLayout layout) {
    AVector<uint32_t> offsets = { INTEL_MC_BIOS_DATA };
    for (auto base : intelChannelBases(layout)) {
        for (auto offset : intelChannelRegisters(layout)) {
            offsets << (base + offset);
        }
    }
    const auto values = readMchbar(layout, offsets);

    AJson::Object clock = decodeIntelClock(values[0], layout);
    AJson::Object result;
    size_t i = 1;
    unsigned index = 0;
    for ([[maybe_unused]] auto base : intelChannelBases(layout)) {
        AMap<uint32_t, uint32_t> registers;
        for (auto offset : intelChannelRegisters(layout)) {
            registers[offset] = values[i++];
        }
        auto channel = decodeIntelChannel(registers, layout);
        if (!channel.empty()) {
            // frequency is shared by the whole memory controller
            auto& target = result["Channel {}"_format(index)];
            for (auto& [k, v] : clock) {
                target[k] = v;
            }
            for (auto& [k, v] : channel) {
                target[k] = v;
            }
        }
        ++index;
    }
    return result;
}

