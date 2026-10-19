#pragma once

#include <AUI/Json/AJson.h>
#include <AUI/View/AView.h>

namespace ui {

/// Horizontal strip of logos (OS, CPU, GPUs) built from the systemInfo() output.
/// @return nullptr if there is nothing to show
_<AView> logoStrip(const AJson::Object& info);

}   // namespace ui
