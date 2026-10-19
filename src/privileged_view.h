#pragma once

#include <functional>

#include <AUI/View/AView.h>

#include "privileges.h"

namespace ui {

/// Builds a message with the "Upgrade privileges" button.
using UpgradePrompt = std::function<_<AView>(const AString& reason)>;

/// Message and a button that applies the fix of a privileges::SettingRequired.
_<AView> settingPrompt(const privileges::SettingRequired& e);

/**
 * @brief A view that shows what `factory` made.
 * @details
 * If `factory` throws privileges::Required, an upgrade button is shown instead, and after the upgrade `factory` is
 * called again. `factory` may use the UpgradePrompt to embed the same button into its own contents.
 */
_<AView> privilegedView(std::function<_<AView>(const UpgradePrompt&)> factory);

}   // namespace ui
