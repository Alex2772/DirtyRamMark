#pragma once

#include <AUI/Common/AException.h>
#include <AUI/Common/AString.h>

/**
 * @brief Some data (SMBIOS, memory controller registers on Linux) can't be read without elevated privileges.
 * @details
 * The app starts unprivileged and shows everything it can. Queries that need more throw privileges::Required, and the
 * UI offers an "Upgrade privileges" button which calls privileges::upgrade():
 *  - Linux: from now on queries are allowed to ask for authorization through pkexec;
 *  - Windows: the PawnIO driver (memory controller registers) needs an elevated process; upgrade() relaunches the app
 *    through UAC and exits this instance;
 *  - macOS: nothing is gated by privileges.
 */
namespace privileges {

/// Thrown by a query that needs privileges that the process doesn't have (yet).
class Required : public AException {
public:
    using AException::AException;
};

/// @return true if privileged queries are allowed right now.
bool isGranted();

/// Caption of the button that calls upgrade().
AString upgradeButtonText();

/// Obtains privileges for the UI. On Windows doesn't return on success: the process is replaced by an elevated one.
/// @throws AException if the user declined.
void upgrade();

}   // namespace privileges
