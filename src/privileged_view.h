#pragma once

#include <functional>

#include <AUI/View/AView.h>

#include "privileges.h"

namespace ui {

/// Builds a message with the "Upgrade privileges" button.
using UpgradePrompt = std::function<_<AView>(const AString& reason)>;

/**
 * @brief A view that shows what `factory` made.
 * @details
 * If `factory` throws privileges::Required, an upgrade button is shown instead, and after the upgrade `factory` is
 * called again. `factory` may use the UpgradePrompt to embed the same button into its own contents.
 */
_<AView> privilegedView(std::function<_<AView>(const UpgradePrompt&)> factory);

/**
 * @brief The "Restart as administrator" button for windows that show sensors rather than a one-off query.
 * @details
 * Windows only (the sensors are read through the PawnIO driver, which needs an administrator): privileges::upgrade()
 * relaunches the app through UAC. Returns an empty view if the process is already elevated or on the other platforms,
 * so it can be put into a layout unconditionally.
 */
_<AView> upgradeButton();

}   // namespace ui
