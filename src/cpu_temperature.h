#pragma once

#include <AUI/Common/AOptional.h>

/**
 * @brief Current CPU temperature (package / die control temperature), degrees Celsius.
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_temperature.cpp).
 * @details
 * Doesn't need elevated privileges. Cheap enough to be polled a few times per second.
 */
AOptional<float> cpuTemperatureCelsius();

/**
 * @brief Current liquid temperature of an AIO cooler / custom loop, degrees Celsius.
 * @return std::nullopt if there is no cooler the platform can read. Implemented per platform alongside
 * cpuTemperatureCelsius().
 * @details
 * Doesn't need elevated privileges.
 */
AOptional<float> coolantTemperatureCelsius();
