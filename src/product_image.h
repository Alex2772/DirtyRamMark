#pragma once
#include "AUI/Util/Declarative/Contracts.h"

namespace ui {
struct ProductImage {
    AString name;

    AArc<AView> operator()();
};
}
