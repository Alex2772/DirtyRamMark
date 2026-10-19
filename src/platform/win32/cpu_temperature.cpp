#include <cpu_temperature.h>

// No unprivileged way to read the CPU temperature here (yet).
AOptional<float> cpuTemperatureCelsius() { return std::nullopt; }
