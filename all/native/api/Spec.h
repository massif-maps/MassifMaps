/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_SPEC_H_
#define _MASSIF_API_SPEC_H_

#include "api/Context.h"
#include "core/Variant.h"

#include <set>
#include <string>

namespace massif { namespace api {

    /**
     * Builds SDK objects from JSON specs.
     *
     * A factory only handles what a constructor needs. Every other key in the spec is applied
     * through the property table afterwards, so adding an option to a class costs nothing here -
     * and an option the SDK does not have is a warning, not an error, so a spec written for
     * another version still applies what it can.
     */
    class Spec {
    public:
        /**
         * Builds an object of the given kind.
         * @param context The context, for resolving nested references by id.
         * @param kind The object kind, currently only "source".
         * @param spec The parsed spec. Its "type" chooses the factory.
         * @param object Set to the new object and its class on success.
         * @param consumed The spec keys the factory used, so the caller knows which are left.
         * @return RESULT_OK, or RESULT_UNKNOWN_TYPE when nothing builds that "type".
         */
        static Result build(Context& context, const std::string& kind, const Variant& spec,
                            ObjectRef& object, std::set<std::string>& consumed);

    private:
        Spec();
    };

} }

#endif
