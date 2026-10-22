#include <privileges.h>

// Nothing on macOS is gated by privileges: either it's available to everyone or not available at all.
bool privileges::isGranted() { return true; }

AString privileges::upgradeButtonText() { return "Upgrade privileges to obtain more info"; }

void privileges::upgrade() {}
