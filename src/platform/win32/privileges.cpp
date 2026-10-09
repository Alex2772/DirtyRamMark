#include <windows.h>
#include <shellapi.h>

#include <cstdlib>

#include <privileges.h>

bool privileges::isGranted() {
    static const bool elevated = [] {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }
        TOKEN_ELEVATION elevation {};
        DWORD size = sizeof(elevation);
        const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        return ok && elevation.TokenIsElevated;
    }();
    return elevated;
}

void privileges::grant() {}

void privileges::upgrade() {
    if (isGranted()) {
        return;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // UAC prompt; the elevated instance takes over, this one quits.
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        throw AException("Unable to restart as administrator (the request was declined?)");
    }
    std::exit(0);
}
