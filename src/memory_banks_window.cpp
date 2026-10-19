#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AScrollArea.h>

#include "generic_key_value_cloud.h"
#include "memory_banks_window.h"
#include "memory_info.h"
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

}   // namespace

void ui::openMemoryBanksWindow(AWindow& parent) {
    auto window = _new<AWindow>(AWindow::Init {
        .title = "Memory Banks",
        .width = 600_dp,
        .height = 600_dp,
        .parent = AWindow::Init::Parent { .window = parent, .modal = true },
        .resizable = false,
    });
    window->setContents(Vertical {
      privilegedView([](const UpgradePrompt&) { return scrollable(generic_key_value_cloud::makeFlex(memoryInfo())); }),
    });
    window->show();
    window->pack();
}
