#include <AUI/Platform/AWindow.h>
#include <AUI/Util/UIBuildingHelpers.h>

#include "memory_benchmark.h"

using namespace declarative;

void ui::openMemoryBenchmarkWindow(AWindow& parent) {
    auto window = _new<AWindow>(AWindow::Init {
        .title = "Memory Benchmark",
        .width = 300_dp,
        .height = 200_dp,
        .parent = AWindow::Init::Parent { .window = parent, .modal = true },
        .resizable = false,
    });
    window->setContents(Vertical { memoryBenchmark() });
    window->show();
}
