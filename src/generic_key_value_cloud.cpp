#include <optional>
#include <range/v3/all.hpp>
#include <range/v3/range/conversion.hpp>
#include "generic_key_value_cloud.h"
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/View/AText.h>
#include <AUI/View/AButton.h>
#include <AUI/View/ATextField.h>
#include <AUI/View/ASpacerFixed.h>
#include <AUI/View/AScrollArea.h>
#include <AUI/View/AGroupBox.h>
#include <AUI/Platform/AClipboard.h>
#include <AUI/Util/UIBuildingHelpers.h>

using namespace declarative;
using namespace ass;

struct RenderFlags {
    enum class Container {
        FLEXBOX,
        FIXED_GRID,
    } container = Container::FLEXBOX;
};

static _<AView> dispatch(const AJson& entry, const RenderFlags&);

// std::nullopt_t, std::nullptr_t, int, int64_t, double, bool, AString, aui::impl::JsonArray, aui::impl::JsonObject
static _<AView> impl(const AString& text, const RenderFlags&) {
    return _new<ATextField>() AUI_OVERRIDE_STYLE { MinSize {
         80_dp * float(glm::clamp((text.length() + 6) / 7, size_t(1), size_t(3))), {}
        } } AUI_LET {
        it->setText(text);
        it->setEditable(false);
    };
}

static _<AView> impl(std::nullptr_t, const RenderFlags& flags) {
    return impl("-", flags);
}

static _<AView> impl(std::nullopt_t, const RenderFlags& flags) {
    return impl("-", flags);
}

static _<AView> impl(int v, const RenderFlags& flags) {
    return impl("{}"_format(v), flags);
}

static _<AView> impl(int64_t v, const RenderFlags& flags) {
    return impl("{}"_format(v), flags);
}

static _<AView> impl(double v, const RenderFlags& flags) {
    return impl("{}"_format(v), flags);
}

static _<AView> impl(bool v, const RenderFlags& flags) {
    return impl("{}"_format(v), flags);
}

static _<AView> impl(const AJson::Array& v, const RenderFlags& flags) {
    return Vertical {
        ranges::views::transform(v, [&](const AJson& e) {
            return dispatch(e, flags);
        }) | ranges::to<AVector<_<AView>>>(),
    } AUI_OVERRIDE_STYLE {
        LayoutSpacing { 2_dp },
    };
}


static _<AView> impl(const AJson::Object& entries, const RenderFlags& flags) {
    auto generator = ranges::views::transform(entries, [&](const std::pair<AString, AJson>& e) -> _<AView> {
            if (e.second.isObject()) {
                return GroupBox {
                    Label { e.first },
                    dispatch(e.second, flags),
                } AUI_OVERRIDE_STYLE {
                    MinSize { 420_dp, {} },
                };
            }
            return Horizontal {
                Label { e.first } AUI_OVERRIDE_STYLE {
                    Expanding{},
                    ATextAlign::RIGHT,
                },
                SpacerFixed { 2_dp },
                dispatch(e.second, flags) AUI_LET { it->setExpanding(); },
            } AUI_OVERRIDE_STYLE {
                Padding { 1_px, 0 },
            };
    });

    auto container = [&]() -> AArc<AView> {
        switch (flags.container) {
            case RenderFlags::Container::FLEXBOX: {
                auto text = _new<AText>();
                text->setItems(generator | ranges::to<AVector<std::variant<AString, _<AView>>>>);
                return text;
            }
            case RenderFlags::Container::FIXED_GRID: {
                return Vertical { generator | ranges::to<AVector<_<AView>>> };
            }
        }
        return nullptr;
    }();

    container->setCustomStyle({
        LayoutSpacing { 4_dp },
        LineHeight { 1.f },
        ATextAlign::JUSTIFY,
        MinSize { 380_dp, {} },
    });
    return container;
}

static _<AView> dispatch(const AJson& entry, const RenderFlags& flags) {
    return std::visit([&](const auto& value) {
        return impl(value, flags);
    }, static_cast<const aui::impl::JsonVariant&>(entry));
}

static _<AView> copyableJson(_<AView> contents, const AJson::Object& entries) {
    return Vertical::Expanding {
        std::move(contents),
        Button {
            .content = Label {"Copy"},
            .onClick = [asString = AJson::toString(entries)] {
                AClipboard::copyToClipboard(asString);
            },
        },
    } AUI_OVERRIDE_STYLE { Expanding(), MinSize {430_dp } };
}

_<AView> generic_key_value_cloud::makeFlex(const AJson::Object& entries) {
    return copyableJson(impl(entries, { .container = RenderFlags::Container::FLEXBOX }), entries);
}

_<AView> generic_key_value_cloud::makeGrid(const AJson::Object& entries) {
    return copyableJson(impl(entries, { .container = RenderFlags::Container::FIXED_GRID }), entries);
}
