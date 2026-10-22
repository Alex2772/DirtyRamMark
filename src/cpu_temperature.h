#pragma once

#include <AUI/Common/AOptional.h>

/**
 * @brief Current CPU temperature (package / die control temperature), degrees Celsius.
 * @return std::nullopt if the platform can't tell. Implemented per platform (src/platform/<os>/cpu_temperature.cpp).
 * @details
 * Cheap enough to be polled a few times per second. Doesn't need elevated privileges, except on Windows: the real die
 * sensor is read through the PawnIO driver, which needs an administrator; otherwise the hottest ACPI thermal zone is
 * used, which many PCs don't have.
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

/**
 * @brief Temperature of the motherboard's CPU voltage regulator (VRM / MOSFETs), degrees Celsius.
 * @return std::nullopt if the board doesn't expose it. Implemented per platform alongside cpuTemperatureCelsius().
 * @details
 * Doesn't need elevated privileges.
 */
AOptional<float> vrmTemperatureCelsius();

/**
 * @brief Rotation speed of the pump of an AIO cooler / custom loop, revolutions per minute.
 * @return std::nullopt if there is no pump the platform can read. Implemented per platform alongside
 * cpuTemperatureCelsius().
 * @details
 * Doesn't need elevated privileges.
 */
AOptional<float> pumpRpm();
