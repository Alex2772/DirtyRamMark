#include <algorithm>
#include <cmath>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/BackgroundImage.h>
#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/ASS/Property/Border.h>
#include <AUI/ASS/Property/BorderRadius.h>
#include <AUI/ASS/Property/FixedSize.h>
#include <AUI/ASS/Property/FontSize.h>
#include <AUI/ASS/Property/Padding.h>
#include <AUI/ASS/Property/TextColor.h>
#include <AUI/Common/ATimer.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Render/ARenderContext.h>
#include <AUI/Render/RenderHints.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AForEachUI.h>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/reverse.hpp>
#include <AUI/View/ACheckBox.h>
#include <AUI/View/AViewContainer.h>
#include <AUI/View/ASlider.h>

#include "gauge.h"
#include "cpu_frequency.h"
#include "cpu_stresstest.h"
#include "cpu_stresstest_window.h"
#include "cpu_temperature.h"
#include "cpu_usage.h"

using namespace ass;
using namespace declarative;
using namespace std::chrono_literals;

namespace {

constexpr float TFLOPS_MAX = 3.f;
constexpr float FREQUENCY_MAX_GHZ = 6.f;

/// Reference points for the TFLOPS gauge: theoretical peak of fp32 FMA (AVX2/NEON), all cores at a typical sustained
/// clock: cores x GHz x FLOP per cycle per core (32 for 2x256-bit FMA or an Apple P-core with 4x128-bit NEON FMA, 16
/// for a Gracemont/Apple E-core or 2x128-bit NEON). There is no authoritative table of these; they are computed, so
/// treat them as ballpark.
AVector<Gauge::Marker> referenceCpus() {
    return {
        { 0.15f, "RPi 5" },            // 4 x 2.4 GHz x 16
        { 0.55f, "Apple M1" },         // 4 P x 3.2 x 32 + 4 E x 2.1 x 16
        { 0.8f, "i5-12400" },          // 6 x 4.0 x 32
        { 1.0f, "Apple M3 Pro" },      // 6 P x 4.05 x 32 + 6 E x 2.75 x 16
        { 1.2f, "Ryzen 7 7800X3D" },   // 8 x 4.7 x 32
        { 1.4f, "i7-12700K" },         // 8 P x 4.7 x 32 + 4 E x 3.6 x 16
        { 1.7f, "Apple M3 Max" },      // 12 P x 4.05 x 32 + 4 E x 2.75 x 16
        { 2.0f, "Ryzen 9 5950X" },     // 16 x 3.9 x 32 (measured by others: ~2.1)
        { 2.5f, "Ryzen 9 7950X" },     // 16 x 4.9 x 32
    };
}

struct State : AObject {
    ui::CpuStressTest test;
    AProperty<float> coresFraction = 1.f;   // slider position of "Cores"
    const AVector<AProperty<bool>> enabledCores = makeEnabledCores();
    AProperty<float> load = 0.f;   // 0..1
    AProperty<float> tflops = 0.f;
    AProperty<float> temperature = 0.f;
    AProperty<float> frequency = 0.f;
    AProperty<float> usage = 0.f;   // percent, all cores
    _<ATimer> timer = _new<ATimer>(100ms);

    State() {
        for (unsigned i = 0; i < enabledCores.size(); ++i) {
            test.setCoreEnabled(i, *enabledCores[i]);
            connect(enabledCores[i].changed, [this, i](bool enabled) { test.setCoreEnabled(i, enabled); });
        }
        connect(timer->fired, me::update);
        timer->start();
        update();
    }

    /// Cores [0, position * count] are enabled, the rest are disabled; the checkboxes follow.
    void setCoresFraction(float fraction) {
        coresFraction = fraction;
        const auto count = unsigned(std::lround(fraction * float(enabledCores.size())));
        for (unsigned i = 0; i < enabledCores.size(); ++i) {
            const_cast<AProperty<bool>&>(enabledCores[i]) = i < count;
        }
    }

    unsigned enabledCount() const {
        return unsigned(std::count_if(enabledCores.begin(), enabledCores.end(), [](const auto& c) { return *c; }));
    }

    void setLoad(float value) {
        load = value;
        test.setLoad(value);
    }

