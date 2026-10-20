#pragma once

#include <AUI/Common/AOptional.h>

/**
 * @brief Current CPU package power draw, watts.
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_power.cpp).
 * @details
 * Derived from an energy counter, so the value is averaged between two consecutive calls; calls made too close to each
 * other return the previous value. Cheap enough to be polled a few times per second.
 */
AOptional<float> cpuPowerWatts();
