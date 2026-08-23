/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_GEOMETRYMETHODS_H_
#define _MASSIF_API_GEOMETRYMETHODS_H_

#include "api/Methods.h"

#include <memory>

namespace massif { namespace api {

    /**
     * Returns an object as a call's result: registers it and puts its handle in the result.
     *
     * Shared because every method that returns an object does exactly this, and because a null
     * object has one meaning - the call produced nothing, which is not the same as failing.
     * @return RESULT_OK, RESULT_FAILED for a null object, or the registration's error.
     */
    Result objectResult(Context& context, const std::shared_ptr<void>& obj, const char* cppClass,
                        PropertyValue& result);

    /**
     * The methods over geometry and collections.
     *
     * Their own translation unit, and their own entry point, so a test can link them: the rest of
     * the built-ins need a data source, a hillshade layer and a CartoCSS decoder, these need
     * nothing beyond the geometry classes.
     */
    void registerGeometryMethods();

} }

#endif
