#include <algorithm>
#include <optional>
#include <cmath>
#include <deque>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/BackgroundImage.h>
#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/ASS/Property/Border.h>
#include <AUI/ASS/Property/BorderRadius.h>
#include <AUI/ASS/Property/FixedSize.h>
#include <AUI/ASS/Property/FontSize.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/ASS/Property/Padding.h>
#include <AUI/ASS/Property/TextColor.h>
#include <AUI/Common/ATimer.h>
#include <AUI/Thread/AThread.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Render/ABrush.h>
#include <AUI/Render/ARenderContext.h>
#include <AUI/Render/IRenderer.h>
#include <AUI/Render/RenderHints.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AForEachUI.h>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/reverse.hpp>
#include <AUI/Layout/AStackedLayout.h>
#include <AUI/Model/AListModel.h>
#include <AUI/View/AButton.h>
#include <AUI/View/ACheckBox.h>
#include <AUI/View/ADropdownList.h>
#include <AUI/View/AViewContainer.h>
#include <AUI/View/ASlider.h>

#include "all_indicators_window.h"
#include "gauge.h"
#include "plot.h"
#include "cpu_frequency.h"
#include "cpu_stresstest.h"
#include "cpu_stresstest_window.h"
#include "cpu_power.h"
#include "cpu_temperature.h"
#include "cpu_usage.h"
#include "system_info.h"

using namespace ass;
using namespace declarative;
using namespace std::chrono_literals;

