#include <range/v3/all.hpp>

#include <AUI/Platform/Entry.h>
#include <AUI/Thread/AThread.h>
#include <AUI/Traits/values.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/ADrawableView.h>
#include <AUI/View/AButton.h>
#include <AUI/View/ANumberPicker.h>
#include <AUI/View/AProgressBar.h>
#include <AUI/View/ASpinnerV2.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Platform/AWindow.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <cstdint>
#include "AUI/Platform/ACursor.h"
#include "AUI/Reflect/AEnumerate.h"

using namespace ass;
using namespace declarative;

static constexpr auto GIGA_PC_BANDWIDTH = 1024.0 * 1024.0 * 1024.0 * 100 /* gb */;

enum class TestType {
    ALL,
    SEQ,
    RND,
    COUNT,
};

AUI_ENUM_VALUES(TestType, TestType::ALL, TestType::SEQ, TestType::RND, TestType::COUNT)

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

// Template function to access buffer with different patterns
template<TestType T>
inline glm::dvec4& accessBuffer(std::vector<glm::dvec4>& buffer, size_t offset, size_t index);

// Specialization for sequential access
template<>
inline glm::dvec4& accessBuffer<TestType::SEQ>(std::vector<glm::dvec4>& buffer, size_t offset, size_t index) {
    return buffer[offset + index];
}

// Specialization for random access
template<>
inline glm::dvec4& accessBuffer<TestType::RND>(std::vector<glm::dvec4>& buffer, size_t offset, size_t index) {
    // Each thread will have its own RNG to avoid contention
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<size_t> dist(0, buffer.size() - 1);
    
    return buffer[dist(gen)];
}

// Operation type for combined measure function
enum class OperationType {
    READ,
    WRITE
};

