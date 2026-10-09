#pragma once

#include <array>
#include <thread>
#include <vector>
#include <glm/glm.hpp>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Thread/AFuture.h>
#include <AUI/Traits/values.h>
#include <AUI/View/AView.h>
#include <AUI/Reflect/AEnumerate.h>

namespace ui {
    enum class TestType {
        ALL,
        SEQ,
        RND,
        COUNT,
    };
}

AUI_ENUM_VALUES(ui::TestType, ui::TestType::ALL, ui::TestType::SEQ, ui::TestType::RND, ui::TestType::COUNT)

namespace ui {

    struct TestState {
        AProperty<aui::float_within_0_1> progress = 0.0f; // for progress button
        AProperty<double> readBandwidth = 0.0; // for tests[ALL] will be unused
        AProperty<double> writeBandwidth = 0.0;// for tests[ALL] will be unused
    };

    struct State {
        AAsyncHolder async;
        AProperty<int> bufferSizeGB = 8;
        AProperty<int> threads = std::thread::hardware_concurrency();
        AProperty<bool> isTestRunning = false;
    
        std::array<TestState, static_cast<size_t>(TestType::COUNT)> tests;
    
        // Shared buffer for read/write operations
        std::vector<glm::dvec4> buffer;
    };

    _<AView> memoryBenchmark();

    /// Runs SEQ and RND read/write tests, filling `state->tests`. Must be called from the main thread.
    AFuture<> runAllTests(_<State> state);
}
