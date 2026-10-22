#pragma once

#include <AUI/Platform/AWindow.h>

namespace ui {

/// "All Indicators" window (not modal): every sensor the platform can read, as a live table.
void openAllIndicatorsWindow(AWindow& parent);

}   // namespace ui
