#include <chrono>

#include <cpu_power.h>

#include "die_sensor.h"

AOptional<float> cpuPowerWatts() {
    using Clock = std::chrono::steady_clock;
    static AOptional<uint32_t> lastCounter;
    static Clock::time_point lastTime;
    static AOptional<float> lastWatts = 0.f;   // until there are two samples

    // The package energy counter through PawnIO (administrator only); there is no unprivileged way on Windows.
    const auto energy = DieSensor::instance().packageEnergy();
    if (!energy) {
        return std::nullopt;
    }
    const auto now = Clock::now();
    if (lastCounter) {
        const auto dt = std::chrono::duration<double>(now - lastTime).count();
        if (dt < 0.05) {
            return lastWatts;   // too soon for a meaningful average
        }
        const uint32_t delta = energy->counter - *lastCounter;   // the 32-bit counter wraps around: unsigned math handles it
        lastWatts = float(double(delta) * energy->unitJoules / dt);
    }
    lastCounter = energy->counter;
    lastTime = now;
    return lastWatts;
}
