#include <cstdlib>
#include <fstream>

#include <AUI/Common/AByteBuffer.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Platform/AProcess.h>
#include <memory_timings.h>

using namespace memory_timings;

CpuId memory_timings::readCpuId() {
    // procfs files report zero size, so stream-to-buffer helpers would read nothing
    std::ifstream cpuinfoFile("/proc/cpuinfo");
    return parseCpuInfo(std::string(std::istreambuf_iterator<char>(cpuinfoFile), {}));
}


AJson::Object memoryTimings() {
    auto cpu = readCpuId();
    auto timings = [&] {
        if (cpu.vendor == "AuthenticAMD") {
            auto result = readAmdTimings(cpu);
            ALogger::info("MemoryTimings") << "AMD family " << cpu.family << " model " << cpu.model << ": "
                                           << AJson::toString(result);
            return result;
        }
        if (cpu.vendor == "GenuineIntel") {
            auto layout = intelLayoutOf(cpu.family, cpu.model);
            if (!layout) {
                throw AException("Unsupported Intel CPU: family {}, model {}"_format(cpu.family, cpu.model));
            }
            auto result = readIntelTimings(*layout);
            ALogger::info("MemoryTimings") << "Intel family " << cpu.family << " model " << cpu.model << ": "
                                           << AJson::toString(result);
            return result;
        }
        throw AException("Unsupported CPU family \"{}\""_format(cpu.vendor));
    }();

    auto result = [&] {
        AJson::Object result;
        for (auto& [channel, t] : timings) {
            result["Timings - {}"_format(channel)] = std::move(t);
        }
        return result;
    }();
    return result;
}

