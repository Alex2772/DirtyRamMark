#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#else
#include <unistd.h>
#endif

#include <AUI/Logging/ALogger.h>
#include <AUI/Util/kAUI.h>
#include "cli.h"
#include "cpu_frequency.h"
#include "cpu_power.h"
#include "cpu_stresstest.h"
#include "cpu_temperature.h"
#include "memory_info.h"
#include "memory_timings.h"
#include "system_info.h"

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

/// Value of "--name VALUE"; std::nullopt if there is no such option. Throws AException if the value is missing.
AOptional<AString> optionValue(const AStringVector& args, const AString& name) {
    const auto it = std::find(args.begin(), args.end(), name);
    if (it == args.end()) {
        return std::nullopt;
    }
    if (std::next(it) == args.end()) {
        throw AException("{} needs a value"_format(name));
    }
    return *std::next(it);
}

double numberOption(const AStringVector& args, const AString& name, double defaultValue, double min, double max) {
    const auto text = optionValue(args, name);
    if (!text) {
        return defaultValue;
    }
    const auto value = text->toDouble();
    if (!value || *value < min || *value > max) {
        throw AException("{} must be a number between {} and {}, got \"{}\""_format(name, min, max, *text));
    }
    return *value;
}

/// Burns the CPU for --duration seconds, printing the gauges once a second to stderr; the summary goes to stdout.
int runStressTest(const AStringVector& args) {
    unsigned threads = 0;
    double load = 0;
    double duration = 0;
    auto workload = ui::CpuStressTest::Workload::Fp32;
    try {
        if (const auto name = optionValue(args, "--workload")) {
            if (*name == "coremark") {
                workload = ui::CpuStressTest::Workload::CoreMark;
            } else if (*name != "fp32") {
                throw AException("--workload must be fp32 or coremark, got \"{}\""_format(*name));
            }
        }
        threads = unsigned(numberOption(args, "--cores", ui::CpuStressTest::maxThreads(), 1, ui::CpuStressTest::maxThreads()));
        load = numberOption(args, "--load", 100, 0, 100);
        duration = numberOption(args, "--duration", 10, 1, 24 * 60 * 60);
    } catch (const AException& e) {
        std::cerr << e.getMessage().toStdString() << std::endl;
        return 2;
    }

    ui::CpuStressTest test;
    const bool coreMark = workload == ui::CpuStressTest::Workload::CoreMark;
    test.setWorkload(workload);
    test.setThreads(threads);
    test.setLoad(float(load / 100.0));

    // TFLOPS for fp32, CoreMark iterations/s for coremark
    double sum = 0, maxTflops = 0;
    int samples = 0;
    AOptional<float> maxTemperature, maxFrequency, maxPower;
    cpuPowerWatts();   // the first call only takes the initial energy sample
    for (int second = 1; second <= int(duration); ++second) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const double tflops = coreMark ? test.sampleRate() : test.sampleTflops();
        const auto temperature = cpuTemperatureCelsius();
        const auto frequency = cpuFrequencyGHz();
        const auto power = cpuPowerWatts();
        if (second > 1) {   // the first second includes the warm-up
            sum += tflops;
            ++samples;
        }
        maxTflops = std::max(maxTflops, tflops);
        if (temperature) {
            maxTemperature = std::max(maxTemperature.valueOr(*temperature), *temperature);
        }
        if (power) {
            maxPower = std::max(maxPower.valueOr(*power), *power);
        }
        if (frequency) {
            maxFrequency = std::max(maxFrequency.valueOr(*frequency), *frequency);
        }
        std::cerr << "[{}/{}] {} threads, {}%: "_format(second, int(duration), threads, int(load)).toStdString()
                  << (coreMark ? "{:.0f} CoreMark/s"_format(tflops) : "{:.3f} TFLOPS"_format(tflops)).toStdString();
        if (frequency) {
            std::cerr << ", " << "{:.2f} GHz"_format(*frequency).toStdString();
        }
        if (temperature) {
            std::cerr << ", " << "{:.0f} C"_format(*temperature).toStdString();
        }
        if (power) {
            std::cerr << ", " << "{:.0f} W"_format(*power).toStdString();
        }
        std::cerr << std::endl;
    }

    AJson::Object result;
    result["threads"] = int(threads);
    result["load"] = load;
    result["duration"] = int(duration);
    result["workload"] = coreMark ? "coremark" : "fp32";
    result[coreMark ? "coremark_avg" : "fp32_tflops_avg"] = samples ? sum / samples : maxTflops;   // iterations/s for CoreMark
    result[coreMark ? "coremark_max" : "fp32_tflops_max"] = maxTflops;
    if (maxFrequency) {
        result["frequency_ghz_max"] = double(*maxFrequency);
    }
    if (maxPower) {
        result["power_w_max"] = double(*maxPower);
    }
    if (maxTemperature) {
        result["temperature_c_max"] = double(*maxTemperature);
    }
    std::cout.flush();
    std::cout << AJson::toString(result).toStdString() << std::endl;
    return 0;
}

}   // namespace

AJson::Object collectReport() {
    AJson::Object report;
    report["version"] = AString(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION));
    report["general"] = section("general", [] { return systemInfo(); });
    report["controller"] = memory_timings::describeController(memory_timings::readCpuId());
    report["memory"] = section("memory", [] { return memoryInfo(); });
    report["timings"] = section("timings", [] { return memoryTimings(); });
    return report;
}

AOptional<int> runCli(const AStringVector& args) {
    if (args.contains("--help") || args.contains("-h")) {
        std::cout << "Usage: dirty_ram_mark [--json | --stress-test [--workload fp32|coremark] [--cores N] [--load PERCENT] [--duration SECONDS]]\n"
                     "  --json   print general system info, memory info and actual DRAM timings as JSON to stdout and exit (no GUI).\n"
                     "           Never asks for authorization: the real timings and the memory modules info are reported as\n"
                     "           errors unless the process is already root (Linux) / administrator (Windows).\n"
                     "           Logs go to stderr.\n"
                     "  --stress-test   CPU stress test (no GUI). Prints the score (TFLOPS or CoreMark iterations/s), frequency, power and\n"
                     "           temperature every second to stderr and a JSON summary to stdout.\n"
                     "           --workload NAME fp32 (default) or coremark\n"
                     "           --cores N       number of threads (default: all logical processors)\n"
                     "           --load PERCENT  0..100 (default: 100)\n"
                     "           --duration SEC  (default: 10)\n";
        return 0;
    }
    if (args.contains("--stress-test")) {
        return runStressTest(args);
    }
    if (!args.contains("--json")) {
        return std::nullopt;
    }

    // The AUI logger writes to stdout (including its first-use "Log file:" line). While collecting, point stdout at
    // stderr so the logger can't pollute it, then restore it and print the report with std::cout.
    std::cout.flush();
    const int realStdout = ::dup(1);
    ::dup2(2, 1);
    const auto report = AJson::toString(collectReport()).toStdString();
    std::cout.flush();
    ::dup2(realStdout, 1);
    ::close(realStdout);

    std::cout << report << std::endl;
    return 0;
}
