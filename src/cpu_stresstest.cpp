#include "cpu_stresstest.h"

#include <algorithm>

extern "C" {
#include <coremark_kernel.h>
}

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#include <windows.h>
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>
#endif
#endif

using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

constexpr auto PERIOD = 50ms;               // duty cycle period
constexpr size_t LANES = 64;                // independent accumulators: hides the latency of the FMA chain
constexpr uint64_t ITERATIONS = 2048;       // per chunk: ~tens of microseconds, so the duty cycle is precise
constexpr uint64_t FLOP_PER_CHUNK = ITERATIONS * LANES * 2;   // multiply + add

std::atomic<float> sink = 0.f;              // keeps the result alive so the kernel isn't optimized out
std::atomic<float> factorA = 0.999999f;     // loaded at run time so the compiler can't fold the constants
std::atomic<float> factorB = 1e-7f;

#if defined(__GNUC__) || defined(__clang__)
// 8 floats per vector: the compiler lowers it to AVX2 (or 2x SSE2 / 2x NEON in the baseline version).
using Vec = float __attribute__((vector_size(32)));
constexpr size_t VECTORS = LANES / 8;

/// fp32 multiply-add over LANES independent accumulators.
[[gnu::always_inline]] inline void chunkImpl(float (&acc)[LANES]) {
    const float a = factorA.load(std::memory_order_relaxed);
    const float b = factorB.load(std::memory_order_relaxed);
    const Vec va = { a, a, a, a, a, a, a, a };
    const Vec vb = { b, b, b, b, b, b, b, b };
    Vec v[VECTORS];
    __builtin_memcpy(v, acc, sizeof(v));
    for (uint64_t i = 0; i < ITERATIONS; ++i) {
#pragma GCC unroll 8
        for (size_t k = 0; k < VECTORS; ++k) {
            v[k] = v[k] * va + vb;
        }
    }
    __builtin_memcpy(acc, v, sizeof(v));
}
#else
/// fp32 multiply-add over LANES independent accumulators.
inline void chunkImpl(float (&acc)[LANES]) {
    const float a = factorA.load(std::memory_order_relaxed);
    const float b = factorB.load(std::memory_order_relaxed);
    for (uint64_t i = 0; i < ITERATIONS; ++i) {
        for (size_t lane = 0; lane < LANES; ++lane) {
            acc[lane] = acc[lane] * a + b;
        }
    }
}
#endif

void chunkBaseline(float (&acc)[LANES]) { chunkImpl(acc); }

// On x86-64 the kernel is built twice and picked at run time by what the CPU supports: with AVX2+FMA (any CPU of the last
// ~10 years) and for the baseline ISA. Without it the number would be a SSE2 one, 4 times lower than the real peak.
// (target_clones("arch=haswell") is not an option: it matches the CPU model, so Zen would get the baseline version.)
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
[[gnu::target("avx2,fma")]] void chunkAvx2(float (&acc)[LANES]) { chunkImpl(acc); }

using Kernel = void (*)(float (&)[LANES]);
Kernel pickKernel() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") ? chunkAvx2 : chunkBaseline;
}
const Kernel chunk = pickKernel();
#elif defined(_MSC_VER) && defined(_M_X64)
// MSVC has no vector_size extension: AVX2+FMA intrinsics, which it allows in any function without a global /arch flag.
// 8 independent ymm accumulators cover the FMA latency (4 cycles x 2 ports).
void chunkAvx2(float (&acc)[LANES]) {
    const __m256 va = _mm256_set1_ps(factorA.load(std::memory_order_relaxed));
    const __m256 vb = _mm256_set1_ps(factorB.load(std::memory_order_relaxed));
    static_assert(LANES == 64);
    __m256 v0 = _mm256_loadu_ps(acc + 0), v1 = _mm256_loadu_ps(acc + 8), v2 = _mm256_loadu_ps(acc + 16),
           v3 = _mm256_loadu_ps(acc + 24), v4 = _mm256_loadu_ps(acc + 32), v5 = _mm256_loadu_ps(acc + 40),
           v6 = _mm256_loadu_ps(acc + 48), v7 = _mm256_loadu_ps(acc + 56);
    for (uint64_t i = 0; i < ITERATIONS; ++i) {
        v0 = _mm256_fmadd_ps(v0, va, vb);
        v1 = _mm256_fmadd_ps(v1, va, vb);
        v2 = _mm256_fmadd_ps(v2, va, vb);
        v3 = _mm256_fmadd_ps(v3, va, vb);
        v4 = _mm256_fmadd_ps(v4, va, vb);
        v5 = _mm256_fmadd_ps(v5, va, vb);
        v6 = _mm256_fmadd_ps(v6, va, vb);
        v7 = _mm256_fmadd_ps(v7, va, vb);
    }
    _mm256_storeu_ps(acc + 0, v0);
    _mm256_storeu_ps(acc + 8, v1);
    _mm256_storeu_ps(acc + 16, v2);
    _mm256_storeu_ps(acc + 24, v3);
    _mm256_storeu_ps(acc + 32, v4);
    _mm256_storeu_ps(acc + 40, v5);
    _mm256_storeu_ps(acc + 48, v6);
    _mm256_storeu_ps(acc + 56, v7);
}

