#include <algorithm>
#include <cmath>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/ASS/Property/BorderRadius.h>
#include <AUI/ASS/Property/FixedSize.h>
#include <AUI/ASS/Property/FontSize.h>
#include <AUI/ASS/Property/Padding.h>
#include <AUI/ASS/Property/TextColor.h>
#include <AUI/Common/ATimer.h>
#include <AUI/Platform/AWindow.h>
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
    explicit VerticalSliderHandleWrapper(_<AView> handle) : mHandle(std::move(handle)) { addView(mHandle); }

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
        mHandle->layout({ 0, size.y * (1.f - mValue) - handleSize.y / 2.f }, { size.x, handleSize.y });
    }

private:
    float valueAt(float y) const { return std::clamp(1.f - y / float(getSize().y), 0.f, 1.f); }

    _<AView> mHandle;
    aui::float_within_0_1 mValue = 0;
};

/// Vertical slider: a thin track with a handle; `width` is the width of the handle (and of the whole control).
_<AView> verticalSlider(
    contract::In<aui::float_within_0_1> value, contract::Slot<aui::float_within_0_1> onChanged, AMetric width) {
    auto handle = _new<AView>() AUI_OVERRIDE_STYLE {
        BackgroundSolid { AStylesheet::getOsThemeColor() },
        FixedSize { {}, 12_dp },
        BorderRadius { 4_dp },
        ACursor::NS_RESIZE,
    };
    auto track = _new<AView>() AUI_OVERRIDE_STYLE {
        BackgroundSolid { AColor::GRAY },
        FixedSize { 4_dp, {} },
        MinSize { {}, 150_dp },
        Expanding { 0, 1 },
    };
    auto wrapper = _new<VerticalSliderHandleWrapper>(std::move(handle));
    value.bindToCopy(ASlotDef { AUI_SLOT(wrapper.get())::setValue });
    onChanged.bindTo(wrapper->valueChanged);
    return Centered {
        Stacked {
          std::move(track),
          std::move(wrapper) AUI_OVERRIDE_STYLE { Expanding {} },
        } AUI_OVERRIDE_STYLE { FixedSize { width, {} }, Expanding { 0, 1 } },
    } AUI_OVERRIDE_STYLE { Expanding { 0, 1 } };
}

_<AView> captioned(const AString& caption, _<AView> slider, contract::In<AString> readout) {
    return Vertical {
        Label { caption } AUI_OVERRIDE_STYLE { FontSize { 12_pt }, ATextAlign::CENTER },
        std::move(slider) AUI_OVERRIDE_STYLE { Expanding { 0, 1 } },
        Label { std::move(readout) } AUI_OVERRIDE_STYLE { ATextAlign::CENTER },
    } AUI_OVERRIDE_STYLE { LayoutSpacing { 4_dp }, Expanding { 0, 1 } };
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

    const unsigned coreCount = ui::CpuStressTest::maxThreads();   // the checkboxes go from the highest core to CPU 0

    window->setContents(Vertical {
#if AUI_DEBUG
      Centered { Label { "Debug build - results might be inaccurate" } AUI_OVERRIDE_STYLE {
        TextColor(AColor::RED),
      } },
#endif

      Horizontal {
        Horizontal { std::move(meters) } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 32_dp } },
        // left: load (the "heavier" control, so a bit wider), right: cores with a checkbox per core
        Centered { Horizontal {
          captioned(
              "Load",
              verticalSlider(
                  AUI_REACT(*state->load), [state](aui::float_within_0_1 v) { state->setLoad(v); }, 48_dp),
              AUI_REACT("{}%"_format(int(std::lround(*state->load * 100.f))))),
          captioned(
              "Cores",
              Horizontal {
                verticalSlider(
                    AUI_REACT(*state->coresFraction), [state](aui::float_within_0_1 v) { state->setCoresFraction(v); },
                    32_dp),
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
              } AUI_OVERRIDE_STYLE { Expanding { 0, 1 }, LayoutSpacing { 4_dp } },
              AUI_REACT("{}"_format(state->enabledCount()))),
        } } } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 16_dp } },
    } AUI_OVERRIDE_STYLE { Padding { 16_dp }, LayoutSpacing { 8_dp } });
    window->show();
}
