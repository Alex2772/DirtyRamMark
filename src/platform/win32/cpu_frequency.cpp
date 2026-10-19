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

AOptional<float> cpuFrequencyGHz() {
    const auto info = processorPowerInformation();
    if (info.empty()) {
        return std::nullopt;
    }

    // Task Manager's way: nominal frequency * "% Processor Performance" (goes above 100% when boosting).
    static PdhCounterArray performance(L"\\Processor Information(*)\\% Processor Performance");
    double fastestPerformance = 0.0;
    for (const auto& v : performance.values()) {
        if (v.instance.find(L"_Total") == std::wstring::npos) {
            fastestPerformance = std::max(fastestPerformance, v.value);
        }
    }
    if (fastestPerformance > 0.0) {
        ULONG maxMhz = 0;
        for (const auto& p : info) {
            maxMhz = std::max(maxMhz, p.maxMhz);
        }
        return float(double(maxMhz) * fastestPerformance / 100.0 / 1000.0);
    }

    // the counter needs two samples (first call) or is missing: whatever the power manager reports
    ULONG currentMhz = 0;
    for (const auto& p : info) {
        currentMhz = std::max(currentMhz, p.currentMhz);
    }
    if (currentMhz == 0) {
        return std::nullopt;
    }
    return float(currentMhz) / 1000.f;
}
