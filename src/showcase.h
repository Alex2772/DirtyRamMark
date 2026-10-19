#pragma once

#include <AUI/View/AView.h>
#include <AUI/Platform/AWindow.h>

namespace ui {

/// Main window contents: a compact card with the system summary (OS, CPU, memory, GPUs) that is nice to screenshot.
/// Built on top of systemInfo().
/// @param window the window the contents are going to be put in; the tools (benchmark, timings...) open modally over it
_<AViewContainer> showcase(AWindow& window);

}   // namespace ui
