#include <iostream>
#include <unistd.h>

#include <AUI/Logging/ALogger.h>
#include <AUI/Util/kAUI.h>
#include "cli.h"
#include "memory_info.h"
#include "memory_timings.h"

namespace {

AJson::Object section(const char* name, const std::function<AJson::Object()>& query) {
    try {
        return query();
    } catch (const AException& e) {
        std::cerr << "Failed to query " << name << ": " << e.getMessage().toStdString() << std::endl;
        AJson::Object error;
        error["error"] = e.getMessage();
        return error;
    }
}

}   // namespace

AJson::Object collectReport() {
    AJson::Object report;
    report["version"] = AString(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION));
    report["controller"] = memory_timings::describeController(memory_timings::readCpuId());
    report["memory"] = section("memory", [] { return memoryInfo(); });
    report["timings"] = section("timings", [] { return memoryTimings(); });
    return report;
}

std::optional<int> runCli(const AStringVector& args) {
    if (args.contains("--help") || args.contains("-h")) {
        std::cout << "Usage: dirty_ram_mark [--json]\n"
                     "  --json   print memory info and actual DRAM timings as JSON to stdout and exit (no GUI).\n"
                     "           Needs root for the real timings: pkexec is asked for authorization.\n"
                     "           Logs go to stderr.\n";
        return 0;
    }
    if (!args.contains("--json")) {
        return std::nullopt;
    }

    // The AUI logger writes to stdout (including its first-use "Log file:" line). While collecting, point stdout at
    // stderr so the logger can't pollute it, then restore it and print the report with std::cout.
    std::cout.flush();
    const int realStdout = ::dup(STDOUT_FILENO);
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
    const auto report = AJson::toString(collectReport()).toStdString();
    std::cout.flush();
    ::dup2(realStdout, STDOUT_FILENO);
    ::close(realStdout);

    std::cout << report << std::endl;
    return 0;
}
