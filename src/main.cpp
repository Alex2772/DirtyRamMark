#include <range/v3/all.hpp>

#include <AUI/Platform/Entry.h>
#include <AUI/Thread/AThread.h>
#include <AUI/Traits/values.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AGroupBox.h>
#include <AUI/View/ATabView.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Platform/AWindow.h>
#include "AUI/Platform/ACursor.h"
#include "generic_key_value_cloud.h"
#include "memory_timings.h"
#include "cli.h"

#include <memory_benchmark.h>
#include <memory_info.h>
#include <AUI/View/AScrollArea.h>

using namespace ass;
using namespace declarative;

static AArc<AView> genericScrollable(AArc<AView> contents) {
    return AScrollArea::Builder().withContents(std::move(contents)).withExpanding().build() AUI_OVERRIDE_STYLE { MinSize { 500_dp } };
}

static AArc<AView> safeQuery(aui::factory<AArc<AView>> auto && factory) {
    try {
        return factory();
    } catch (const AException& e) {
        ALogger::err("safeQuery") << "Failed: " << e;
        return Centered { Label { "Query failed: {}"_format(e.getMessage()) } };
    }
}

AUI_ENTRY {
    if (auto exitCode = runCli(args)) {
        return *exitCode;
    }

    auto window = _new<AWindow>("DirtyRamMark {}"_format(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION)), 300_dp, 200_dp);
    auto tabs = _new<ATabView>() AUI_OVERRIDE_STYLE { Expanding{} };
    tabs->addTab(ui::memoryBenchmark(), "Benchmark");
    tabs->addTab(genericScrollable(safeQuery([] { return generic_key_value_cloud::makeFlex(memoryInfo()); })), "Banks");
    tabs->addTab(genericScrollable(safeQuery([] {
                     AJson::Object entries;
                     entries["Memory Controller"] = memory_timings::describeController(memory_timings::readCpuId());
                     for (auto& [name, timings] : memoryTimings()) {
                         entries[name] = std::move(timings);
                     }
                     return generic_key_value_cloud::makeGrid(entries);
                 })),
                 "Timings");
    window->setContents(Vertical { tabs });

    window->show();

    return 0;
};
