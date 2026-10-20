#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace ui {

/**
 * @brief CPU stress test: worker threads executing a selectable benchmark kernel.
 * @details
 * Intended to load the cores to 100% and give a reference point that can be compared between platforms.
 *
 * Workloads (see Workload): an fp32 multiply-add kernel (peak SIMD throughput), and EEMBC CoreMark (integer code with
 * branches and pointer chasing; every worker runs its own copy, the score is the sum, like CoreMark's MULTITHREAD=N).
 *
 * The fp32 kernel is portable code (vector extensions, no intrinsics). On x86-64 an AVX2+FMA version is picked at run time when the CPU
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
    enum class Workload {
        /// fp32 multiply-add, the rate is in FLOP/s.
        Fp32,
        /// EEMBC CoreMark 1.0, the rate is in iterations/s, i.e. the CoreMark score (not an official one: no 10 s rule).
        CoreMark,
    };
    static constexpr size_t WORKLOADS = 2;

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

    /// Selects what the workers execute. Takes effect within one duty cycle period.
    void setWorkload(Workload workload);
    Workload workload() const { return mWorkload; }

    /**
     * @brief Throughput of the selected workload since the previous call, per second: FLOP/s for Workload::Fp32, CoreMark
     * iterations/s for Workload::CoreMark.
     * @details The first call after construction or setWorkload() measures since then. Call it from one thread.
     */
    double sampleRate();

    /// sampleRate() in TFLOPS (1 FMA = 2 FLOP); meaningful for Workload::Fp32.
    double sampleTflops() { return sampleRate() / 1e12; }

private:
    struct alignas(64) Worker {
        std::atomic<uint64_t> done[WORKLOADS] = {};   // FLOP or CoreMark iterations, per Workload
        std::atomic<bool> enabled = false;
    };

    void run(size_t index);

    std::atomic<Workload> mWorkload = Workload::Fp32;
    std::atomic<float> mLoad = 0.f;
    std::atomic<bool> mStop = false;
    std::vector<std::unique_ptr<Worker>> mWorkers;
    std::vector<std::thread> mThreadsPool;

    uint64_t total(Workload workload) const;

    // sampling baseline, per workload: the one that is not selected must not accumulate an idle interval
    uint64_t mLastDone[WORKLOADS] = {};
    std::chrono::steady_clock::time_point mLastSample[WORKLOADS] = { std::chrono::steady_clock::now(), std::chrono::steady_clock::now() };
};

}   // namespace ui
