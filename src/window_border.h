#pragma once

#include <AUI/Platform/AWindow.h>

namespace window_border {

/// Makes a window with DecorationExtended get the 1px border DWM draws around windows.
///
/// Windows 11 draws it by itself. On Windows 10 AUI makes the client area cover the whole window, so DWM keeps the
/// shadow but has no non-client area to draw the border in; this leaves a 1px non-client strip for it.
/// Does nothing on Windows 11 and other platforms. Must be called after AWindow::show().
void enable(AWindow& window);

}   // namespace window_border
