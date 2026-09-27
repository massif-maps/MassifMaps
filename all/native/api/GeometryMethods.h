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
     * @return RESULT_OK, RESULT_FAILED for a null object, or the registration's error.
     */
    Result objectResult(Context& context, const std::shared_ptr<void>& obj, const char* cppClass,
                        PropertyValue& result);

    /**
     * The methods over geometry and collections; separate from the other built-ins so a test can
     * link them without data sources, layers or decoders.
     */
    void registerGeometryMethods();

} }

#endif
