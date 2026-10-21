#pragma once

#include <AUI/Common/AString.h>
#include <AUI/Common/AVector.h>
#include <AUI/View/AView.h>
#include <AUI/Util/Declarative/Contracts.h>

namespace declarative {

/**
 * @brief Analog gauge with a needle, like a car's tachometer.
 * @details
 * The dial spans 270 degrees; the needle is damped, so it "revs" towards the value instead of jumping. The scale is
 * split into colored zones: green up to `yellowFrom`, then yellow up to `redFrom`, then red up to `maroonFrom`, then
 * maroon ("super dangerous") up to `max`. A zone that starts at or above `max` is not shown.
 *
 * ```cpp
 * Gauge {
 *     .value = AUI_REACT(*state->temperature),
 *     .min = 0, .max = 110,
 *     .yellowFrom = 70, .redFrom = 85, .maroonFrom = 95,
 *     .title = "CPU", .unit = "°C",
 * }
 * ```
 */
struct Gauge {
    /// Current value of the needle (clamped to [min, max]).
    contract::In<float> value = 0.f;
    contract::In<float> min = 0.f;
    contract::In<float> max = 1.f;
    contract::In<float> yellowFrom = 1.f;
    contract::In<float> redFrom = 1.f;
    contract::In<float> maroonFrom = 1.f;

    /// Caption inside the dial, e.g. "FP32".
    AString title;

    /// Unit under the digital readout, e.g. "TFLOPS".
    AString unit;

    /// Digits after the decimal point in the digital readout.
    int precision = 0;

    /// Digits after the decimal point in the scale numbers.
    int scalePrecision = 0;

    /// Number of intervals between the numbered ticks of the scale (the numbers are min + i * (max - min) / majorTicks).
    int majorTicks = 10;

    /// The dial catches fire at `fireFrom` and burns the hardest at `fireFull`. Disabled when `fireFrom` >= `fireFull`.
    float fireFrom = 0.f;
    float fireFull = 0.f;

    /// A reference point outside of the dial: a label at the given value; lights up once the needle passes it.
    struct Marker {
        float value;
        AString label;
    };
    AVector<Marker> markers;

    /// Whether the needle trembles under load (engine vibration).
    bool jitter = true;

    _<AView> operator()();
};

}   // namespace declarative
