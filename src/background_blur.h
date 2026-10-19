#pragma once

#include <AUI/Platform/AWindow.h>

namespace background_blur {

/// Asks the compositor to blur whatever is behind the window.
///
/// Linux: Wayland only (GTK4/libadwaita backend of AUI): `ext-background-effect-v1`, falling back to
/// `org_kde_kwin_blur`. The window's own GTK background is made transparent.
/// Windows: DWM blur behind (`SetWindowCompositionAttribute`), the alpha channel of the window's framebuffer is honored.
/// Must be called after AWindow::show().
///
/// On success the caller is expected to make the window contents translucent. Nothing is changed on failure, so the
/// window stays opaque.
///
/// @return true if the compositor blurs the window now
bool enable(AWindow& window);

}   // namespace background_blur
