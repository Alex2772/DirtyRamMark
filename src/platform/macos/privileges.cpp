#include <privileges.h>

// Nothing on macOS is gated by privileges: either it's available to everyone or not available at all.
bool privileges::isGranted() { return true; }

void privileges::upgrade() {}
