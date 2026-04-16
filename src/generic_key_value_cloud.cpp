#include <optional>
#include <range/v3/all.hpp>
#include <range/v3/range/conversion.hpp>
#include "generic_key_value_cloud.h"
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/View/AText.h>
#include <AUI/View/AButton.h>
#include <AUI/View/ATextField.h>
#include <AUI/View/ASpacerFixed.h>
#include <AUI/View/AGroupBox.h>
#include <AUI/Platform/AClipboard.h>
#include <AUI/Util/UIBuildingHelpers.h>

using namespace declarative;
using namespace ass;


static _<AView> dispatch(const AJson& entry);

// std::nullopt_t, std::nullptr_t, int, int64_t, double, bool, AString, aui::impl::JsonArray, aui::impl::JsonObject
static _<AView> impl(const AString& text) {
    return _new<ATextField>() AUI_OVERRIDE_STYLE { MinSize {
         80_dp * float(glm::clamp((text.length() + 6) / 7, size_t(1), size_t(3))), {}
        } } AUI_LET {
        it->setText(text);
        it->setEditable(false);
    };
}

static _<AView> impl(std::nullptr_t) {
    return impl("-");
}

static _<AView> impl(std::nullopt_t) {
    return impl("-");
}

static _<AView> impl(int v) {
    return impl("{}"_format(v));
}

static _<AView> impl(int64_t v) {
    return impl("{}"_format(v));
}

static _<AView> impl(double v) {
    return impl("{}"_format(v));
}

static _<AView> impl(bool v) {
    return impl("{}"_format(v));
}

static _<AView> impl(const AJson::Array& v) {
    return Vertical {
        ranges::views::transform(v, [](const AJson& e) {
            return dispatch(e);
        }) | ranges::to<AVector<_<AView>>>(),
    } AUI_OVERRIDE_STYLE {
        LayoutSpacing { 2_dp },
    };
}


static _<AView> impl(const AJson::Object& entries) {
    auto text = _new<AText>();
    text->setItems(
        ranges::views::transform(entries, [](const std::pair<AString, AJson>& e) -> _<AView> {
            if (e.second.isObject()) {
                return GroupBox {
                    Label { e.first },
                    dispatch(e.second),
                } AUI_OVERRIDE_STYLE {
                    MinSize { 400_dp, {} },
                };
            }
            return Horizontal {
                Label { e.first } AUI_OVERRIDE_STYLE {
                    Expanding{},
                    ATextAlign::RIGHT,
                },
                SpacerFixed { 2_dp },
                dispatch(e.second) AUI_LET { it->setExpanding(); },
            } AUI_OVERRIDE_STYLE {
                Padding { 1_px, 0 },
            };
    }) | ranges::to<AVector<std::variant<AString, _<AView>>>>);
    text->setCustomStyle({
        LayoutSpacing { 4_dp },
        LineHeight { 1.f },
        ATextAlign::JUSTIFY,
    });
    return text;
}


static _<AView> dispatch(const AJson& entry) {
    return std::visit([](const auto& value) {
        return impl(value);
    }, static_cast<const aui::impl::JsonVariant&>(entry));
}


_<AView> generic_key_value_cloud::makeView(const AJson::Object& entries) {
    return Vertical {
        impl(entries),
        Button {
            .content = Label {"Copy"},
            .onClick = [asString = AJson::toString(entries)] {
                AClipboard::copyToClipboard(asString);
            },
        },
    };
}