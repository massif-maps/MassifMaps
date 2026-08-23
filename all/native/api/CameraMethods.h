/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_CAMERAMETHODS_H_
#define _MASSIF_API_CAMERAMETHODS_H_

#include "api/Methods.h"

namespace massif { namespace api {

    /**
     * The camera, as facade methods on BaseMapView.
     *
     * Its own TU because it is the one part of the facade that needs the map view, and pulling the
     * view into MethodImpls.cpp would put the renderer behind every method the SDK registers.
     */
    void registerCameraMethods();

} }

#endif
