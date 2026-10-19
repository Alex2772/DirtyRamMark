#include <algorithm>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/ASS/Property/Padding.h>
#include <AUI/ASS/Property/Margin.h>
#include <AUI/ASS/Property/TextColor.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AText.h>
#include <AUI/View/AButton.h>
#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/ASS/Property/BorderRadius.h>
#include <AUI/ASS/Property/Border.h>
#include <AUI/ASS/Property/BoxShadow.h>
#include <AUI/ASS/Property/FontSize.h>

#include "memory_benchmark.h"
#include "showcase.h"
#include "logo_strip.h"
#include "cpu_stresstest_window.h"
#include "memory_banks_window.h"
#include "memory_timings_window.h"
#include "system_info.h"
#include "AUI/View/ASpacerFixed.h"

using namespace ass;
using namespace declarative;

namespace {

/// declarative::Button without the borders and shadows: just a translucent background.
struct FlatButton {
    _<AView> content;
    contract::Slot<> onClick;

    _<AButton> operator()() {
        auto button = _new<AButton>();
        onClick.bindTo(button->clicked);
        button->setContents(Centered { std::move(content) });
        button AUI_OVERRIDE_STYLE {
            BackgroundSolid { AColor(0xffffff_rgb).transparentize(0.7f) },
            Border { nullptr },
            BoxShadow { nullptr },
            BorderRadius { 6_dp },
            Padding { 2_dp, 6_dp },
        };
        return button;
    }
};

struct Section {
    AString title;
    AVector<std::pair<AString, AString>> rows;
};

/// Distributes the flat systemInfo() into sections; what doesn't fit anywhere is not shown (it's a showcase).
AVector<Section> makeSections(const AJson::Object& info) {
    const auto value = [&](const AString& key) -> AOptional<AString> {
        for (const auto& [k, v] : info) {
            if (k == key) {
                return v.asStringOpt();
            }
        }
        return std::nullopt;
    };
    const auto add = [&](Section& section, const AString& label, const AString& key) {
        if (auto v = value(key)) {
            section.rows << std::pair { label, *v };
        }
    };

    AVector<Section> result;
    Section os { "OS" };
    add(os, "OS", "OS");
    add(os, "Kernel", "Kernel");
    add(os, "Uptime", "Uptime");
    add(os, "Shell", "Shell");
    result << std::move(os);

    Section desktop { "Desktop" };
    add(desktop, "Desktop", "Desktop");
    add(desktop, "WM", "WM");
    add(desktop, "WM Theme", "WM Theme");
    add(desktop, "Font", "Font");
    result << std::move(desktop);

    Section cpu { "CPU" };
    add(cpu, "CPU", "CPU");
    add(cpu, "Board", "Board");
    result << std::move(cpu);

    Section memory { "Memory" };
    add(memory, "RAM", "Memory");
    add(memory, "Swap", "Swap");
    result << std::move(memory);

    Section gpus { "GPU" };
    Section displays { "Display" };
    Section disks { "Disk" };
    // AJson::Object is unordered
    std::vector<AString> keys;
    for (const auto& [key, v] : info) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        if (key == "GPU" || key.startsWith("GPU ")) {
            add(gpus, key, key);
        } else if (key.startsWith("Display ")) {
            add(displays, "Display", key);
        } else if (key.startsWith("Disk ")) {
            // "Disk 01 (/)" -> "Disk (/)"
            add(disks, "Disk" + key.substr(key.find('(') - 1), key);
        }
    }
    result << std::move(gpus) << std::move(displays) << std::move(disks);

    result.removeIf([](const Section& s) { return s.rows.empty(); });
    return result;
}

_<AView> sectionView(const Section& section, AVector<_<AView>> actions = {}) {
    AVector<_<AView>> rows;
    for (const auto& [label, text] : section.rows) {
        rows << (Horizontal {
            Label { label } AUI_OVERRIDE_STYLE {
                FixedSize { 100_dp, {} },
                TextColor { AColor::BLACK.transparentize(0.4f) },
                ATextAlign::RIGHT,
            },
            AText::fromString(text) AUI_LET { it->setExpanding(); },
        } AUI_OVERRIDE_STYLE { LayoutSpacing { 12_dp } });
    }
    if (!actions.empty()) {
        rows << (Horizontal { std::move(actions) } AUI_OVERRIDE_STYLE { LayoutSpacing { 6_dp }, Margin { 3_dp, 0, 0, 114_dp } });
    }
    return Vertical { std::move(rows) } AUI_OVERRIDE_STYLE { LayoutSpacing { 2_dp } };
}

}   // namespace

_<AViewContainer> ui::showcase(AWindow& window) {
    const auto action = [](const AString& title, std::function<void()> onClick) -> _<AView> {
        return FlatButton {
            .content = Label { title },
            .onClick = std::move(onClick),
        };
    };
    AVector<_<AView>> cpuActions {
        action("Stress Test...", [&window] { openCpuStressTestWindow(window); }),
    };
    AVector<_<AView>> memoryActions {
        action("Benchmark...", [&window] { openMemoryBenchmarkWindow(window); }),
        action("Banks...", [&window] { openMemoryBanksWindow(window); }),
#if AUI_PLATFORM_LINUX
        action("Timings...", [&window] { openMemoryTimingsWindow(window); }),
#endif
    };
    AJson::Object info;
    try {
        info = systemInfo();
    } catch (const AException& e) {
        ALogger::err("showcase") << "Failed: " << e;
        return Centered { AText::fromString("Query failed: {}"_format(e.getMessage())) };
    }
    AVector<_<AView>> sections;
    if (auto logos = ui::logoStrip(info)) {
        sections << std::move(logos);
    }
    for (const auto& section : makeSections(info)) {
        if (section.title == "CPU") {
            sections << sectionView(section, std::move(cpuActions));
        } else if (section.title == "Memory") {
            sections << sectionView(section, std::move(memoryActions));
        } else {
            sections << sectionView(section);
        }
    }

    return Vertical { std::move(sections) } AUI_OVERRIDE_STYLE {
        Padding { 16_dp, 24_dp },
        LayoutSpacing { 16_dp },
    };
}
