#pragma once

#include <AUI/Platform/AWindow.h>

namespace background_blur {

/// Asks the compositor to blur whatever is behind the window.
///
/// Wayland only (GTK4/libadwaita backend of AUI): `ext-background-effect-v1`, falling back to `org_kde_kwin_blur`.
/// Must be called after AWindow::show().
///
/// On success the window's own GTK background is made transparent, so the caller is expected to make its contents
/// translucent. Nothing is changed on failure, so the window stays opaque.
///
/// @return true if the compositor blurs the window now
bool enable(AWindow& window);

}   // namespace background_blur
