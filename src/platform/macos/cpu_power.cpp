#include <cpu_power.h>

// No unprivileged way to read the CPU power draw here (yet).
AOptional<float> cpuPowerWatts() { return std::nullopt; }
