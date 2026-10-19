#include <cpu_frequency.h>

// No unprivileged way to read the current CPU frequency here (yet).
AOptional<float> cpuFrequencyGHz() { return std::nullopt; }
