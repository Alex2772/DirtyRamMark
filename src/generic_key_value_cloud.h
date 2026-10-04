#pragma once

#include <AUI/View/AView.h>
#include <AUI/Json/AJson.h>

namespace generic_key_value_cloud {
    _<AView> makeFlex(const AJson::Object& entries);
    _<AView> makeGrid(const AJson::Object& entries);
}