// Combined template measure function
template<TestType T, OperationType Op>
AFuture<> measure(std::vector<glm::dvec4>& buffer, size_t threadCount, std::function<void(aui::float_within_0_1, double)> onProgress) {
    return AUI_THREADPOOL_X [=, &buffer] {
        size_t bufferSizeBytes = buffer.size() * sizeof(glm::dvec4);

        auto start = std::chrono::high_resolution_clock::now();

        auto progress = _new<aui::float_within_0_1>(0.0f);
        auto processedBytes = _new<size_t>(0);

        glm::dvec4 crc{};
        const auto CHUNK_SIZE = buffer.size() / threadCount;
        const auto PROGRESS_INTERVAL = CHUNK_SIZE / 4;
        
        {
            std::vector<std::thread> threads;

            for (size_t i = 0; i < threadCount; ++i) {
                size_t offset = i * CHUNK_SIZE;
                
                threads.emplace_back([=, &buffer, bufferSize = buffer.size(), &crc]() mutable {
                    glm::dvec4 localCrc{};
                    for (size_t index = 0; index < CHUNK_SIZE; ++index) {

                        if constexpr (Op == OperationType::READ) {
                            // Simulate some processing (e.g., CRC calculation)
                            localCrc += accessBuffer<T>(buffer, offset, index); // Dummy operation to prevent optimization
                        } else {
                            accessBuffer<T>(buffer, offset, index) = glm::dvec4(1, 2, 3, 4);
                        }

                        if ((index + 1) % PROGRESS_INTERVAL == 0) {
                            const auto now = std::chrono::high_resolution_clock::now();
                            AThread::main()->enqueue([=] {
                                *progress = *progress + static_cast<float>(PROGRESS_INTERVAL) / bufferSize;
                                *processedBytes += PROGRESS_INTERVAL * threadCount;
                                const std::chrono::duration<double> elapsed = now - start;
                                double currentSpeed = static_cast<double>(*processedBytes) / elapsed.count();
                                onProgress(*progress, currentSpeed);
                            });
                        }
                    }
                    crc += localCrc;
                });
            }
            for (auto& i : threads) {
                i.join();
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        ALogger::info("main") << "CRC: " << crc;
        std::chrono::duration<double> elapsed = end - start;
        double finalSpeed = static_cast<double>(bufferSizeBytes) / elapsed.count();
        AThread::main()->enqueue([=] {
            onProgress(1.0f, finalSpeed);
        });
    };
}

static _<AView> myProgressBar(contract::In<aui::float_within_0_1> progress) {
    return ProgressBar {
        .progress = std::move(progress),
        .inner = _new<AView>() AUI_OVERRIDE_STYLE {
            BackgroundImage { ":img/progress.png", AStylesheet::getOsThemeColor() * 2.f, Repeat::NONE, Sizing::FIT },
        },
    } AUI_OVERRIDE_STYLE {
        BackgroundSolid { AColor::WHITE },
        Border { 1_px, AColor::GRAY },
        BorderRadius { 0 },
        FixedSize { {}, 40_dp },
        Padding { 2_px },
        Expanding {},
    };
}

static _<AView> myProgressBarWithButton(contract::In<aui::float_within_0_1> progress, _<AView> content, contract::Slot<> onClick) {
    return Stacked {
        myProgressBar(std::move(progress)),
        std::move(content),
    } AUI_OVERRIDE_STYLE {
        ACursor::POINTER,
    } AUI_LET {
        onClick.bindTo(it->clicked);
    };
}


static _<AView> myProgressBarWithLabel(contract::In<aui::float_within_0_1> progress, _<AView> content) {
    return Stacked {
        myProgressBar(std::move(progress)),
        Horizontal::Expanding {
            SpacerExpanding {},
            std::move(content),
        } AUI_OVERRIDE_STYLE {
            Padding(4_dp),
        },
    };
}

// Helper function to run a specific test type
template<TestType T>
static void runTestType(_<State> state) {
    auto& testState = state->tests[static_cast<size_t>(T)];
    testState.progress = 0.f;
    state->isTestRunning = true;
    testState.readBandwidth = 0;
    testState.writeBandwidth = 0;
    
    state->async << AUI_THREADPOOL {
        // Allocate buffer
        size_t bufferSizeBytes = static_cast<size_t>(state->bufferSizeGB) * 1024ULL * 1024ULL * 1024ULL;
        state->buffer.resize(bufferSizeBytes / sizeof(glm::dvec4));
        

        state->async << measure<T, OperationType::READ>(state->buffer, state->threads, [=, testType = T](aui::float_within_0_1 p, double speed) {
            // Read phase: 0-50% progress
            state->tests[static_cast<size_t>(testType)].progress = p * 0.5f;
            state->tests[static_cast<size_t>(testType)].readBandwidth = speed;
        }).onFinally([=, testType = T] {
            AThread::main()->enqueue([=] {
                // Reset to 50% at the start of write phase
                state->tests[static_cast<size_t>(testType)].progress = 0.5f;
            });
            state->async << measure<T, OperationType::WRITE>(state->buffer, state->threads, [=, testType = T](aui::float_within_0_1 p, double speed) {
                // Write phase: 50-100% progress
                state->tests[static_cast<size_t>(testType)].progress = 0.5f + p * 0.5f;
                state->tests[static_cast<size_t>(testType)].writeBandwidth = speed;
            }).onFinally([=, testType = T] {
                AThread::main()->enqueue([=] {
                    state->isTestRunning = false;
                    state->tests[static_cast<size_t>(testType)].progress = 1.0f;
                });
            });
        });
    };
}

// Run all tests sequentially
static void runAllTests(_<State> state) {
    state->isTestRunning = true;
    
    // Reset all progress
    state->tests.fill({});
    
    state->async << AUI_THREADPOOL {
        // Allocate buffer
        size_t bufferSizeBytes = static_cast<size_t>(state->bufferSizeGB) * 1024ULL * 1024ULL * 1024ULL;
        state->buffer.resize(bufferSizeBytes / sizeof(glm::dvec4));
        
        // Run SEQ test first
        state->async << measure<TestType::SEQ, OperationType::READ>(state->buffer, state->threads, [=](aui::float_within_0_1 p, double speed) {
            // SEQ read progress: 0-50% of SEQ test, 0-25% of overall
            state->tests[static_cast<size_t>(TestType::SEQ)].progress = p * 0.5f;
            state->tests[static_cast<size_t>(TestType::SEQ)].readBandwidth = speed;
            // Overall progress: 0-25%
            state->tests[static_cast<size_t>(TestType::ALL)].progress = p * 0.25f;
        }).onFinally([=] {
            AThread::main()->enqueue([=] {
                state->tests[static_cast<size_t>(TestType::SEQ)].progress = 0.5f;
            });
            state->async << measure<TestType::SEQ, OperationType::WRITE>(state->buffer, state->threads, [=](aui::float_within_0_1 p, double speed) {
                // SEQ write progress: 50-100% of SEQ test, 25-50% of overall
                state->tests[static_cast<size_t>(TestType::SEQ)].progress = 0.5f + p * 0.5f;
                state->tests[static_cast<size_t>(TestType::SEQ)].writeBandwidth = speed;
                // Overall progress: 25-50%
                state->tests[static_cast<size_t>(TestType::ALL)].progress = 0.25f + p * 0.25f;
            }).onFinally([=] {
                // Now run RND test
                state->async << measure<TestType::RND, OperationType::READ>(state->buffer, state->threads, [=](aui::float_within_0_1 p, double speed) {
                    // RND read progress: 0-50% of RND test, 50-75% of overall
                    state->tests[static_cast<size_t>(TestType::RND)].progress = p * 0.5f;
                    state->tests[static_cast<size_t>(TestType::RND)].readBandwidth = speed;
                    // Overall progress: 50-75%
                    state->tests[static_cast<size_t>(TestType::ALL)].progress = 0.5f + p * 0.25f;
                }).onFinally([=] {
                    AThread::main()->enqueue([=] {
                        state->tests[static_cast<size_t>(TestType::RND)].progress = 0.5f;
                    });
                    state->async << measure<TestType::RND, OperationType::WRITE>(state->buffer, state->threads, [=](aui::float_within_0_1 p, double speed) {
                        // RND write progress: 50-100% of RND test, 75-100% of overall
                        state->tests[static_cast<size_t>(TestType::RND)].progress = 0.5f + p * 0.5f;
                        state->tests[static_cast<size_t>(TestType::RND)].writeBandwidth = speed;
                        // Overall progress: 75-100%
                        state->tests[static_cast<size_t>(TestType::ALL)].progress = 0.75f + p * 0.25f;
                    }).onFinally([=] {
                        AThread::main()->enqueue([=] {
                            state->isTestRunning = false;
                            state->tests[static_cast<size_t>(TestType::SEQ)].progress = 1.0f;
                            state->tests[static_cast<size_t>(TestType::RND)].progress = 1.0f;
                            state->tests[static_cast<size_t>(TestType::ALL)].progress = 1.0f;
                        });
                    });
                });
            });
        });
    };
}

template<TestType T>
static std::pair<std::variant<AString, _<AView>>, _<AView>> testRow(_<State> state) {
    return {
        myProgressBarWithButton( AUI_REACT(state->tests[static_cast<size_t>(T)].progress), Label { AEnumerate<TestType>::toName(T) }, [=] {
            if (state->isTestRunning) {
                return;
            }
            runTestType<T>(state);
        }),
        Horizontal {
            myProgressBarWithLabel(
                AUI_REACT(state->tests[static_cast<size_t>(T)].readBandwidth / GIGA_PC_BANDWIDTH),
                Label { AUI_REACT("{:.2f} GB/s"_format(state->tests[static_cast<size_t>(T)].readBandwidth / 1024.0 / 1024.0 / 1024.0)) }) AUI_OVERRIDE_STYLE { Expanding() },
            myProgressBarWithLabel(
                AUI_REACT(state->tests[static_cast<size_t>(T)].writeBandwidth / GIGA_PC_BANDWIDTH),
                Label { AUI_REACT("{:.2f} GB/s"_format(state->tests[static_cast<size_t>(T)].writeBandwidth / 1024.0 / 1024.0 / 1024.0)) }) AUI_OVERRIDE_STYLE { Expanding() },
            
        } AUI_OVERRIDE_STYLE {
            LayoutSpacing(4_dp),
        },
    };
}

AUI_ENTRY {
    auto window = _new<AWindow>("DirtyRamMark {}"_format(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION)), 300_dp, 200_dp);
    auto state = _new<State>();
    window->setContents(
      Vertical {
#if AUI_DEBUG
        Centered {Label { "Debug build - results might be inaccurate" } AUI_OVERRIDE_STYLE {
            TextColor(AColor::RED),
        } },
#endif
        _form({
            {
                myProgressBarWithButton( AUI_REACT(state->tests[static_cast<size_t>(TestType::ALL)].progress), Label { "All" }, [=] {
                    if (state->isTestRunning) {
                        return;
                    }
                    runAllTests(state);
                }),
                Vertical {
                    Horizontal {
                        Horizontal { _new<ANumberPicker>() && state->bufferSizeGB, Label { "GB"} },
                        Horizontal { _new<ANumberPicker>() && state->threads, Label { "cores" } },
                    } AUI_OVERRIDE_STYLE {
                        LayoutSpacing(8_dp),
                    },
                    SpacerExpanding {},
                    Horizontal {
                        Centered { Label { "Read" } } AUI_OVERRIDE_STYLE { Expanding() },
                        Centered { Label { "Write" } } AUI_OVERRIDE_STYLE { Expanding() },
                    },
                }
            },
            testRow<TestType::SEQ>(state),
            testRow<TestType::RND>(state),
        }) AUI_OVERRIDE_STYLE {
            LayoutSpacing(4_dp),
        },


        Horizontal {
            Horizontal::Expanding {
                Centered {
                    _new<ASpinnerV2>()
                },
            } AUI_OVERRIDE_STYLE {
                LayoutSpacing(4_dp),
            } AUI_LET {
                AObject::connect(AUI_REACT(state->isTestRunning), AUI_SLOT(it)::setVisible);
                AObject::connect(state->isTestRunning.changed, AUI_SLOT(window)::redraw);
            },
        } AUI_OVERRIDE_STYLE {
            LayoutSpacing(4_dp),
        },
      } AUI_OVERRIDE_STYLE {
        LayoutSpacing { 4_dp },
      }
    );

    window->show();

    return 0;
};