    void update() {
        tflops = float(test.sampleTflops());
        temperature = cpuTemperatureCelsius().valueOr(0.f);
        frequency = cpuFrequencyGHz().valueOr(0.f);
        usage = cpuUsageFraction().valueOr(0.f) * 100.f;
    }

private:
    static AVector<AProperty<bool>> makeEnabledCores() {
        AVector<AProperty<bool>> result;
        result.reserve(ui::CpuStressTest::maxThreads());
        for (unsigned i = 0; i < ui::CpuStressTest::maxThreads(); ++i) {
            result.emplace_back(true);
        }
        return result;
    }
};

/// Vertical counterpart of the handle wrapper of declarative::Slider (see ASlider.cpp): 0 is at the bottom.
class VerticalSliderHandleWrapper : public AViewContainerBase {
public:
    /// `inset` is the distance in px from the top and the bottom edges to the centre of the handle at values 1 and 0, so
    /// that the handle doesn't stick out of its track.
    VerticalSliderHandleWrapper(_<AView> handle, AMetric inset) : mHandle(std::move(handle)), mInset(inset) {
        addView(mHandle);
    }

    void onPointerMove(glm::vec2 pos, const APointerMoveEvent& event) override {
        AViewContainerBase::onPointerMove(pos, event);
        if (!isPressed(event.pointerIndex)) {
            return;
        }
        emit valueChanged(valueAt(pos.y));
    }

    void onPointerPressed(const APointerPressedEvent& event) override {
        AViewContainerBase::onPointerPressed(event);
        emit valueChanged(valueAt(event.position.y));
    }

    bool consumesClick(const glm::ivec2& pos) override { return true; }

    void setValue(aui::float_within_0_1 value) {
        mValue = value;
        requestLayout();
    }

    AMinMaxAxis onComputeIntrinsicMinMaxAxis(int) override {
        auto minMax = mHandle->computeMinMaxAxis();
        return {
            .min = minMax.min,
            .max = minMax.max,
        };
    }

    emits<aui::float_within_0_1> valueChanged;

protected:
    void onLayout(glm::ivec2 size) override {
        auto handleSize = mHandle->measure(AConstraints::fixedInline(size.x));
        const float inset = insetPx();
        const float travel = std::max(0.f, float(size.y) - 2.f * inset);
        mHandle->layout({ 0, inset + travel * (1.f - mValue) - handleSize.y / 2.f }, { size.x, handleSize.y });
    }

private:
    float insetPx() const { return float(mInset.getValuePx()); }

    float valueAt(float y) const {
        const float inset = insetPx();
        const float travel = float(getSize().y) - 2.f * inset;
        return travel <= 0.f ? 0.f : std::clamp(1.f - (y - inset) / travel, 0.f, 1.f);
    }

    _<AView> mHandle;
    AMetric mInset;
    aui::float_within_0_1 mValue = 0;
};

// Textures cut from the mockups, see scripts/extract_stresstest_textures.py. The proportions are those of the files.
constexpr float TRACK_WIDTH_DP = 40.f;                           // track.png is 88x600 px
constexpr float KNOB_WIDTH_DP = 32.f;                            // the knob is a bit narrower than the track
constexpr float KNOB_HEIGHT_DP = KNOB_WIDTH_DP * 71 / 46;        // knob.png is 46x71 px
constexpr float KNOB_INSET_DP = KNOB_HEIGHT_DP / 2 + 6.f;        // keeps the knob inside the track
constexpr float PLATE_WIDTH_DP = 80.f;
constexpr float PLATE_HEIGHT_DP = PLATE_WIDTH_DP * 48 / 163;     // plate.png is 163x48 px
constexpr AColor COLOR_WINDOW_BACKGROUND = 0xe2e7ec_rgb;
constexpr AColor COLOR_PLATE_TEXT = 0xe8eef4_rgb;

constexpr float WIDE_KNOB_WIDTH_DP = 84.f;                       // the knob is stretched by Sizing::SPLIT_2X2
/// Background made of a texture; the corners keep their size, the middle row and column are stretched.
ass::BackgroundImage texture(const char* url, float texturePx, float widthDp) {
    return BackgroundImage { url, AColor::WHITE, Repeat::NONE, Sizing::SPLIT_2X2, {}, texturePx / widthDp };
}

/// The lit groove of a slider: shown only below the knob (the filled part of the slider).
class LitTrack : public AView {
public:
    explicit LitTrack(_weak<AView> knob) : mKnob(std::move(knob)) {}

