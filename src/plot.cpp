#include "plot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <optional>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/Common/ATimer.h>
#include <AUI/Font/AFontStyle.h>
#include <AUI/Render/ABrush.h>
#include <AUI/Render/ARenderContext.h>
#include <AUI/Render/IRenderer.h>
#include <AUI/Render/RenderHints.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AViewContainer.h>

using namespace ass;
using namespace std::chrono_literals;

namespace {

using Plot = declarative::Plot;
using Clock = Plot::Clock;

constexpr int FADE_BANDS = 32;          // the curve is drawn in this many strips of different transparency
constexpr float FADE_FRACTION = 0.6f;   // the part of the width (from the left) where the curve fades out
constexpr float LINE_WIDTH_DP = 2.f;
constexpr float MARKER_GAP_DP = 1.f;
constexpr AColor COLOR_GRID = 0x8a97a6_rgb;

float seconds(Clock::duration d) { return std::chrono::duration<float>(d).count(); }

/// Smallest "nice" step (1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10 min, ...) that gives at most `maxLines` lines on the span.
float niceTimeStep(float spanSeconds, int maxLines) {
    static constexpr float STEPS[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
    for (float step : STEPS) {
        if (spanSeconds / step <= float(maxLines)) {
            return step;
        }
    }
    return STEPS[std::size(STEPS) - 1];
}

AString formatTick(float value, float step) {
    const int decimals = std::abs(step - std::round(step)) < 1e-3f ? 0 : (step >= 0.1f ? 1 : 2);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, double(value));
    return buffer;
}

class PlotView : public AViewContainerBase {
public:
    AProperty<std::chrono::milliseconds> pollInterval = 100ms;
    AProperty<AVector<Plot::YAxisScale>> yAxisScales;
    AProperty<std::chrono::seconds> xAxisDistance = 60s;
    AProperty<AVector<Plot::Marker>> markers;
    std::function<AVector<float>()> readMeasurements;
    AVector<size_t> extraSeriesAxes;
    AVector<Plot::Fill> fills;
    int yDivisions = 10;

    PlotView() {
        connect(pollInterval.changed, this, [this](std::chrono::milliseconds) { restartTimer(); });
        connect(yAxisScales.changed, this, [this](const AVector<Plot::YAxisScale>&) { redraw(); });
        connect(xAxisDistance.changed, this, [this](std::chrono::seconds) { redraw(); });
        connect(markers.changed, this, [this](const AVector<Plot::Marker>&) { updateMarkerViews(); });
    }

    void render(ARenderContext ctx) override {
        AView::render(ctx);
        const auto now = Clock::now();
        const auto& scales = *yAxisScales;
        const auto geometry = computeGeometry(scales);

        drawGrid(ctx, geometry, now);
        drawFills(ctx, geometry, scales, now);
        drawSeries(ctx, geometry, scales, now);
        drawAxes(ctx, geometry, scales);
        drawMarkers(ctx, geometry, scales, now);

        redraw();   // the data flows left continuously, not only when a sample arrives
    }

    void restartTimer() {
        mTimer = _new<ATimer>(*pollInterval);
        connect(mTimer->fired, this, &PlotView::poll);
        mTimer->start();
    }

protected:
    void onLayout(glm::ivec2 size) override {
        AViewContainerBase::onLayout(size);
        // markers are positioned in render() since they move; they only need their size here (and the layout() call
        // that allows them to be rendered)
        for (const auto& marker : *markers) {
            if (!marker.view) {
                continue;
            }
            const auto viewSize = marker.view->measure(AConstraints {});
            marker.view->layout(marker.view->getPosition(), viewSize);
        }
    }

private:
    struct Sample {
        Clock::time_point time;
        AVector<float> values;
    };

    struct Geometry {
        float left, right, top, bottom;   // plot area
        float axisWidth;                  // width of one axis column
        AFontStyle font;

        float width() const { return right - left; }
        float height() const { return bottom - top; }
    };

    std::deque<Sample> mSamples;
    _<ATimer> mTimer;

    void poll() {
        if (!readMeasurements) {
            return;
        }
        const auto now = Clock::now();
        mSamples.push_back({ now, readMeasurements() });
        const auto keepFrom = now - *xAxisDistance - *pollInterval * 2;   // one sample before the left edge: the curve doesn't start in the middle
        while (mSamples.size() > 2 && mSamples[1].time < keepFrom) {
            mSamples.pop_front();
        }
        redraw();
    }

