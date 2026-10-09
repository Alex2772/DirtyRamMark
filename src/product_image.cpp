//
// Created by alex2772 on 10/9/26.
//

#include "product_image.h"

#include <AUI/Util/ABuiltinFiles.h>

using namespace ass;

static AArc<AView> probe(AString path) {
    if (!ABuiltinFiles::contains(path)) {
        return nullptr;
    }

    return _new<AView>() AUI_OVERRIDE_STYLE {
        FixedSize { 100_dp },
        Border { 1_px, AColor::GRAY.opacify(0.5f) },
        BackgroundSolid { AColor::WHITE },
        BackgroundImage { ":{}"_format(path), {}, {}, Sizing::CONTAIN_PADDING },
        Padding { 2_px },
    };
}

AArc<AView> ui::ProductImage::operator()() {
    auto nameCopy = name.lowercase();
    while (!nameCopy.empty()) {
        if (auto v = probe("img/product/{}.jpg"_format(nameCopy))) {
            return v;
        }
        if (auto v = probe("img/product/{}.png"_format(nameCopy))) {
            return v;
        }
        auto eraseAt = nameCopy.rfind(' ');
        if (eraseAt == std::string::npos) {
            break;
        }
        nameCopy.erase(eraseAt);
#if AUI_DEBUG
        AUI_ASSERT(!nameCopy.endsWith(" "));
#endif
    }
    ALogger::warn("product_image") << "A suitable product image for \"" << name << "\" was not found";
    return nullptr;
}