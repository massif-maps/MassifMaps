/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_VALUE_H_
#define _CARTO_CARTOCSS_VALUE_H_

#include "Color.h"

#include <string>
#include <variant>

namespace carto::css {
    using Value = std::variant<std::monostate, bool, long long, double, Color, std::string>;
}

#endif