    void updateMarkerViews() {
        AVector<_<AView>> views;
        for (const auto& marker : *markers) {
            if (marker.view) {
                views << marker.view;
            }
        }
        setViews(std::move(views));
        requestLayout();
    }

    Geometry computeGeometry(const AVector<Plot::YAxisScale>& scales) const {
        Geometry g;
        g.font.size = unsigned(AMetric(8_pt).getValuePx());
        const float tickLength = float(AMetric(4_dp).getValuePx());
        const float padding = float(AMetric(4_dp).getValuePx());

        float widestLabel = 0.f;   // the widest tip over all of the axes: the columns are equal, so they look like a ruler
        for (const auto& scale : scales) {
            const float step = (scale.max - scale.min) / float(std::max(1, yDivisions));
            widestLabel = std::max({ widestLabel, float(g.font.getWidth(formatTick(scale.min, step))),
                                     float(g.font.getWidth(formatTick(scale.max, step))) });
        }
        g.axisWidth = tickLength + padding + widestLabel + padding * 2.f;
        g.left = 0.f;
        g.right = std::max(0.f, float(getWidth()) - g.axisWidth * float(scales.size()));
        // the name of an axis is above the axis, so the top of the plot is lower; the last tip sticks out by half a line
        g.top = float(g.font.size) * 2.2f;
        g.bottom = std::max(g.top, float(getHeight()) - float(g.font.size) * 0.7f);
        return g;
    }

    float xOf(const Geometry& g, Clock::time_point t, Clock::time_point now) const {
        const float span = std::max(1e-3f, std::chrono::duration<float>(*xAxisDistance).count());
        return g.right - seconds(now - t) / span * g.width();
    }

    static float yOf(const Geometry& g, const Plot::YAxisScale& scale, float value) {
        const float range = std::max(1e-6f, scale.max - scale.min);
        return g.bottom - std::clamp((value - scale.min) / range, 0.f, 1.f) * g.height();
    }

    /// Transparency of the curve at x: 0 at the left edge, 1 further than FADE_FRACTION of the width.
    static float fadeAt(const Geometry& g, float x) {
        const float t = std::clamp((x - g.left) / std::max(1.f, g.width() * FADE_FRACTION), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);   // smoothstep
    }

    static AColor withAlpha(const AColor& color, float alpha) { return color.transparentize(1.f - alpha); }

    void drawGrid(ARenderContext& ctx, const Geometry& g, Clock::time_point now) const {
        if (g.width() <= 0.f) {
            return;
        }
        const AMetric width(1.f, AMetric::T_PX);

        AVector<std::pair<glm::vec2, glm::vec2>> horizontal;
        for (int i = 0; i <= yDivisions; ++i) {
            const float y = std::round(g.bottom - g.height() * float(i) / float(yDivisions));
            horizontal.push_back({ { g.left, y }, { g.right, y } });
        }
        ctx.render.lines(ASolidBrush { withAlpha(COLOR_GRID, 0.35f) }, horizontal, ABorderStyle::Solid {}, width);

        // vertical lines stick to the time (they flow together with the data), so they are placed on multiples of the step
        const float span = std::chrono::duration<float>(*xAxisDistance).count();
        const float step = niceTimeStep(span, 10);
        const float nowSeconds = std::chrono::duration<float>(now.time_since_epoch()).count();
        const float oldest = nowSeconds - span;
        for (float t = std::ceil(oldest / step) * step; t <= nowSeconds; t += step) {
            const float x = g.right - (nowSeconds - t) / span * g.width();
            const float alpha = fadeAt(g, x) * 0.35f;
            if (alpha <= 0.01f) {
                continue;
            }
            ctx.render.line(ASolidBrush { withAlpha(COLOR_GRID, alpha) }, { x, g.top }, { x, g.bottom },
                            ABorderStyle::Solid {}, width);
        }
    }

    /// The axis a series belongs to; the series of the axes come first, then `extraSeriesAxes`.
    size_t axisOfSeries(size_t series, size_t axisCount) const {
        if (series < axisCount) {
            return series;
        }
        return series - axisCount < extraSeriesAxes.size() ? extraSeriesAxes[series - axisCount] : axisCount;
    }

