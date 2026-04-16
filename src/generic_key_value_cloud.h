#pragma once

#include <AUI/View/AView.h>
#include <AUI/Json/AJson.h>

namespace generic_key_value_cloud {
    _<AView> makeView(const AJson::Object& entries);
}