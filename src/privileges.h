#pragma once

#include <functional>

#include <AUI/Common/AException.h>

/**
 * @brief Some data (SMBIOS on Linux, memory controller registers) can't be read without elevated privileges.
 * @details
 * The app starts unprivileged and shows everything it can. Queries that need more throw privileges::Required, and the
 * UI offers an "Upgrade privileges" button which calls privileges::upgrade():
 *  - Linux: from now on queries are allowed to ask for authorization through pkexec;
 *  - Windows: the app is restarted elevated; the elevated instance deploys a temporary kernel driver and removes it
 *    on exit.
 */
namespace privileges {

/// Thrown by a query that needs privileges that the process doesn't have (yet).
class Required : public AException {
public:
    using AException::AException;
};

/**
 * @brief Thrown by a query that failed because of a system setting which the app can change on the user's behalf
 * (i.e., Windows test signing mode needed for the driver). The UI shows the message and a button that calls fix().
 */
class SettingRequired : public AException {
public:
    /// @param buttonLabel text of the button offered to the user
    /// @param fix performed when the user presses the button; may throw AException
    SettingRequired(AString message, AString buttonLabel, std::function<void()> fix)
      : AException(std::move(message)), mButtonLabel(std::move(buttonLabel)), mFix(std::move(fix)) {}

    [[nodiscard]] const AString& buttonLabel() const noexcept { return mButtonLabel; }
    [[nodiscard]] const std::function<void()>& fix() const noexcept { return mFix; }

private:
    AString mButtonLabel;
    std::function<void()> mFix;
};

/// @return true if privileged queries are allowed right now.
bool isGranted();

/// Allows privileged queries without leaving the process (CLI). On Windows only an elevated process is granted, so
/// this does nothing there.
void grant();

/// Obtains privileges for the UI. May restart the application (Windows), in which case it doesn't return.
/// @throws AException if the user declined.
void upgrade();

}   // namespace privileges