    void render(ARenderContext ctx) override {
        auto knob = mKnob.lock();
        if (!knob) {
            AView::render(ctx);
            return;
        }
        const float y = std::clamp(
            float(knob->getPositionInWindow().y + knob->getHeight() / 2 - getPositionInWindow().y), 0.f, float(getHeight()));
        RenderHints::PushMask mask(ctx.render, [&] {
            ctx.render.rectangle(ASolidBrush { AColor::WHITE }, { 0.f, y }, { float(getWidth()), float(getHeight()) - y });
        });
        AView::render(ctx);
    }

private:
    _weak<AView> mKnob;
};

constexpr AColor COLOR_PANEL = 0xeef2f6_rgb;
constexpr AColor COLOR_PANEL_BORDER = 0xc3ccd6_rgb;
constexpr AColor COLOR_TICK_OFF = 0xaab2bb_rgb;

/// A column of ticks next to the "Load" slider: a tick is gray while the slider is below it and glows once the slider
/// reaches it. Purely decorative, it emphasizes the main control.
class LoadTicks : public AView {
public:
    AProperty<float> value = 0.f;

    explicit LoadTicks(AMetric inset) : mInset(inset) {
        connect(value.changed, this, [this](float) { redraw(); });
    }

    void render(ARenderContext ctx) override {
        AView::render(ctx);
        const float inset = float(mInset.getValuePx());
        const float travel = std::max(0.f, float(getHeight()) - 2.f * inset);
        const float tickHeight = float(AMetric(5.f, AMetric::T_DP).getValuePx());
        const float width = float(getWidth());
        for (int i = 0; i < TICKS; ++i) {
            const float f = float(i) / float(TICKS - 1);
            const float y = inset + travel * (1.f - f) - tickHeight / 2.f;
            if (*value + 1e-4f < f || *value < 1e-4f) {   // nothing is lit when the slider is at the bottom
                ctx.render.roundedRectangle(ASolidBrush { COLOR_TICK_OFF }, { width * 0.15f, y }, { width * 0.7f, tickHeight }, 1.5f);
                continue;
            }
            const AColor color = heat(f);
            const float glow = tickHeight * 0.8f;
            ctx.render.roundedRectangle(ASolidBrush { color.transparentize(0.75f) }, { 0.f, y - glow }, { width, tickHeight + 2.f * glow }, glow + tickHeight / 2.f);
            ctx.render.roundedRectangle(ASolidBrush { color }, { width * 0.1f, y }, { width * 0.8f, tickHeight }, 1.5f);
        }
    }

private:
    static constexpr int TICKS = 20;
    AMetric mInset;

