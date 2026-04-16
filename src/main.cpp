#include <range/v3/all.hpp>

#include <AUI/Platform/Entry.h>
#include <AUI/Thread/AThread.h>
#include <AUI/Traits/values.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AGroupBox.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Platform/AWindow.h>
#include "AUI/Platform/ACursor.h"
#include "generic_key_value_cloud.h"

#include <memory_benchmark.h>
#include <memory_info.h>

using namespace ass;
using namespace declarative;


AUI_ENTRY {
    auto window = _new<AWindow>("DirtyRamMark {}"_format(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION)), 300_dp, 200_dp);
    window->setContents(
        Vertical {
        GroupBox {
            Label {"Memory Info"},
            generic_key_value_cloud::makeView(memoryInfo())
        },
        GroupBox {
            Label {"Memory Benchmark"},
            ui::memoryBenchmark(),
        }
        }
    );

    window->show();

    return 0;
};