    /// The polyline of a series: a point per sample, then the last value held up to the right edge.
    AVector<glm::vec2> curveOf(const Geometry& g, const Plot::YAxisScale& scale, size_t series, Clock::time_point now) const {
        AVector<glm::vec2> points;
        float lastValue = 0.f;
        bool hasLast = false;
        for (const auto& sample : mSamples) {
            if (series >= sample.values.size()) {
                continue;
            }
            lastValue = sample.values[series];
            hasLast = true;
            points.push_back({ xOf(g, sample.time, now), yOf(g, scale, lastValue) });
        }
        if (hasLast) {
            points.push_back({ g.right, yOf(g, scale, lastValue) });   // between the polls the curve is held, not cut
        }
        return points;
    }

    static int bandOf(const Geometry& g, float x) {
        return x < g.left ? -1 : std::min(FADE_BANDS - 1, int((x - g.left) / g.width() * float(FADE_BANDS)));
    }

    void drawFills(ARenderContext& ctx, const Geometry& g, const AVector<Plot::YAxisScale>& scales,
                   Clock::time_point now) const {
        if (mSamples.empty() || g.width() <= 0.f) {
            return;
        }
        for (const auto& fill : fills) {
            const size_t axisA = axisOfSeries(fill.seriesA, scales.size());
            const size_t axisB = axisOfSeries(fill.seriesB, scales.size());
            if (axisA >= scales.size() || axisB >= scales.size()) {
                continue;
            }
            const auto a = curveOf(g, scales[axisA], fill.seriesA, now);
            const auto b = curveOf(g, scales[axisB], fill.seriesB, now);
            if (a.size() < 2 || b.size() < 2) {
                continue;
            }

            // the renderer has no polygons: the area is a column of one pixel width per pixel of the plot
            const auto yAt = [](const AVector<glm::vec2>& curve, size_t& cursor, float x) -> std::optional<float> {
                if (x < curve.front().x || x > curve.back().x) {
                    return std::nullopt;
                }
                while (cursor + 2 < curve.size() && curve[cursor + 1].x < x) {
                    ++cursor;
                }
                const auto& p = curve[cursor];
                const auto& q = curve[cursor + 1];
                const float t = q.x > p.x ? std::clamp((x - p.x) / (q.x - p.x), 0.f, 1.f) : 1.f;
                return p.y + (q.y - p.y) * t;
            };
            std::array<AVector<std::pair<glm::vec2, glm::vec2>>, FADE_BANDS> columns;
            size_t cursorA = 0;
            size_t cursorB = 0;
            for (float x = std::floor(g.left) + 0.5f; x < g.right; x += 1.f) {
                const auto ya = yAt(a, cursorA, x);
                const auto yb = yAt(b, cursorB, x);
                if (ya && yb) {
                    columns[size_t(bandOf(g, x))].push_back({ { x, *ya }, { x, *yb } });
                }
            }
            for (int band = 0; band < FADE_BANDS; ++band) {
                if (columns[size_t(band)].empty()) {
                    continue;
                }
                const float bandCenterX = g.left + (float(band) + 0.5f) / float(FADE_BANDS) * g.width();
                AColor color = fill.color;
                color.a *= fadeAt(g, bandCenterX);
                ctx.render.lines(ASolidBrush { color }, columns[size_t(band)], ABorderStyle::Solid {}, AMetric(1.f, AMetric::T_PX));
            }
        }
    }

    void drawSeries(ARenderContext& ctx, const Geometry& g, const AVector<Plot::YAxisScale>& scales,
                    Clock::time_point now) const {
        if (mSamples.empty() || g.width() <= 0.f) {
            return;
        }
        const AMetric width(LINE_WIDTH_DP, AMetric::T_DP);
        // the extra series go first, so they are below the series of the axes
        for (size_t order = 0; order < scales.size() + extraSeriesAxes.size(); ++order) {
            const bool isExtra = order < extraSeriesAxes.size();
            const size_t series = isExtra ? scales.size() + order : order - extraSeriesAxes.size();
            const size_t axis = axisOfSeries(series, scales.size());
            if (axis >= scales.size()) {
                continue;
            }
            // the curve is a single polyline, but a brush has one color: it is drawn in strips of equal transparency
            AVector<glm::vec2> run;
            int runBand = -1;
            const auto flush = [&] {
                if (runBand < 0 || run.size() < 2) {
                    return;
                }
                const float bandCenterX = g.left + (float(runBand) + 0.5f) / float(FADE_BANDS) * g.width();
                ctx.render.lines(ASolidBrush { withAlpha(scales[axis].color, fadeAt(g, bandCenterX)) }, run,
                                 ABorderStyle::Solid {}, width);
            };

            std::optional<glm::vec2> previous;
            for (const auto& p : curveOf(g, scales[axis], series, now)) {
                if (previous) {
                    const int band = bandOf(g, (previous->x + p.x) / 2.f);
                    if (band != runBand) {
                        flush();
                        run.clear();
                        run.push_back(*previous);
                        runBand = band;
                    }
                    run.push_back(p);
                }
                previous = p;
            }
            flush();
        }
    }