    /// cyan -> amber -> red
    static AColor heat(float f) {
        const auto mix = [](glm::vec3 a, glm::vec3 b, float k) { return a + (b - a) * k; };
        const glm::vec3 cyan(0.22f, 0.82f, 1.f), amber(1.f, 0.69f, 0.13f), red(1.f, 0.25f, 0.15f);
        const auto c = f < 0.5f ? mix(cyan, amber, f * 2.f) : mix(amber, red, f * 2.f - 1.f);
        return AColor(c.r, c.g, c.b, 1.f);
    }
};

/// Vertical slider: a silver knob moves along a glowing track. `wide` is for the main control: a knob much wider
/// than the track.
_<AView> verticalSlider(contract::In<aui::float_within_0_1> value, contract::Slot<aui::float_within_0_1> onChanged, bool wide = false) {
    const float knobWidth = wide ? WIDE_KNOB_WIDTH_DP : KNOB_WIDTH_DP;
    const float inset = KNOB_INSET_DP;
    auto knob = _new<AView>() AUI_OVERRIDE_STYLE {
        texture(":img/stresstest/knob.png", 46, KNOB_WIDTH_DP),
        FixedSize { {}, AMetric(KNOB_HEIGHT_DP, AMetric::T_DP) },
        ACursor::NS_RESIZE,
    };
    auto track = _new<AView>() AUI_OVERRIDE_STYLE {
        texture(":img/stresstest/track.png", 88, TRACK_WIDTH_DP),
        FixedSize { AMetric(TRACK_WIDTH_DP, AMetric::T_DP), {} },
        MinSize { {}, 300_dp },
        Expanding { 0, 1 },
    };
    auto trackLit = _new<LitTrack>(knob) AUI_OVERRIDE_STYLE {
        texture(":img/stresstest/track_lit.png", 88, TRACK_WIDTH_DP),
        FixedSize { AMetric(TRACK_WIDTH_DP, AMetric::T_DP), {} },
        MinSize { {}, 300_dp },
        Expanding { 0, 1 },
    };
    auto wrapper = _new<VerticalSliderHandleWrapper>(std::move(knob), AMetric(inset, AMetric::T_DP));
    value.bindToCopy(ASlotDef { AUI_SLOT(wrapper.get())::setValue });
    onChanged.bindTo(wrapper->valueChanged);
    return Centered {
        Stacked {
          Centered { std::move(track) } AUI_OVERRIDE_STYLE { Expanding { 1, 1 } },
          Centered { std::move(trackLit) } AUI_OVERRIDE_STYLE { Expanding { 1, 1 } },
          Centered {
            std::move(wrapper) AUI_OVERRIDE_STYLE {
              FixedSize { AMetric(knobWidth, AMetric::T_DP), {} },
              Expanding { 0, 1 },
            },
          } AUI_OVERRIDE_STYLE { Expanding { 1, 1 } },
        } AUI_OVERRIDE_STYLE { FixedSize { AMetric(std::max(TRACK_WIDTH_DP, knobWidth), AMetric::T_DP), {} }, Expanding { 0, 1 } },
    } AUI_OVERRIDE_STYLE { Expanding { 0, 1 } };
}

_<AView> loadTicks(contract::In<float> value, float insetDp) {
    auto view = _new<LoadTicks>(AMetric(insetDp, AMetric::T_DP));
    value.bindTo(view->value.assignment());
    return view AUI_OVERRIDE_STYLE { FixedSize { 16_dp, {} }, Expanding { 0, 1 } };
}

/// Dark display under a slider.
_<AView> readoutPlate(contract::In<AString> readout) {
    return Centered {
        Label { std::move(readout) } AUI_OVERRIDE_STYLE { FontSize { 10_pt }, TextColor { COLOR_PLATE_TEXT }, ATextAlign::CENTER },
    } AUI_OVERRIDE_STYLE {
        texture(":img/stresstest/plate.png", 163, PLATE_WIDTH_DP),
        FixedSize { AMetric(PLATE_WIDTH_DP, AMetric::T_DP), AMetric(PLATE_HEIGHT_DP, AMetric::T_DP) },
    };
}

_<AView> captioned(const AString& caption, _<AView> slider, contract::In<AString> readout) {
    return Vertical {
        Label { caption } AUI_OVERRIDE_STYLE { FontSize { 12_pt }, ATextAlign::CENTER },
        std::move(slider) AUI_OVERRIDE_STYLE { Expanding { 0, 1 } },
        readoutPlate(std::move(readout)),
    } AUI_OVERRIDE_STYLE { LayoutSpacing { 6_dp }, Expanding { 0, 1 } };
}

}   // namespace

