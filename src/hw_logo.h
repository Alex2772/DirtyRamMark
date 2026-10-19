#pragma once

#include <string>

/// Picks a logo from assets/img by the hardware / OS name (as reported by /etc/os-release, /proc/cpuinfo, lspci, etc.).
/// Pure string matching: knows nothing about the UI.
namespace hw_logo {

struct Logo {
    std::string asset;     ///< path relative to assets/img (e.g. "cpu/intel-core-i7-gen6.png"); empty if there is no suitable one
    std::string caption;   ///< optional
};

Logo os(const std::string& name);
Logo cpu(const std::string& name);
Logo gpu(const std::string& name);

}   // namespace hw_logo
