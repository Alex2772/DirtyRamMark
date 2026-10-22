#include <AUI/Layout/AVerticalLayout.h>
#include <AUI/Logging/ALogger.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>
#include <AUI/View/AButton.h>
#include <AUI/View/AText.h>

#include "privileged_view.h"

using namespace ass;
using namespace declarative;

namespace {

using Factory = std::function<_<AView>(const ui::UpgradePrompt&)>;

void fill(const _<AViewContainer>& holder, const Factory& factory);

_<AView> upgradePrompt(const std::weak_ptr<AViewContainer>& weakHolder, const Factory& factory, const AString& reason) {
    return Centered {
        Vertical {
          AText::fromString(reason),
          Button {
            .content = Label { privileges::upgradeButtonText() },
            .onClick =
                [weakHolder, factory] {
                    auto holder = weakHolder.lock();
                    if (!holder) {
                        return;
                    }
                    try {
                        privileges::upgrade();
                    } catch (const AException& e) {
                        ALogger::err("privileges") << "Failed: " << e;
                        return;
                    }
                    fill(holder, factory);
                },
          },
        },
    };
}

void fill(const _<AViewContainer>& holder, const Factory& factory) {
    holder->removeAllViews();
    auto prompt = [weak = std::weak_ptr(holder), factory](const AString& reason) {
        return upgradePrompt(weak, factory, reason);
    };
    try {
        holder->addView(factory(prompt));
    } catch (const privileges::Required& e) {
        holder->addView(prompt(e.getMessage()));
    } catch (const AException& e) {
        ALogger::err("privileged_view") << "Failed: " << e;
        holder->addView(Centered { AText::fromString("Query failed: {}"_format(e.getMessage())) });
    }
}

}   // namespace

_<AView> ui::upgradeButton() {
#if AUI_PLATFORM_WIN
    if (!privileges::isGranted()) {
        return Button {
            .content = Label { privileges::upgradeButtonText() },
            .onClick =
                [] {
                    try {
                        privileges::upgrade();   // doesn't return on success: an elevated instance takes over
                    } catch (const AException& e) {
                        ALogger::err("privileges") << "Failed: " << e;
                    }
                },
        };
    }
#endif
    return _new<AView>();
}

_<AView> ui::privilegedView(std::function<_<AView>(const UpgradePrompt&)> factory) {
    auto holder = _new<AViewContainer>();
    holder->setLayout(std::make_unique<AVerticalLayout>());
    holder->setExpanding();
    fill(holder, factory);
    return holder;
}
