#include "window_border.h"

#include <AUI/Logging/ALogger.h>

#include <windows.h>

namespace {

constexpr auto LOG_TAG = "window_border";

constexpr DWORD WINDOWS_11_BUILD = 22000;

DWORD windowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW info { sizeof(info) };
    if (!rtlGetVersion || rtlGetVersion(&info) != 0) {
        return 0;
    }
    return info.dwBuildNumber;
}

WNDPROC gOriginalProc = nullptr;

LRESULT CALLBACK borderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCALCSIZE && wParam == TRUE && !IsZoomed(hwnd)) {
        // AUI's handler leaves the whole window as the client area; take 1px away on each side for DWM's border
        auto result = CallWindowProcW(gOriginalProc, hwnd, message, wParam, lParam);
        auto& rect = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
        rect.left += 1;
        rect.right -= 1;
        rect.bottom -= 1;
        return result;
    }
    return CallWindowProcW(gOriginalProc, hwnd, message, wParam, lParam);
}

}   // namespace

void window_border::enable(AWindow& window) {
    if (gOriginalProc) {
        return;   // one window is supported
    }
    const auto build = windowsBuild();
    if (build == 0 || build >= WINDOWS_11_BUILD) {
        ALogger::info(LOG_TAG) << "skipped: build " << build << ", the system draws the border";
        return;
    }
    auto hwnd = reinterpret_cast<HWND>(window.nativeHandle());
    if (!hwnd) {
        ALogger::warn(LOG_TAG) << "skipped: window has no native handle (call after show()?)";
        return;
    }
    gOriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&borderProc)));
    SetWindowPos(
        hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    ALogger::info(LOG_TAG) << "done";
}
