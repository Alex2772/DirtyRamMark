#include <cpu_temperature.h>

// No unprivileged way to read the CPU temperature here (yet).
AOptional<float> cpuTemperatureCelsius() { return std::nullopt; }

AOptional<float> coolantTemperatureCelsius() { return std::nullopt; }

// No unprivileged way to read the VRM temperature and the pump speed here.
AOptional<float> vrmTemperatureCelsius() { return std::nullopt; }

AOptional<float> pumpRpm() { return std::nullopt; }
