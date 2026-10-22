#include "background_blur.h"

#include <AUI/Logging/ALogger.h>

#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>

namespace {

constexpr auto LOG_TAG = "background_blur";

// undocumented, but stable since Windows 10 (used by Windows Terminal's predecessors, Explorer tweaks, etc.)
enum AccentState {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,   // Windows 10 1803+; much stronger blur than BLURBEHIND
};

struct AccentPolicy {
    int accentState;
    int accentFlags;
    unsigned gradientColor;
    int animationId;
};

constexpr int WCA_ACCENT_POLICY = 19;

struct WindowCompositionAttributeData {
    int attribute;
    void* data;
    unsigned long dataSize;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);

bool gBlurApplied = false;

/// A GL framebuffer is composed with its alpha channel only if the window has the DWM "blur behind" enabled. An empty
/// blur region turns the alpha on without any blur of its own.
bool enableAlphaComposition(HWND hwnd) {
    BOOL compositionEnabled = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&compositionEnabled)) || !compositionEnabled) {
        ALogger::info(LOG_TAG) << "skipped: DWM composition is disabled";
        return false;
    }
    DWM_BLURBEHIND bb {};
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.fEnable = TRUE;
    bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    const auto result = DwmEnableBlurBehindWindow(hwnd, &bb);
    DeleteObject(bb.hRgnBlur);
    if (FAILED(result)) {
        ALogger::warn(LOG_TAG) << "DwmEnableBlurBehindWindow failed: 0x" << std::hex << unsigned(result);
        return false;
    }
    return true;
}

SetWindowCompositionAttributeFn findSetWindowCompositionAttribute() {
    auto user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<SetWindowCompositionAttributeFn>(
                        GetProcAddress(user32, "SetWindowCompositionAttribute"))
                  : nullptr;
}

bool setAccent(HWND hwnd, AccentState state) {
    auto setWindowCompositionAttribute = findSetWindowCompositionAttribute();
    if (!setWindowCompositionAttribute) {
        return false;
    }
    // gradientColor is ABGR; the alpha must be non-zero for the acrylic to blur, a nearly transparent tint keeps the
    // look of the window content unchanged
    AccentPolicy policy { state, 0, state == ACCENT_ENABLE_ACRYLICBLURBEHIND ? 0x01000000u : 0u, 0 };
    WindowCompositionAttributeData data { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
    return setWindowCompositionAttribute(hwnd, &data);
}

bool gAcrylic = false;
bool gMoving = false;

void beginMove(HWND hwnd) {
    if (gMoving) {
        return;
    }
    gMoving = true;
    setAccent(hwnd, ACCENT_ENABLE_BLURBEHIND);
}

void endMove(HWND hwnd, const char* reason) {
    if (!gMoving) {
        return;
    }
    gMoving = false;
    ALogger::info(LOG_TAG) << "restoring acrylic (" << reason << ")";
    // DWM ignores a repeated accent change when it thinks nothing has changed, so go through the disabled state
    setAccent(hwnd, ACCENT_DISABLED);
    setAccent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

/// Acrylic is re-rendered by DWM on every step of the window move on Windows 10, which makes the cursor lag behind the
/// window. The cheaper plain blur is used while the window is being dragged or resized.
LRESULT CALLBACK blurSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (gAcrylic) {
        switch (message) {
            case WM_ENTERSIZEMOVE:
                beginMove(hwnd);
                break;
            case WM_EXITSIZEMOVE:
                endMove(hwnd, "WM_EXITSIZEMOVE");
                break;
            // the modal move loop of a custom (extended) decoration may not end with WM_EXITSIZEMOVE; any sign that
            // the mouse button is released is a reason to restore
            case WM_CAPTURECHANGED:
            case WM_LBUTTONUP:
            case WM_NCLBUTTONUP:
                endMove(hwnd, "button released");
                break;
            case WM_MOUSEMOVE:
            case WM_NCMOUSEMOVE:
                if (gMoving && !(GetKeyState(VK_LBUTTON) & 0x8000)) {
                    endMove(hwnd, "mouse move without button");
                }
                break;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

bool enableAccentBlur(HWND hwnd) {
    if (!findSetWindowCompositionAttribute()) {
        ALogger::warn(LOG_TAG) << "SetWindowCompositionAttribute not found";
        return false;
    }
    if (setAccent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND)) {
        gAcrylic = true;
        SetWindowSubclass(hwnd, &blurSubclassProc, 1, 0);
        return true;
    }
    ALogger::warn(LOG_TAG) << "acrylic failed (" << GetLastError() << "), falling back to plain blur";
    if (!setAccent(hwnd, ACCENT_ENABLE_BLURBEHIND)) {
        ALogger::warn(LOG_TAG) << "SetWindowCompositionAttribute failed: " << GetLastError();
        return false;
    }
    return true;
}

}   // namespace

bool background_blur::enable(AWindow& window) {
    if (gBlurApplied) {
        ALogger::info(LOG_TAG) << "already applied";
        return true;   // one window is supported
    }
    auto hwnd = reinterpret_cast<HWND>(window.nativeHandle());
    if (!hwnd) {
        ALogger::warn(LOG_TAG) << "skipped: window has no native handle (call after show()?)";
        return false;
    }
    if (!enableAlphaComposition(hwnd)) {
        return false;
    }
    if (!enableAccentBlur(hwnd)) {
        // without blur the translucent window would be hard to read; take the alpha composition back
        DWM_BLURBEHIND bb {};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = FALSE;
        DwmEnableBlurBehindWindow(hwnd, &bb);
        return false;
    }
    gBlurApplied = true;
    ALogger::info(LOG_TAG) << "done";
    return true;
}
