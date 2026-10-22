#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AScrollArea.h>
#include <AUI/View/AText.h>

#include "generic_key_value_cloud.h"
#include "memory_timings.h"
#include "memory_timings_window.h"
#include "privileged_view.h"

using namespace ass;
using namespace declarative;

namespace {

_<AView> scrollable(_<AView> contents) {
    return AScrollArea::Builder()
        .withContents(std::move(contents))
        .build() AUI_OVERRIDE_STYLE {
            Expanding { 1, 1 },
        };
}


_<AView> timingsContents(const ui::UpgradePrompt& prompt) {
    AJson::Object entries;
    entries["Memory Controller"] = memory_timings::describeController(memory_timings::readCpuId());
    _<AView> footer;
    try {
        for (auto& [name, timings] : memoryTimings()) {
            entries[name] = std::move(timings);
        }
    } catch (const privileges::Required& e) {
        footer = prompt(e.getMessage());
    } catch (const AException& e) {
        ALogger::err("MemoryTimings") << "Unable to read memory timings: " << e;
        footer = Centered { AText::fromString(e.getMessage()) };
    }
    auto grid = scrollable(generic_key_value_cloud::makeGrid(entries));
    if (!footer) {
        return grid;
    }
    return Vertical { grid, footer };
}

}   // namespace

void ui::openMemoryTimingsWindow(AWindow& parent) {
    auto window = _new<AWindow>(AWindow::Init {
        .title = "Memory Timings",
        .width = 500_dp,
        .height = 600_dp,
        .parent = AWindow::Init::Parent { .window = parent, .modal = true },
        .resizable = false,
    });
    window->setContents(Vertical { privilegedView(timingsContents) });
    window->show();
}
