#include <atomic>
#include <unistd.h>

#include <privileges.h>

namespace {
std::atomic_bool granted = false;
}

bool privileges::isGranted() { return granted || geteuid() == 0; }

// pkexec authorizes every privileged invocation on its own, so there is nothing to do in advance.
void privileges::upgrade() { granted = true; }
