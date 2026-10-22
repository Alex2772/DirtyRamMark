#include <chrono>
#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/APath.h>
#include <AUI/Common/AByteBuffer.h>
#include <cpu_power.h>

namespace {

AOptional<uint64_t> readCounter(const APath& path) {
    try {
        const auto text = AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream(path))).trim(" \t\r\n");
        if (auto value = text.toULong()) {
            return *value;
        }
    } catch (const AException&) {
    }
    return std::nullopt;
}

/// The RAPL package domain: `intel-rapl:N` (the AMD driver registers under the same name), not its `:N:M` subdomains.
AOptional<APath> findPackageDomain() {
    try {
        for (const auto& domain : APath("/sys/class/powercap").listDir()) {
            const auto name = domain.filename();
            if (name.startsWith("intel-rapl:") && !AString(name.substr(11)).contains(':') && readCounter(domain / "energy_uj")) {
                return domain;
            }
        }
    } catch (const AException&) {
    }
    return std::nullopt;
}

}   // namespace

AOptional<float> cpuPowerWatts() {
    using Clock = std::chrono::steady_clock;
    static const auto domain = findPackageDomain();
    if (!domain) {
        return std::nullopt;
    }
    static const uint64_t range = readCounter(*domain / "max_energy_range_uj").valueOr(0);
    static AOptional<uint64_t> lastEnergy;
    static Clock::time_point lastTime;
    static AOptional<float> lastWatts = 0.f;   // until there are two samples

    const auto energy = readCounter(*domain / "energy_uj");
    if (!energy) {
        return std::nullopt;
    }
    const auto now = Clock::now();
    if (lastEnergy) {
        const auto dt = std::chrono::duration<double>(now - lastTime).count();
        if (dt < 0.05) {
            return lastWatts;   // too soon for a meaningful average
        }
        // the counter wraps around at max_energy_range_uj
        const uint64_t delta = *energy >= *lastEnergy ? *energy - *lastEnergy : (range ? range - *lastEnergy + *energy : 0);
        lastWatts = float(double(delta) / 1e6 / dt);
    }
    lastEnergy = energy;
    lastTime = now;
    return lastWatts;
}
