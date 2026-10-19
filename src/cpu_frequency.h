#pragma once

#include <AUI/Common/AOptional.h>

/**
 * @brief Current CPU frequency, GHz: the fastest core at the moment (that's what "boost" is about).
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_frequency.cpp).
 * @details
 * Doesn't need elevated privileges. Cheap enough to be polled a few times per second.
 */
AOptional<float> cpuFrequencyGHz();