bool supportsAvx2Fma() {
    int info[4];
    __cpuid(info, 1);
    const bool fma = info[2] & (1 << 12), osxsave = info[2] & (1 << 27), avx = info[2] & (1 << 28);
    if (!fma || !osxsave || !avx || (_xgetbv(0) & 6) != 6) {   // the OS must save the ymm state
        return false;
    }
    __cpuidex(info, 7, 0);
    return info[1] & (1 << 5);
}

using Kernel = void (*)(float (&)[LANES]);
const Kernel chunk = supportsAvx2Fma() ? chunkAvx2 : chunkBaseline;
#else
void chunk(float (&acc)[LANES]) { chunkBaseline(acc); }
#endif

/// Pins the calling thread to a logical processor. Best effort: a failure just leaves the thread floating.
void pinCurrentThreadToCpu(unsigned cpu) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#elif defined(_WIN32)
    // SetThreadAffinityMask covers only the current processor group (64 logical processors)
    if (cpu < 8 * sizeof(DWORD_PTR)) {
        SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << cpu);
    }
#else
    (void)cpu;   // macOS has no thread affinity API (only affinity tags, which are a scheduling hint)
#endif
}

}   // namespace

ui::CpuStressTest::CpuStressTest() {
    const auto count = maxThreads();
    for (unsigned i = 0; i < count; ++i) {
        mWorkers.push_back(std::make_unique<Worker>());
    }
    for (unsigned i = 0; i < count; ++i) {
        mThreadsPool.emplace_back([this, i] { run(i); });
    }
}

ui::CpuStressTest::~CpuStressTest() {
    mStop = true;
    for (auto& t : mThreadsPool) {
        t.join();
    }
}

unsigned ui::CpuStressTest::maxThreads() { return std::max(1u, std::thread::hardware_concurrency()); }

void ui::CpuStressTest::setThreads(unsigned threads) {
    for (size_t i = 0; i < mWorkers.size(); ++i) {
        mWorkers[i]->enabled = i < threads;
    }
}

void ui::CpuStressTest::setCoreEnabled(unsigned core, bool enabled) {
    if (core < mWorkers.size()) {
        mWorkers[core]->enabled = enabled;
    }
}

void ui::CpuStressTest::setLoad(float load) { mLoad = std::clamp(load, 0.f, 1.f); }

uint64_t ui::CpuStressTest::total(Workload workload) const {
    uint64_t result = 0;
    for (const auto& worker : mWorkers) {
        result += worker->done[size_t(workload)].load(std::memory_order_relaxed);
    }
    return result;
}

void ui::CpuStressTest::setWorkload(Workload workload) {
    const auto index = size_t(workload);
    mLastDone[index] = total(workload);
    mLastSample[index] = steady_clock::now();
    mWorkload = workload;
}

double ui::CpuStressTest::sampleRate() {
    const auto index = size_t(mWorkload.load());
    const auto done = total(Workload(index));
    const auto now = steady_clock::now();
    const double seconds = duration<double>(now - mLastSample[index]).count();
    const double result = seconds > 0 ? double(done - mLastDone[index]) / seconds : 0.0;
    mLastDone[index] = done;
    mLastSample[index] = now;
    return result;
}

void ui::CpuStressTest::run(size_t index) {
    pinCurrentThreadToCpu(unsigned(index));
    float acc[LANES];
    for (size_t i = 0; i < LANES; ++i) {
        acc[i] = float(i) * 0.01f;
    }
    struct CoreMarkDeleter {
        void operator()(coremark_context* c) const { coremark_destroy(c); }
    };
    std::unique_ptr<coremark_context, CoreMarkDeleter> coreMark(coremark_create());
    auto& fp32Done = mWorkers[index]->done[size_t(Workload::Fp32)];
    auto& coreMarkDone = mWorkers[index]->done[size_t(Workload::CoreMark)];
    while (!mStop) {
        const float load = mLoad;
        if (!mWorkers[index]->enabled || load <= 0.f) {
            std::this_thread::sleep_for(10ms);
            continue;
        }
        const auto workload = mWorkload.load();
        const auto begin = steady_clock::now();
        const auto busyUntil = begin + duration_cast<steady_clock::duration>(PERIOD * double(load));
        while (steady_clock::now() < busyUntil) {
            if (workload == Workload::CoreMark && coreMark) {
                coremark_run(coreMark.get(), 1);   // ~tens of microseconds
                coreMarkDone.store(coreMarkDone.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            } else {
                chunk(acc);
                fp32Done.store(fp32Done.load(std::memory_order_relaxed) + FLOP_PER_CHUNK, std::memory_order_relaxed);
            }
        }
        if (const auto rest = begin + PERIOD - steady_clock::now(); rest > 0ms && load < 1.f) {
            std::this_thread::sleep_for(rest);
        }
    }
    float sum = 0.f;
    for (float v : acc) {
        sum += v;
    }
    sink.store(sum, std::memory_order_relaxed);
}