void ui::openCpuStressTestWindow(AWindow& parent) {
    auto window = _new<AWindow>(AWindow::Init {
      .title = "CPU Stress Test",
      .width = 720_dp,
      .height = AMetric(std::clamp(140 + 18 * int(ui::CpuStressTest::maxThreads()), 300, 900), AMetric::T_DP),
      .parent = AWindow::Init::Parent { .window = parent },
    });
    auto state = _new<State>();

    // a gauge of a value that can't be read here is not shown at all
    AVector<_<AView>> meters;
    if (cpuFrequencyGHz()) {
        meters << Gauge {
            .value = AUI_REACT(*state->frequency),
            .min = 0.f,
            .max = FREQUENCY_MAX_GHZ,
            .yellowFrom = FREQUENCY_MAX_GHZ - 3,
            .redFrom = FREQUENCY_MAX_GHZ - 2,
            .maroonFrom = FREQUENCY_MAX_GHZ - 1,
            .title = "CPU",
            .unit = "GHz",
            .precision = 2,
            .majorTicks = 6,
        };
    }
    if (cpuTemperatureCelsius()) {
        meters << Gauge {
            .value = AUI_REACT(*state->temperature),
            .min = 0.f,
            .max = 110.f,
            .yellowFrom = 60.f,
            .redFrom = 80.f,
            .maroonFrom = 95.f,
            .title = "CPU",
            .unit = "\u00b0C",
            .majorTicks = 11,
        };
    }
    if (cpuUsageFraction()) {
        meters << Gauge {
            .value = AUI_REACT(*state->usage),
            .min = 0.f,
            .max = 100.f,
            .yellowFrom = 60.f,
            .redFrom = 85.f,
            .maroonFrom = 100.f,
            .title = "CPU",
            .unit = "% LOAD",
            .majorTicks = 10,
        };
    }
    meters << Gauge {
        .value = AUI_REACT(*state->tflops),
        .min = 0.f,
        .max = TFLOPS_MAX,
        .title = "FP32",
        .unit = "TFLOPS",
        .precision = 2,
        .scalePrecision = 1,
        .majorTicks = 6,
        .markers = referenceCpus(),
    };

    const unsigned coreCount = ui::CpuStressTest::maxThreads();   // the checkboxes go from the highest core to CPU 0

    window->setContents(Vertical {
#if AUI_DEBUG
      Centered { Label { "Debug build - results might be inaccurate" } AUI_OVERRIDE_STYLE {
        TextColor(AColor::RED),
      } },
#endif

      Horizontal::Expanding {
        Horizontal { std::move(meters) } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 8_dp } },
        // left: load (the main control, so it is wider and has a ladder of ticks), right: cores with a checkbox per core
        Horizontal {
          Vertical {
            captioned(
                "Load",
                Horizontal {
                  verticalSlider(
                      AUI_REACT(*state->load), [state](aui::float_within_0_1 v) { state->setLoad(v); }, true),
                  loadTicks(AUI_REACT(*state->load), KNOB_INSET_DP),
                } AUI_OVERRIDE_STYLE { LayoutSpacing { 8_dp }, Expanding { 0, 1 } },
                AUI_REACT("LOAD: {}%"_format(int(std::lround(*state->load * 100.f))))),
          } AUI_OVERRIDE_STYLE { BackgroundSolid { COLOR_PANEL }, Border { 1_dp, COLOR_PANEL_BORDER }, BorderRadius { 10_dp }, Padding { 10_dp }, Expanding { 0, 1 } },
          Horizontal {
            captioned(
                "Cores",
                verticalSlider(
                    AUI_REACT(*state->coresFraction), [state](aui::float_within_0_1 v) { state->setCoresFraction(v); }),
                AUI_REACT("CORES: {}"_format(state->enabledCount()))),
            Centered {
              AUI_DECLARATIVE_FOR(i, ranges::views::iota(0u, coreCount) | ranges::views::reverse, AVerticalLayout) {
                  return CheckBox {
                      .checked = AUI_REACT(*state->enabledCores[i]),
                      .onCheckedChange =
                          [state, i](bool) {
                              const_cast<AProperty<bool>&>(state->enabledCores[i]) = !state->enabledCores[i];
                          },
                      .content = Label { "CPU {}"_format(i) } AUI_OVERRIDE_STYLE { FontSize { 9_pt } },
                  };
              } AUI_OVERRIDE_STYLE { LayoutSpacing { 2_dp } },
            },
          } AUI_OVERRIDE_STYLE { BackgroundSolid { COLOR_PANEL }, Border { 1_dp, COLOR_PANEL_BORDER }, BorderRadius { 10_dp }, Padding { 10_dp }, Expanding { 0, 1 }, LayoutSpacing { 12_dp } },
        } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 16_dp } },
      } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 16_dp } },
    } AUI_OVERRIDE_STYLE { BackgroundSolid { COLOR_WINDOW_BACKGROUND }, Padding { 16_dp }, LayoutSpacing { 8_dp } });
    window->show();
}
