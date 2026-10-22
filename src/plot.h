#pragma once

#include <chrono>
#include <functional>
#include <AUI/Common/AColor.h>
#include <AUI/Common/AString.h>
#include <AUI/Common/AVector.h>
#include <AUI/View/AView.h>
#include <AUI/Util/Declarative/Contracts.h>

namespace declarative {

/**
 * @brief Real-time multi-series line chart: new data appears on the right edge and flows to the left.
 * @details
 * The plot polls `readMeasurements` every `pollInterval` and stamps the returned values with the current time. Each
 * returned value belongs to the y axis of the same index in `yAxisScales`, so a single plot shows any number of series,
 * each with its own scale (`extraSeriesAxes` adds more curves to the existing axes). The axes (a colored line with ticks, tip numbers and the name of the series) are on the
 * right, one column per axis. The grid is shared: the plot height is split into `yDivisions` equal parts, so the ticks
 * of every axis are aligned with the horizontal grid lines; the vertical lines are drawn at "nice" time steps and flow
 * together with the data. The older the data, the more transparent the curve: it fades out towards the left edge.
 *
 * The plot knows nothing about what is measured. Interesting points (a peak, a threshold crossing, ...) are found by the
 * caller and passed as `markers`: a point in the plot's coordinates (time and value on one of the axes) plus an
 * arbitrary view that is shown at that point and flows left together with the data.
 *
 * ```cpp
 * using namespace std::chrono_literals;
 * Plot {
 *     .pollInterval = 100ms,
 *     .yAxisScales = AVector<Plot::YAxisScale> { { 0x2ecc71_rgb, "TFLOPS", 0.f, 3.f }, { 0xe74c3c_rgb, "°C", 0.f, 110.f } },
 *     .xAxisDistance = 1min,
 *     .readMeasurements = [] { return AVector<float> { readTflops(), readTemperature() }; },
 * }
 * ```
 */
struct Plot {
    using Clock = std::chrono::steady_clock;

    struct YAxisScale {
        AColor color;
        AString name;
        float min;
        float max;

        bool operator==(const YAxisScale&) const = default;
    };

    /// A view pinned to a point of the plot. It is shown above the point, its tail pointing at the point.
    struct Marker {
        Clock::time_point time;
        float value;           // in units of the axis `axis`
        size_t axis = 0;       // index in yAxisScales
        _<AView> view;

        bool operator==(const Marker&) const = default;
    };

    /// How often `readMeasurements` is called.
    contract::In<std::chrono::milliseconds> pollInterval = std::chrono::milliseconds(100);

    contract::In<AVector<YAxisScale>> yAxisScales = AVector<YAxisScale> {};

    /// Width of the visible time span.
    contract::In<std::chrono::seconds> xAxisDistance = std::chrono::seconds(60);

    /// Series drawn in addition to one per axis, in the color of an existing axis and without an axis of their own:
    /// element k is the axis of the value with the index `yAxisScales.size() + k` returned by `readMeasurements`.
    /// They are drawn below the series of the axes.
    AVector<size_t> extraSeriesAxes = {};

    /// A distribution of a group of values (e.g. the frequencies of all the cores) drawn below all the curves: every
    /// value of the group gets a translucent band from the mean of the group to the value, so the bands pile up where
    /// the values are dense. The group is `count` consecutive values returned by `readMeasurements` starting with the
    /// index `firstSeries`; they are not drawn as curves, so they can be placed after the values of the axes and of
    /// `extraSeriesAxes`. A NaN value is skipped (and doesn't take part in the mean).
    struct Heatmap {
        size_t firstSeries;
        size_t count;
        size_t axis;     // index in yAxisScales: the scale of all the values of the group
        /// The alpha is the opacity where all the values of the group overlap (that is next to the mean); the opacity
        /// of a single band is derived from it and `count`.
        AColor color;
    };
    AVector<Heatmap> heatmaps = {};

    /// Returns one value per element of `yAxisScales`, then one per element of `extraSeriesAxes`, then whatever the
    /// heatmaps use (a missing value is skipped). Called on the UI thread.
    std::function<AVector<float>()> readMeasurements;

    contract::In<AVector<Marker>> markers = AVector<Marker> {};

    /// Number of intervals between grid lines (and axis ticks) along the y axes.
    int yDivisions = 10;

    _<AView> operator()();
};

}   // namespace declarative
