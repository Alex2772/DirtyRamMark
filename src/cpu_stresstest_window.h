#pragma once

#include <AUI/Platform/AWindow.h>

namespace ui {

/// "CPU Stress Test" window: fp32 load generator with live TFLOPS and temperature gauges.
void openCpuStressTestWindow(AWindow& parent);

}   // namespace ui
