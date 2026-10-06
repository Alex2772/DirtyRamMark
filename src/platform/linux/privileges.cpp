#include <atomic>

#include <privileges.h>

namespace {
std::atomic_bool granted = false;
}

bool privileges::isGranted() { return granted; }

void privileges::grant() { granted = true; }

// pkexec authorizes every privileged invocation on its own, so there is nothing to do in advance.
void privileges::upgrade() { grant(); }
