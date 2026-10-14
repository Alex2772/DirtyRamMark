#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AScrollArea.h>
#include <AUI/View/AText.h>

#include "general_tab.h"
#include "generic_key_value_cloud.h"
#include "system_info.h"

using namespace ass;
using namespace declarative;

_<AView> ui::generalTab() {
    AJson::Object info;
    try {
        info = systemInfo();
    } catch (const AException& e) {
        ALogger::err("general") << "Failed: " << e;
        return Centered { AText::fromString("Query failed: {}"_format(e.getMessage())) };
    }
    return AScrollArea::Builder().withContents(generic_key_value_cloud::makeGrid(info)).build() AUI_OVERRIDE_STYLE {
        MinSize { 500_dp },
        Expanding { 0, 1 },
    };
}
