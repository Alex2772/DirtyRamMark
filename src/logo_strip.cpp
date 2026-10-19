#include "logo_strip.h"

#include <algorithm>
#include <AUI/ASS/Property/BackgroundImage.h>
#include <AUI/Util/ABuiltinFiles.h>
#include <AUI/Util/UIBuildingHelpers.h>

#include "hw_logo.h"

using namespace ass;
using namespace declarative;

namespace {

_<AView> logoView(const hw_logo::Logo& logo) {
    if (logo.asset.empty() || !ABuiltinFiles::contains("img/{}"_format(logo.asset))) {
        return nullptr;
    }
    return _new<AView>() AUI_OVERRIDE_STYLE {
        FixedSize { 64_dp, 48_dp },
        BackgroundImage { ":img/{}"_format(logo.asset), {}, {}, Sizing::CONTAIN_PADDING },
    };
}

}   // namespace

_<AView> ui::logoStrip(const AJson::Object& info) {
    AVector<_<AView>> logos;
    const auto add = [&](const hw_logo::Logo& logo) {
        if (auto v = logoView(logo)) {
            logos << std::move(v);
        }
    };
    const auto value = [&](const AString& key) -> std::string {
        for (const auto& [k, v] : info) {
            if (k == key) return v.asStringOpt().valueOr(AString {}).toStdString();
        }
        return {};
    };
    add(hw_logo::os(value("OS")));
    add(hw_logo::cpu(value("CPU")));
    std::vector<AString> keys;
    for (const auto& [key, v] : info) {
        if (key == "GPU" || key.startsWith("GPU ")) keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        add(hw_logo::gpu(value(key)));
    }
    if (logos.empty()) {
        return nullptr;
    }
    return Centered { Horizontal { std::move(logos) } AUI_OVERRIDE_STYLE { LayoutSpacing { 12_dp } } };
}
