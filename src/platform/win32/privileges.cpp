#include <cstdlib>

#include <windows.h>
#include <shellapi.h>

#include <AUI/Common/AException.h>
#include <AUI/Logging/ALogger.h>
#include <privileges.h>

namespace {

constexpr auto LOG_TAG = "privileges";

/// Quotes an argument the way CommandLineToArgvW expects it.
std::wstring quote(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
        } else {
            result.append(backslashes, L'\\');
        }
        backslashes = 0;
        result += c;
    }
    result.append(backslashes * 2, L'\\');   // backslashes before the closing quote
    result += L'"';
    return result;
}

/// Arguments of this process (without the executable) as a command line.
std::wstring ownArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return {};
    }
    std::wstring result;
    for (int i = 1; i < argc; ++i) {
        if (!result.empty()) {
            result += L' ';
        }
        result += quote(argv[i]);
    }
    LocalFree(argv);
    return result;
}

}   // namespace

bool privileges::isGranted() {
    static const bool elevated = [] {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            ALogger::warn(LOG_TAG) << "OpenProcessToken failed: " << GetLastError();
            return false;
        }
        TOKEN_ELEVATION elevation {};
        DWORD size = 0;
        const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        if (!ok) {
            ALogger::warn(LOG_TAG) << "GetTokenInformation failed: " << GetLastError();
            return false;
        }
        ALogger::info(LOG_TAG) << "Process is " << (elevation.TokenIsElevated ? "elevated (administrator)"
                                                                               : "NOT elevated (not administrator)");
        return elevation.TokenIsElevated != 0;
    }();
    return elevated;
}

AString privileges::upgradeButtonText() { return "Restart as administrator"; }

void privileges::upgrade() {
    if (isGranted()) {
        return;
    }
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        throw AException("Unable to determine the path of the executable (error {})"_format(GetLastError()));
    }
    wchar_t directory[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, directory);
    const auto arguments = ownArguments();

    SHELLEXECUTEINFOW info { .cbSize = sizeof(info) };
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";   // triggers UAC
    info.lpFile = exe;
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.lpDirectory = directory;
    info.nShow = SW_SHOWNORMAL;

    ALogger::info(LOG_TAG) << "Restarting as administrator";
    if (!ShellExecuteExW(&info)) {
        const auto error = GetLastError();
        if (error == ERROR_CANCELLED) {
            ALogger::warn(LOG_TAG) << "The user declined the UAC prompt";
            throw AException("Administrator rights were declined");
        }
        ALogger::err(LOG_TAG) << "ShellExecuteEx(runas) failed: " << error;
        throw AException("Unable to restart as administrator (error {})"_format(error));
    }
    if (info.hProcess) {
        CloseHandle(info.hProcess);
    }
    // The elevated instance has taken over, this one must not stay around as a second window.
    ALogger::info(LOG_TAG) << "Elevated instance started, exiting";
    ExitProcess(0);
}
