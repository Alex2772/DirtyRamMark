#include <gtest/gtest.h>
#include <chrono>
#include <AUI/Thread/AThread.h>
#include <memory_benchmark.h>

using namespace ui;
using namespace std::chrono_literals;

TEST(MemoryBenchmark, RunAllTestsProducesResults) {
    // AThread::main() is bound lazily; make sure it is this thread before measure() calls it from a worker.
    ASSERT_EQ(AThread::main(), AThread::current());

    auto state = _new<State>();
    state->bufferSizeGB = 1;
    state->threads = 2;

    // measure() reports progress through the main thread's message queue, so pump it while the run is in flight.
    auto future = runAllTests(state);
    while (!future.hasResult()) {
        AThread::processMessages();
        std::this_thread::sleep_for(1ms);
    }
    *future; // rethrows if the run failed

    // the final reports of the last phases might still sit in the queue 
    auto hasAllBandwidths = [&] {
        return std::ranges::all_of(std::array { TestType::SEQ, TestType::RND }, [&](TestType type) {
            const auto& t = state->tests[static_cast<size_t>(type)];
            return t.readBandwidth.value() > 0.0 && t.writeBandwidth.value() > 0.0;
        });
    };
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!hasAllBandwidths() && std::chrono::steady_clock::now() < deadline) {
        AThread::processMessages();
        std::this_thread::sleep_for(10ms);
    }

    EXPECT_FALSE(state->isTestRunning);
    for (auto type : { TestType::SEQ, TestType::RND }) {
        const auto& t = state->tests[static_cast<size_t>(type)];
        EXPECT_GT(t.readBandwidth.value(), 0.0) << AEnumerate<TestType>::toName(type);
        EXPECT_GT(t.writeBandwidth.value(), 0.0) << AEnumerate<TestType>::toName(type);
        EXPECT_FLOAT_EQ(static_cast<float>(t.progress.value()), 1.0f);
    }
    EXPECT_FLOAT_EQ(static_cast<float>(state->tests[static_cast<size_t>(TestType::ALL)].progress.value()), 1.0f);
}
