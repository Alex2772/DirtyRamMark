#pragma once

#include <AUI/Common/AOptional.h>

/**
 * @brief Current CPU temperature (package / die control temperature), degrees Celsius.
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_temperature.cpp).
 * @details
 * Doesn't need elevated privileges. Cheap enough to be polled a few times per second.
 */
AOptional<float> cpuTemperatureCelsius();
