#include <algorithm>
#include <vector>
#include <windows.h>
#include <powrprof.h>
#include <cpu_frequency.h>
#include "pdh_counter.h"

namespace {

// PROCESSOR_POWER_INFORMATION is not exposed by every SDK header set; the layout is stable
struct ProcessorPowerInformation {
    ULONG number;
    ULONG maxMhz;
    ULONG currentMhz;
    ULONG mhzLimit;
    ULONG maxIdleState;
    ULONG currentIdleState;
};

std::vector<ProcessorPowerInformation> processorPowerInformation() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::vector<ProcessorPowerInformation> info(si.dwNumberOfProcessors);
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, info.data(),
                               ULONG(info.size() * sizeof(ProcessorPowerInformation))) != 0) {
        info.clear();
    }
    return info;
}

}   // namespace

std::vector<float> cpuFrequenciesGHz() {
    const auto info = processorPowerInformation();
    if (info.empty()) {
        return {};
    }

    // Task Manager's way: nominal frequency * "% Processor Performance" (goes above 100% when boosting).
    // The instances are "group,index" in the order of the processors, plus the totals.
    static PdhCounterArray performance(L"\\Processor Information(*)\\% Processor Performance");
    std::vector<float> result;
    for (const auto& v : performance.values()) {
        if (v.instance.find(L"_Total") != std::wstring::npos) {
            continue;
        }
        const size_t i = result.size();
        const ULONG maxMhz = i < info.size() ? info[i].maxMhz : info[0].maxMhz;
        result.push_back(float(double(maxMhz) * v.value / 100.0 / 1000.0));
    }
    if (std::any_of(result.begin(), result.end(), [](float f) { return f > 0.f; })) {
        return result;
    }

    // the counter needs two samples (first call) or is missing: whatever the power manager reports
    result.clear();
    for (const auto& p : info) {
        result.push_back(float(p.currentMhz) / 1000.f);
    }
    return result;
}

AOptional<float> cpuFrequencyGHz() {
    const auto frequencies = cpuFrequenciesGHz();
    const auto fastest = std::max_element(frequencies.begin(), frequencies.end());
    if (fastest == frequencies.end() || *fastest <= 0.f) {
        return std::nullopt;
    }
    return *fastest;
}