namespace {

/// Workloads in the order of the dropdown.
constexpr ui::CpuStressTest::Workload WORKLOADS[] = {
    ui::CpuStressTest::Workload::CoreMark,
    ui::CpuStressTest::Workload::Fp32,
};
constexpr int WORKLOAD_COUNT = int(std::size(WORKLOADS));

/// CoreMark per logical processor, thousands of iterations/s, of a fast modern core (~50k at 5 GHz): sizes the gauge so it
/// ends a bit above what this machine can show. Other cores (and the other half of SMT) are slower.
constexpr float COREMARK_K_PER_THREAD = 50.f;

constexpr float TFLOPS_MAX = 4.f;   // fits a 64-core Threadripper (see referenceCpus())
constexpr float FREQUENCY_MAX_GHZ = 6.f;
constexpr float GAUGE_TEMPERATURE_MIN_CELSIUS = 30.f;   // gauge only
constexpr float COOLANT_MIN_CELSIUS = GAUGE_TEMPERATURE_MIN_CELSIUS;   // gauge only
constexpr float COOLANT_MAX_CELSIUS = 50.f;   // gauge only
constexpr float VRM_MAX_CELSIUS = 120.f;   // gauge only
constexpr float PUMP_MAX_RPM = 5000.f;   // gauge only: AIO pumps run at 2-3k, custom loop ones up to 5k
// shared by the CPU and coolant plot axes: same unit, so the same scale
constexpr float TEMPERATURE_MIN_CELSIUS = 10.f;
constexpr float TEMPERATURE_MAX_CELSIUS = 110.f;
constexpr float FIRE_FROM_CELSIUS = 85.f;   // the gauge starts to burn...
constexpr float FIRE_FULL_CELSIUS = 95.f;   // ...and burns the hardest here
constexpr float POWER_DEFAULT_MAX_WATTS = 200.f;   // for a CPU that is not in maxPowerWatts()
constexpr float THROTTLE_DEFAULT_CELSIUS = 95.f;   // for a CPU that is not in throttleCelsius()
constexpr std::chrono::seconds PLOT_SPAN = 3min;

/// Reference points for the TFLOPS gauge: theoretical peak of fp32 FMA (AVX2/NEON), all cores at a typical sustained
/// clock: cores x GHz x FLOP per cycle per core (32 for 2x256-bit FMA or an Apple P-core with 4x128-bit NEON FMA, 16
/// for a Gracemont/Apple E-core or 2x128-bit NEON). There is no authoritative table of these; they are computed, so
/// treat them as ballpark.
AVector<Gauge::Marker> referenceFp32Cpus() {
    return {
        { 0.15f, "Raspberry Pi 5" },   // 4 x 2.4 GHz x 16
        { 0.55f, "Apple M1" },         // 4 P x 3.2 x 32 + 4 E x 2.1 x 16
        { 0.8f, "i5-12400" },          // 6 x 4.0 x 32
        { 1.0f, "Apple M3 Pro" },      // 6 P x 4.05 x 32 + 6 E x 2.75 x 16
        { 1.2f, "Ryzen 7 7800X3D" },   // 8 x 4.7 x 32
        { 1.4f, "i7-12700K" },         // 8 P x 4.7 x 32 + 4 E x 3.6 x 16
        { 1.6f, "Apple M3 Max" },      // 12 P x 4.05 x 32 + 4 E x 2.75 x 16
        { 2.0f, "Ryzen 9 5950X" },     // 16 x 3.9 x 32 (measured by others: ~2.1)
        { 2.5f, "Ryzen 9 7950X" },     // 16 x 4.9 x 32
        // Threadrippers are power-limited under a full AVX2 load: the all-core clock is well below the boost one
        { 4.3f, "TR 7970X" },          // 32 x 4.2 x 32
    };
}

/// Model name of this CPU ("AMD Ryzen 9 5950X 16-Core Processor", "Apple M3 Pro"), lowercase; empty if unknown.
const std::string& cpuModelLower() {
    static const std::string name = [] {
        const auto info = systemInfo();
        const auto* cpu = info.contains("CPU");
        return cpu ? cpu->second.asStringOpt().valueOr(AString {}).lowercase().toStdString() : std::string {};
    }();
    return name;
}

bool containsAny(const std::string& text, std::initializer_list<const char*> needles) {
    return std::any_of(needles.begin(), needles.end(), [&](const char* needle) { return text.find(needle) != std::string::npos; });
}

/// Maximum package power of the CPU in watts, rounded: the end of the power gauge. Not what the CPU draws all the time: PPT
/// of Ryzens, PL2 ("maximum turbo power") of Intels, the peak measured under a full load for Apple (it has no official figure).
/// std::nullopt if the CPU is not here (the gauge then uses POWER_DEFAULT_MAX_WATTS). Sources:
///  - Ryzen 9 5950X / 5900X, Ryzen 7 5800X: PPT 142 W (105 W TDP), AM4 socket limit:
///    https://www.tomshardware.com/reviews/amd-ryzen-7-5800x-zen-3-review/3
///  - Ryzen 7 7800X3D and the other 120 W TDP Zen 4 X3D: PPT 162 W:
///    https://www.tomshardware.com/reviews/amd-ryzen-7-7800x3d-cpu-review
///  - Ryzen 9 7950X / 7900X / 9950X / 9900X: 170 W TDP, PPT 230 W:
///    https://www.hwcooling.net/en/ryzen-9-7950x-amds-elite-cpu-beats-but-also-doesnt-beat-core-i9-review/
///    https://wccftech.com/amd-ryzen-9-9950x-zen-5-cpu-tested-at-230w-power-limit-34-percent-faster-14900k/
///  - Threadripper: 350 W TDP (only a third-party PSU calculator lists it, not verified with AMD):
///    https://pc.nkbgaming.com/psu-calculator
///  - Core i7-12700K: maximum turbo power 190 W (Intel ARK):
///    https://www.intel.com/content/www/us/en/products/sku/134594/intel-core-i712700k-processor-25m-cache-up-to-5-00-ghz/specifications.html
///  - Core i9-12900K: 241 W, i9-13900K / i9-14900K: PL2 253 W:
///    https://www.cpubenchmark.net/cpu.php?cpu=Intel+Core+i9-12900K&id=4597
///    https://www.cpu-monkey.com/en/compare_cpu-amd_ryzen_9_5950x-vs-intel_core_i9_13900k
///  - Apple, measured: M1/M2/M3 about 20 W, M3 Pro just under 45 W, M3 Max CPU up to 56 W and 78 W of the whole chip:
///    https://www.notebookcheck.net/Apple-M3-SoC-analyzed-Increased-performance-and-improved-efficiency.766789.0.html
///    https://eclecticlight.co/2024/02/23/apple-silicon-2-power-and-thermal-glory/
///    https://www.notebookcheck.net/Apple-M3-Pro-M3-Max-analysis-Apple-has-significantly-upgraded-its-Max-CPU.782974.0.html
std::optional<float> maxPowerWatts(const std::string& cpu) {
    if (containsAny(cpu, { "apple" })) {
        if (containsAny(cpu, { "max", "ultra" })) return 80.f;
        if (containsAny(cpu, { "pro" })) return 50.f;
        return 25.f;
    }
    if (containsAny(cpu, { "threadripper" })) return 350.f;
    if (containsAny(cpu, { "ryzen 9 7950x", "ryzen 9 7900x", "ryzen 9 9950x", "ryzen 9 9900x" })) return 250.f;
    if (containsAny(cpu, { "x3d" })) return 170.f;
    if (containsAny(cpu, { "ryzen 9 5950x", "ryzen 9 5900x", "ryzen 7 5800x" })) return 150.f;
    if (containsAny(cpu, { "i9-12900k", "i9-13900k", "i9-14900k" })) return 250.f;
    if (containsAny(cpu, { "i7-12700k" })) return 200.f;
    return std::nullopt;
}

/// The gauges are tuned for this CPU (its power limit and throttling temperature are known).
bool isKnownCpu() {
    static const bool known = maxPowerWatts(cpuModelLower()).has_value();
    return known;
}

float powerMaxWatts() {
    static const float watts = maxPowerWatts(cpuModelLower()).value_or(POWER_DEFAULT_MAX_WATTS);
    return watts;
}

/// Temperature where the CPU starts to throttle (the gauge marker). Intels run into Tjmax by design, as Ryzens do. Apple
/// has no official figure: it comes from user reports. THROTTLE_DEFAULT_CELSIUS if the CPU is not here. Sources:
///  - Intel 12th-14th gen: Tjmax 100 C: https://hone.gg/blog/best-cpu-temperature-for-pc/
///  - Ryzen 7000/9000: Tjmax 95 C, X3D: 89 C; Ryzen 5000: 90 C (the sources disagree, 95 C for some models):
///    https://hone.gg/blog/best-cpu-temperature-for-pc/ https://mypcbottleneck.com/cpu-temperature-range/
///    https://www.propelrc.com/best-cpu-temperature-2/
///  - Apple M1/M2/M3: about 95-100 C, a fanless M1 Air started at 97-98 C, M2 Pro at about 100 C:
///    https://forums.macrumors.com/threads/m1-air-throttles-to-a-maximum-of-4w-during-a-game.2305984/
///    https://smith6612.me/2023/10/26/apple-m2-pro-still-thermal-throttling/
///    https://breezekit.app/en/guides/apple-silicon-temperature-guide
float throttleCelsius(const std::string& cpu) {
    if (containsAny(cpu, { "apple" }) || containsAny(cpu, { "intel" })) return 100.f;
    if (containsAny(cpu, { "x3d" })) return 89.f;
    if (containsAny(cpu, { "ryzen" })) {
        // Ryzen 5000 are "ryzen N 5xxx"
        for (const char* family : { "ryzen 9 5", "ryzen 7 5", "ryzen 5 5", "ryzen 3 5" }) {
            if (cpu.find(family) != std::string::npos) return 90.f;
        }
        return 95.f;
    }
    return THROTTLE_DEFAULT_CELSIUS;
}

/// Rounds up to 1, 2, 5 x 10^n.
float niceCeil(float value) {
    const float decade = std::pow(10.f, std::floor(std::log10(std::max(value, 1.f))));
    for (const float k : { 1.f, 2.f, 5.f, 10.f }) {
        if (value <= k * decade) {
            return k * decade;
        }
    }
    return 10.f * decade;
}

float coreMarkMaxK() { return niceCeil(COREMARK_K_PER_THREAD * float(ui::CpuStressTest::maxThreads())); }

/// Reference points for the CoreMark gauge, in kilo iterations/s, only those that fit the dial. These are measured ones, not
/// computed ones, all threads, CoreMark size 666 (the 2K performance run, as here):
///  - Ryzen 9 5950X: measured with the official EEMBC build (`make PORT_DIR=linux XCFLAGS="-DMULTITHREAD=32 -DUSE_PTHREAD
///    -pthread"`, gcc -O2, Linux): 628k, this kernel gives 646k;
///  - the rest: the Phoronix Test Suite "CoreMark" averages (OpenBenchmarking.org pts/coremark), read from search engine
///    excerpts of that page (it is behind a Cloudflare check). Community averages differ in clocks and builds, so treat
///    them as ballpark. The Zen 2 ones of that page were dropped: they came out higher than the measured Zen 3.
///  - i5-12400 (308k, average of 13 runs): https://openbenchmarking.org/result/2201180-PTS-ADLY476155
///  - i9-12900K (725k, a single result, 8 P + HT + 8 E, Ubuntu 21.10): https://openbenchmarking.org/result/2111052-TJ-COREI912997
AVector<Gauge::Marker> referenceCoreMarkCpus(float maxK) {
    const AVector<Gauge::Marker> all = {
        { 177.f, "Apple M1" },
        { 240.f, "Apple M3" },
        { 308.f, "i5-12400" },
        { 630.f, "Ryzen 9 5950X" },
        { 725.f, "i9-12900K" },   // 8 P + HT + 8 E
        { 990.f, "Ryzen 9 7950X" },
        { 1955.f, "TR 7970X" },
        { 3340.f, "TR 7980X" },
    };
    AVector<Gauge::Marker> result;
    for (const auto& marker : all) {
        if (marker.value <= maxK) {
            result << marker;
        }
    }
    return result;
}

struct State : AObject {
    ui::CpuStressTest test;
    AProperty<float> coresFraction = 1.f;   // slider position of "Cores"
    const AVector<AProperty<bool>> enabledCores = makeEnabledCores();
    AProperty<float> load = 0.f;   // 0..1
    AProperty<int> workload = 0;   // index in WORKLOADS
    AProperty<float> score = 0.f;   // of the selected workload: kilo CoreMark/s or TFLOPS
    AProperty<float> temperature = 0.f;
    AProperty<float> coolant = 0.f;   // liquid, °C
    AProperty<float> vrm = 0.f;   // °C
    AProperty<float> pump = 0.f;   // RPM
    AProperty<float> power = 0.f;   // watts
    AProperty<float> frequency = 0.f;
    AProperty<float> usage = 0.f;   // percent, all cores
    AProperty<AVector<declarative::Plot::Marker>> peaks;
    _<ATimer> timer = _new<ATimer>(100ms);

