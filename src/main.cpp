#include <range/v3/all.hpp>

#include <AUI/Platform/Entry.h>
#include <AUI/Thread/AThread.h>
#include <AUI/Traits/values.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AGroupBox.h>
#include <AUI/View/ATabView.h>
#include <AUI/Thread/AAsyncHolder.h>
#include <AUI/Platform/AWindow.h>
#include "AUI/Platform/ACursor.h"
#include "generic_key_value_cloud.h"
#include "memory_timings.h"
#include "cli.h"
#include "privileges.h"

#include <memory_benchmark.h>
#include "general_tab.h"
#include <memory_info.h>
#include <AUI/View/AScrollArea.h>
#include <AUI/View/AButton.h>
#include <AUI/Layout/AVerticalLayout.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/ASS/Property/Expanding.h>
#include <functional>
#include <AUI/View/AText.h>

using namespace ass;
using namespace declarative;

static AArc<AView> genericScrollable(AArc<AView> contents) {
    return AScrollArea::Builder()
        .withContents(std::move(contents))
        .build() AUI_OVERRIDE_STYLE {
            MinSize { 500_dp },
            Expanding {0, 1},
        };
}

/// Builds the "Upgrade privileges" prompt for the tab.
using UpgradePrompt = std::function<_<AView>(const AString& reason)>;

/// Message and a button that applies the fix of a privileges::SettingRequired.
static _<AView> settingPrompt(const privileges::SettingRequired& e) {
    return Centered {
        Vertical {
          AText::fromString(e.getMessage()),
          Button {
            .content = Label { e.buttonLabel() },
            .onClick =
                [fix = e.fix()] {
                    try {
                        fix();
                    } catch (const AException& e) {
                        ALogger::err("privileges") << "Failed: " << e;
                    }
                },
          },
        },
    };
}

static void fillTab(const _<AViewContainer>& holder, std::function<_<AView>(const UpgradePrompt&)> factory) {
    holder->removeAllViews();
    auto prompt = [weak = std::weak_ptr(holder), factory](const AString& reason) -> _<AView> {
        return Centered {
            Vertical {
              AText::fromString(reason),
              Button {
                .content = Label { "Upgrade privileges to obtain more info" },
                .onClick =
                    [weak, factory] {
                        auto holder = weak.lock();
                        if (!holder) {
                            return;
                        }
                        try {
                            privileges::upgrade();
                        } catch (const AException& e) {
                            ALogger::err("privileges") << "Failed: " << e;
                            return;
                        }
                        fillTab(holder, factory);
                    },
              },
            },
        };
    };
    try {
        holder->addView(factory(prompt));
    } catch (const privileges::Required& e) {
        holder->addView(Centered { prompt(e.getMessage()) });
    } catch (const privileges::SettingRequired& e) {
        holder->addView(settingPrompt(e));
    } catch (const AException& e) {
        ALogger::err("safeQuery") << "Failed: " << e;
        holder->addView(Centered { AText::fromString("Query failed: {}"_format(e.getMessage())) });
    }
}

/// A tab that shows what the factory made. If it needs more privileges, an upgrade button is shown instead (see
/// UpgradePrompt) and the contents are rebuilt after the upgrade.
static _<AView> privilegedTab(std::function<_<AView>(const UpgradePrompt&)> factory) {
    auto holder = _new<AViewContainer>() AUI_OVERRIDE_STYLE { Expanding {}, MinSize { 500_dp } };
    holder->setLayout(std::make_unique<AVerticalLayout>());
    fillTab(holder, std::move(factory));
    return holder;
}

AUI_ENTRY {
    if (auto exitCode = runCli(args)) {
        return *exitCode;
    }

    auto window = _new<AWindow>("DirtyRamMark {}"_format(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION)), 300_dp, 200_dp);
    auto tabs = _new<ATabView>() AUI_OVERRIDE_STYLE { Expanding {} };
    tabs->addTab(ui::generalTab(), "General");
    tabs->addTab(ui::memoryBenchmark(), "Benchmark");
    tabs->addTab(
        privilegedTab([](const UpgradePrompt&) {
            return genericScrollable(generic_key_value_cloud::makeFlex(memoryInfo()));
        }),
        "Banks");
    tabs->addTab(
        privilegedTab([](const UpgradePrompt& prompt) {
            AJson::Object entries;
            entries["Memory Controller"] = memory_timings::describeController(memory_timings::readCpuId());
            _<AView> footer;
            try {
                for (auto& [name, timings] : memoryTimings()) {
                    entries[name] = std::move(timings);
                }
            } catch (const privileges::Required& e) {
                footer = prompt(e.getMessage());
            } catch (const privileges::SettingRequired& e) {
                footer = settingPrompt(e);
            }
            auto grid = genericScrollable(generic_key_value_cloud::makeGrid(entries));
            if (!footer) {
                return grid;
            }
            return _<AView>(Vertical { grid, footer });
        }),
        "Timings");
    window->setContents(Vertical { tabs });

    window->show();

    return 0;
};
