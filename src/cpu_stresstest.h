#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace ui {

/**
 * @brief CPU stress test: worker threads executing an fp32 multiply-add kernel.
 * @details
 * Intended to load the cores to 100% and give a reference point that can be compared between platforms.
 * Portable code (vector extensions, no intrinsics). On x86-64 an AVX2+FMA version is picked at run time when the CPU
 * has it; otherwise (and on other architectures) the compiler lowers it to the baseline ISA (SSE2, NEON), so the number is
 * not the vendor's peak but is comparable between machines of the same kind.
 *
 * Worker i is pinned to logical processor i (where the platform allows it) and burns it while enabled.
 * Each worker runs a duty cycle: it burns the CPU for `load` of the period and sleeps the rest.
 *
 * Thread safe; the setters and `sampleTflops()` can be called from any thread.
 */
class CpuStressTest {
public:
    /// Starts the workers (idle until threads > 0 and load > 0).
    CpuStressTest();
    ~CpuStressTest();

    CpuStressTest(const CpuStressTest&) = delete;
    CpuStressTest& operator=(const CpuStressTest&) = delete;

    /// Number of logical processors, i.e. the upper bound of setThreads().
    static unsigned maxThreads();

    /// Enables the first `threads` workers (cores 0..threads-1) and disables the rest; clamped to [0, maxThreads()].
    void setThreads(unsigned threads);

    /// Enables or disables the worker pinned to the logical processor `core`. Out of range is ignored.
    void setCoreEnabled(unsigned core, bool enabled);

    /// Fraction of time the active workers are busy; clamped to [0, 1].
    void setLoad(float load);

    /**
     * @brief fp32 throughput since the previous call, in TFLOPS (1 FMA = 2 FLOP).
     * @details The first call measures since construction.
     */
    double sampleTflops();

private:
    struct alignas(64) Worker {
        std::atomic<uint64_t> flops = 0;
        std::atomic<bool> enabled = false;
    };

    void run(size_t index);

    std::atomic<float> mLoad = 0.f;
    std::atomic<bool> mStop = false;
    std::vector<std::unique_ptr<Worker>> mWorkers;
    std::vector<std::thread> mThreadsPool;

    uint64_t mLastFlops = 0;
    std::chrono::steady_clock::time_point mLastSample = std::chrono::steady_clock::now();
};

}   // namespace ui
