#pragma once

#include <AUI/Common/AOptional.h>

/**
 * @brief Total CPU load across all logical cores, 0..1, since the previous call (the first call: since boot).
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_usage.cpp).
 * @details
 * Doesn't need elevated privileges. Cheap enough to be polled a few times per second; keeps the previous counters
 * internally, so call it from one thread only.
 */
AOptional<float> cpuUsageFraction();
