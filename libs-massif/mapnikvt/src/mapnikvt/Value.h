/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_VALUE_H_
#define _CARTO_MAPNIKVT_VALUE_H_

#include <variant>
#include <string>

namespace carto { namespace mvt {
    using Value = std::variant<std::monostate, bool, long long, double, std::string>;
} }

#endif
