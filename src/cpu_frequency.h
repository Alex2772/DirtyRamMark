#pragma once

#include <vector>
#include <AUI/Common/AOptional.h>

/**
 * @brief Current CPU frequency, GHz: the fastest core at the moment (that's what "boost" is about).
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_frequency.cpp).
 * @details
 * Doesn't need elevated privileges. Cheap enough to be polled a few times per second.
 */
AOptional<float> cpuFrequencyGHz();

/**
 * @brief Current frequency of every logical processor, GHz, indexed by the processor number.
 * @return Empty if the platform can't tell; 0 for a processor that is offline or has no reading right now.
 * @details Same constraints as cpuFrequencyGHz(), which is the maximum of this.
 */
std::vector<float> cpuFrequenciesGHz();
