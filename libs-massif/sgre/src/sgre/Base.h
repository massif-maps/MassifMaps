/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_BASE_H_
#define _CARTO_SGRE_BASE_H_

#include <cglib/vec.h>

namespace carto { namespace sgre {
    using Point = cglib::vec3<double>;

    struct RoutingAttributes {
        float speed = 1.38f;
        float zSpeed = 0.5f;
        float turnSpeed = 180.0f;
        float delay = 0.0f;
    };
} }

#endif