    void drawAxes(ARenderContext& ctx, const Geometry& g, const AVector<Plot::YAxisScale>& scales) const {
        const float tickLength = float(AMetric(4_dp).getValuePx());
        const float padding = float(AMetric(4_dp).getValuePx());
        const AMetric lineWidth(1.f, AMetric::T_PX);
        const float lineHeight = float(g.font.size);
        for (size_t axis = 0; axis < scales.size(); ++axis) {
            const auto& scale = scales[axis];
            const float x = g.right + g.axisWidth * float(axis) + 0.5f;
            const AColor color = scale.color;

            ctx.render.line(ASolidBrush { color }, { x, g.top }, { x, g.bottom }, ABorderStyle::Solid {}, AMetric(2.f, AMetric::T_PX));
            const float step = (scale.max - scale.min) / float(std::max(1, yDivisions));
            AVector<std::pair<glm::vec2, glm::vec2>> ticks;
            for (int i = 0; i <= yDivisions; ++i) {
                const float y = std::round(g.bottom - g.height() * float(i) / float(yDivisions));
                ticks.push_back({ { x, y }, { x + tickLength, y } });
                drawText(ctx, formatTick(scale.min + step * float(i), step), { x + tickLength + padding, y + lineHeight * 0.35f },
                         g.font, color);
            }
            ctx.render.lines(ASolidBrush { color }, ticks, ABorderStyle::Solid {}, lineWidth);
            drawText(ctx, scale.name, { x - 1.f, g.top - lineHeight * 0.8f }, g.font, color);
        }
    }

    void drawMarkers(ARenderContext& ctx, const Geometry& g, const AVector<Plot::YAxisScale>& scales,
                     Clock::time_point now) {
        const float gap = float(AMetric(MARKER_GAP_DP, AMetric::T_DP).getValuePx());
        for (const auto& marker : *markers) {
            if (!marker.view || marker.axis >= scales.size()) {
                continue;
            }
            const float x = xOf(g, marker.time, now);
            const float alpha = fadeAt(g, x);
            if (x < g.left || alpha <= 0.01f) {
                continue;
            }
            const auto size = marker.view->getSize();
            const float y = yOf(g, scales[marker.axis], marker.value);
            // above the point; near the edges the view is shifted so it stays within the plot, its tail is up to the view
            const float viewX = std::clamp(x - float(size.x) / 2.f, g.left, std::max(g.left, g.right - float(size.x)));
            const float viewY = std::max(0.f, y - float(size.y) - gap);
            marker.view->setPosition({ int(viewX), int(viewY) });

            RenderHints::PushColor color(ctx.render);
            ctx.render.setColor(AColor(1.f, 1.f, 1.f, alpha));
            drawView(marker.view, ctx);
        }
    }

    static void drawText(ARenderContext& ctx, const AString& text, glm::vec2 baseline, const AFontStyle& style, const AColor& color) {
        RenderHints::PushColor saved(ctx.render);
        ctx.render.setColor(color);
        ctx.render.string(baseline, text, style);
    }
};

}   // namespace

_<AView> declarative::Plot::operator()() {
    auto view = _new<PlotView>();
    view->readMeasurements = std::move(readMeasurements);
    view->yDivisions = std::max(1, yDivisions);
    view->extraSeriesAxes = std::move(extraSeriesAxes);
    view->fills = std::move(fills);
    yAxisScales.bindTo(view->yAxisScales.assignment());
    xAxisDistance.bindTo(view->xAxisDistance.assignment());
    markers.bindTo(view->markers.assignment());
    pollInterval.bindTo(view->pollInterval.assignment());
    view->restartTimer();   // a constant equal to the default doesn't emit `changed`
    view AUI_OVERRIDE_STYLE {
        MinSize { 200_dp, 120_dp },
        Expanding { 1, 1 },
    };
    return view;
}