    State() {
        test.setWorkload(selectedWorkload());   // the test starts with fp32, the dropdown with whatever WORKLOADS[0] is
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

    ui::CpuStressTest::Workload selectedWorkload() const { return WORKLOADS[std::clamp(*workload, 0, WORKLOAD_COUNT - 1)]; }

    void setWorkload(int id) {
        workload = std::clamp(id, 0, WORKLOAD_COUNT - 1);
        test.setWorkload(selectedWorkload());
        score = 0.f;
    }

    void update() {
        const bool coreMark = selectedWorkload() == ui::CpuStressTest::Workload::CoreMark;
        score = float(test.sampleRate() / (coreMark ? 1e3 : 1e12));
        temperature = cpuTemperatureCelsius().valueOr(0.f);
        coolant = coolantTemperatureCelsius().valueOr(0.f);
        vrm = vrmTemperatureCelsius().valueOr(0.f);
        pump = pumpRpm().valueOr(0.f);
        power = cpuPowerWatts().valueOr(0.f);
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

/// `subheader` (optional) goes right under the caption.
_<AView> captioned(const AString& caption, _<AView> slider, contract::In<AString> readout, _<AView> subheader = nullptr) {
    if (!subheader) {
        subheader = _new<AView>();
        subheader->setVisibility(Visibility::GONE);
    }
    return Vertical {
        Label { caption } AUI_OVERRIDE_STYLE { FontSize { 12_pt }, ATextAlign::CENTER },
        std::move(subheader),
        std::move(slider) AUI_OVERRIDE_STYLE { Expanding { 0, 1 } },
        readoutPlate(std::move(readout)),
    } AUI_OVERRIDE_STYLE { LayoutSpacing { 6_dp }, Expanding { 0, 1 } };
}

/// Selector of what the workers execute; the parentheses say what the workload measures.
_<AView> workloadDropdown(const _<State>& state) {
    auto list = _new<ADropdownList>(AListModel<AString>::make({
        AString("CoreMark (integer)"),   // WORKLOADS[0]
        AString("FP32 (SIMD FMA)"),      // WORKLOADS[1]
    }));
    list->setSelectionId(*state->workload);
    AObject::connect(list->selectionChanged, state, [state](int id) { state->setWorkload(id); });
    return list;
}

/// A small triangle pointing down, centered in the view's width. Under the peak bubble it points at the peak.
class PointerDown : public AView {
public:
    explicit PointerDown(AColor color) : mColor(color) {}

    void render(ARenderContext ctx) override {
        const float height = float(getSize().y);
        const float centerX = float(getSize().x) / 2.f;
        const float halfWidth = height;   // 90 degrees at the tip
        // no polygon primitive: the triangle is a stack of one pixel high lines
        AVector<std::pair<glm::vec2, glm::vec2>> rows;
        for (float y = 0.5f; y < height; y += 1.f) {
            const float half = halfWidth * (1.f - y / height);
            rows.push_back({ { centerX - half, y }, { centerX + half, y } });
        }
        ctx.render.lines(ASolidBrush { mColor }, rows, ABorderStyle::Solid {}, AMetric(1.f, AMetric::T_PX));
    }

private:
    AColor mColor;
};

/// Caps the window at FRAME_RATE_LIMIT frames per second: every frame is drawn at most once per frame period, the rest
/// of it is slept away. An invisible 1x1 view, as the window itself is a plain AWindow.
/// Rendering a stress test at 144 FPS is the observer effect at its finest: the more often you look at the CPU load,
/// the more load you get to look at. At 30 FPS the CPU can at least pretend it is not being watched.
class FrameLimiter : public AView {
public:
    static constexpr int FRAME_RATE_LIMIT = 30;

    void render(ARenderContext ctx) override {
        constexpr auto period = std::chrono::microseconds(1'000'000 / FRAME_RATE_LIMIT);
        const auto elapsed = std::chrono::steady_clock::now() - mLastFrame;
        if (elapsed < period) {
            AThread::sleep(std::chrono::duration_cast<std::chrono::milliseconds>(period - elapsed));
        }
        mLastFrame = std::chrono::steady_clock::now();
    }

private:
    std::chrono::steady_clock::time_point mLastFrame = std::chrono::steady_clock::now();
};

/// The highest value of one plot axis over the visible time span. Plot knows nothing about peaks: the caller finds
/// them and pins a view (a bubble with the value and a pointer under it) to the point.
struct PeakTracker : AObject {
    using Clock = declarative::Plot::Clock;

    const size_t axis;
    const std::function<AString(float)> format;
    AProperty<AString> text;
    const _<AView> view;

    PeakTracker(size_t axis, AColor color, std::function<AString(float)> format)
      : axis(axis), format(std::move(format)),
        view(Vertical {
            Label { AUI_REACT(*text) } AUI_OVERRIDE_STYLE {
                FontSize { 8_pt },
                TextColor { AColor::WHITE },
                BackgroundSolid { color },
                BorderRadius { 4_dp },
                Padding { 2_dp, 5_dp },
            },
            _new<PointerDown>(color) AUI_OVERRIDE_STYLE { MinSize { 10_dp, 5_dp } },
        }) {}

    /// @return true if the peak has changed (a new maximum or the old one has left the visible span)
    bool add(Clock::time_point time, float value) {
        mHistory.push_back({ time, value });
        while (mHistory.front().first < time - PLOT_SPAN) {
            mHistory.pop_front();
        }
        const auto peak = *std::max_element(mHistory.begin(), mHistory.end(), [](const auto& a, const auto& b) { return a.second <= b.second; });
        if (peak == mPeak) {
            return false;
        }
        mPeak = peak;
        text = format(peak.second);
        return true;
    }

    declarative::Plot::Marker marker() const { return { mPeak.first, mPeak.second, axis, view }; }

private:
    std::deque<std::pair<Clock::time_point, float>> mHistory;
    std::pair<Clock::time_point, float> mPeak {};
};

}   // namespace

void ui::openCpuStressTestWindow(AWindow& parent) {
    auto window = _new<AWindow>(AWindow::Init {
      .title = "CPU Stress Test",
      .width = 960_dp,
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
            .min = GAUGE_TEMPERATURE_MIN_CELSIUS,
            .max = 110.f,
            .yellowFrom = 60.f,
            .redFrom = 80.f,
            .maroonFrom = 95.f,
            .title = "CPU",
            .unit = "\u00b0C",
            .majorTicks = 8,
            .fireFrom = FIRE_FROM_CELSIUS,
            .fireFull = FIRE_FULL_CELSIUS,
            .markers = { { throttleCelsius(cpuModelLower()), "THROTTLE" } },
        };
    }
    if (coolantTemperatureCelsius()) {
        meters << Gauge {
            .value = AUI_REACT(*state->coolant),
            .min = COOLANT_MIN_CELSIUS,
            .max = COOLANT_MAX_CELSIUS,
            .yellowFrom = 40.f,
            .redFrom = 45.f,
            .maroonFrom = 48.f,
            .title = "COOLANT",
            .unit = "\u00b0C",
            .precision = 1,
            .majorTicks = 4,
            .jitter = false,
        };
    }
    if (pumpRpm()) {
        meters << Gauge {
            .value = AUI_REACT(*state->pump),
            .min = 0.f,
            .max = PUMP_MAX_RPM,
            .yellowFrom = PUMP_MAX_RPM * 0.7f,   // a slow pump is a setting, not an emergency: the zones are about noise and wear
            .redFrom = PUMP_MAX_RPM * 0.85f,
            .maroonFrom = PUMP_MAX_RPM * 0.95f,
            .title = "PUMP",
            .unit = "x1000 RPM",
            .precision = 1,
            .scaleDivisor = 1000.f,
            .majorTicks = 5,
            .jitter = false,
        };
    }
    if (vrmTemperatureCelsius()) {
        meters << Gauge {
            .value = AUI_REACT(*state->vrm),
            .min = GAUGE_TEMPERATURE_MIN_CELSIUS,
            .max = VRM_MAX_CELSIUS,
            .yellowFrom = 80.f,
            .redFrom = 100.f,
            .maroonFrom = 110.f,
            .title = "VRM",
            .unit = "\u00b0C",
            .majorTicks = 9,
            .jitter = false,
        };
    }
    if (cpuPowerWatts()) {
        meters << Gauge {
            .value = AUI_REACT(*state->power),
            .min = 0.f,
            .max = powerMaxWatts(),
            .yellowFrom = powerMaxWatts() * 0.5f,
            .redFrom = powerMaxWatts() * 0.75f,
            .maroonFrom = powerMaxWatts() * 0.9f,
            .title = "CPU",
            .unit = "WATTS",
            .precision = 0,
            .scalePrecision = 0,
            .majorTicks = 10,
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
    // one gauge per workload, only the selected one is shown
    const auto workloadGauge = [&](ui::CpuStressTest::Workload workload, _<AView> gauge) {
        const auto visibility = [state, workload] { return state->selectedWorkload() == workload ? Visibility::VISIBLE : Visibility::GONE; };
        gauge->setVisibility(visibility());
        AObject::connect(state->workload.changed, gauge, [view = gauge.get(), visibility](int) { view->setVisibility(visibility()); });   // not the shared_ptr: it would own itself
        meters << std::move(gauge);
    };
    const float coreMarkMax = coreMarkMaxK();
    workloadGauge(ui::CpuStressTest::Workload::CoreMark, Gauge {
        .value = AUI_REACT(*state->score),
        .min = 0.f,
        .max = coreMarkMax,
        .yellowFrom = coreMarkMax,   // no danger zones: it is a score, not a limit
        .redFrom = coreMarkMax,
        .maroonFrom = coreMarkMax,
        .title = "CoreMark",
        .unit = "KILO ITER/S",
        .precision = 0,
        .scalePrecision = 0,
        .majorTicks = 5,
        .markers = referenceCoreMarkCpus(coreMarkMax),
    });
    workloadGauge(ui::CpuStressTest::Workload::Fp32, Gauge {
        .value = AUI_REACT(*state->score),
        .min = 0.f,
        .max = TFLOPS_MAX,
        .title = "FP32",
        .unit = "TFLOPS",
        .precision = 2,
        .scalePrecision = 0,
        .majorTicks = 8,
        .markers = referenceFp32Cpus(),
    });

    // the plot starts over when the workload changes: its history is in other units
    const auto buildPlot = [state, coreMarkMax]() -> _<AView> {
      // the plot shows every value that can be read here, each with its own axis
      AVector<declarative::Plot::YAxisScale> scales;
      AVector<const AProperty<float>*> sources;   // sources[i] is the value of scales[i]
      AVector<_<PeakTracker>> trackers;
      const auto addSeries = [&](const AProperty<float>& source, declarative::Plot::YAxisScale scale, bool trackPeak,
                                 std::function<AString(float)> peakText) {
          if (trackPeak) {
              trackers << _new<PeakTracker>(scales.size(), scale.color, std::move(peakText));
          }
          scales << std::move(scale);
          sources << &source;
      };

      if (cpuFrequencyGHz()) {
          addSeries(state->frequency, { 0x2f7fd9_rgb, "GHz", 0.f, FREQUENCY_MAX_GHZ }, true, [](float v) { return "{:.2f} GHz"_format(v); });
      }
      const bool hasCoolant = coolantTemperatureCelsius().hasValue();
      if (cpuTemperatureCelsius()) {
          addSeries(state->temperature, { 0xd93a2f_rgb, hasCoolant ? "\u00b0C CPU" : "\u00b0C", TEMPERATURE_MIN_CELSIUS, TEMPERATURE_MAX_CELSIUS }, true,
                    [hasCoolant](float v) { return hasCoolant ? "{:.0f}\u00b0C CPU"_format(v) : "{:.0f}\u00b0C"_format(v); });
      }
      if (hasCoolant) {
          addSeries(state->coolant, { 0x2fb8d9_rgb, "\u00b0C liquid", TEMPERATURE_MIN_CELSIUS, TEMPERATURE_MAX_CELSIUS }, true,
                    [](float v) { return "{:.1f}\u00b0C liquid"_format(v); });
      }
      // the same order as the gauges
      if (pumpRpm()) {
          addSeries(state->pump, { 0x7a8ba3_rgb, "RPM", 0.f, PUMP_MAX_RPM }, true, [](float v) { return "{:.0f} RPM"_format(v); });
      }
      if (vrmTemperatureCelsius()) {
          addSeries(state->vrm, { 0xd9437e_rgb, "\u00b0C VRM", 0.f, VRM_MAX_CELSIUS }, true,
                    [](float v) { return "{:.0f}\u00b0C VRM"_format(v); });
      }
      if (cpuPowerWatts()) {
          addSeries(state->power, { 0x8e44ad_rgb, "W", 0.f, powerMaxWatts() }, true,
                    [](float v) { return "{:.0f} W"_format(v); });
      }
      if (cpuUsageFraction()) {
          addSeries(state->usage, { 0xd98a1f_rgb, "%", 0.f, 100.f }, false, {});
      }
      if (state->selectedWorkload() == ui::CpuStressTest::Workload::CoreMark) {
          addSeries(state->score, { 0x1fa653_rgb, "kCoreMark", 0.f, coreMarkMax }, true,
                    [](float v) { return "{:.0f} kCoreMark"_format(v); });
      } else {
          addSeries(state->score, { 0x1fa653_rgb, "TFLOPS", 0.f, TFLOPS_MAX }, true,
                    [](float v) { return "{:.2f} TFLOPS"_format(v); });
      }

        return declarative::Plot {
            .pollInterval = 100ms,
            .yAxisScales = std::move(scales),
            .xAxisDistance = PLOT_SPAN,
            .readMeasurements =
                [state, sources = std::move(sources), trackers]() {
                    const auto now = declarative::Plot::Clock::now();
                    AVector<float> values;
                    for (const auto* source : sources) {
                        values << **source;
                    }
                    bool peaksChanged = false;
                    for (const auto& tracker : trackers) {
                        peaksChanged |= tracker->add(now, values[tracker->axis]);
                    }
                    if (peaksChanged) {
                        AVector<declarative::Plot::Marker> markers;
                        for (const auto& tracker : trackers) {
                            markers << tracker->marker();
                        }
                        state->peaks = std::move(markers);
                    }
                    return values;
                },
            .markers = AUI_REACT(*state->peaks),
        } AUI_OVERRIDE_STYLE { Expanding { 1, 2 } };
    };
    auto plotHolder = _new<AViewContainer>();
    plotHolder->setLayout(std::make_unique<AStackedLayout>());
    plotHolder->setExpanding({ 1, 2 });
    plotHolder->addView(buildPlot());
    AObject::connect(state->workload.changed, plotHolder, [state, holder = plotHolder.get(), buildPlot](int) {
        state->peaks = AVector<declarative::Plot::Marker> {};
        holder->removeAllViews();
        holder->addView(buildPlot());
    });

    _<AView> tunedNote = isKnownCpu() ? _<AView>(Centered { Label { "Gauges are tuned for your CPU" } AUI_OVERRIDE_STYLE {
                                          FontSize { 9_pt },
                                          TextColor { AColor::GRAY },
                                        } })
                                      : _new<AView>();   // nothing to say about an unknown CPU
    const unsigned coreCount = ui::CpuStressTest::maxThreads();   // the checkboxes go from the highest core to CPU 0

    window->setContents(Vertical {
      _new<FrameLimiter>() AUI_OVERRIDE_STYLE { FixedSize { 1_px, 1_px } },
#if AUI_DEBUG
      Centered { Label { "Debug build - results might be inaccurate" } AUI_OVERRIDE_STYLE {
        TextColor(AColor::RED),
      } },
#endif

      Horizontal::Expanding {
        Vertical {
          Centered {
            Horizontal { std::move(meters) } AUI_OVERRIDE_STYLE { LayoutSpacing { 8_dp } },
          },
          std::move(tunedNote),
          Centered {
            Button {
              .content = Label { "All Indicators..." },
              .onClick = [parent = window.get()] { ui::openAllIndicatorsWindow(*parent); },   // not the shared_ptr: it would own itself
            },
          },
          plotHolder,
        } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 8_dp } },
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
                AUI_REACT("LOAD: {}%"_format(int(std::lround(*state->load * 100.f)))),
                workloadDropdown(state)),
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
        } AUI_OVERRIDE_STYLE { LayoutSpacing { 16_dp } },
      } AUI_OVERRIDE_STYLE { Expanding { 1, 1 }, LayoutSpacing { 16_dp } },
    } AUI_OVERRIDE_STYLE { BackgroundSolid { COLOR_WINDOW_BACKGROUND }, Padding { 16_dp }, LayoutSpacing { 8_dp } });
    window->show();
}
