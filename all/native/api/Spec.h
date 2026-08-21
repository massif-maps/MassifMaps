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
         * Builds one object from a spec. The registry is what a plugin extends to add a type,
         * and what a test replaces to exercise create() without linking every constructor.
         * @param context For resolving nested references by id.
         * @param spec The parsed spec, whose "type" chose this factory.
         * @param object Set to the new object and its class on success.
         * @param consumed The spec keys the factory used; the rest are applied as properties.
         */
        typedef Result (*Factory)(Context& context, const Variant& spec, ObjectRef& object,
                                  std::set<std::string>& consumed);

        /**
         * Registers a factory for a kind. Replaces any factory already registered for it.
         */
        static void registerFactory(const std::string& kind, Factory factory);

        /**
         * Registers the factories the SDK ships. Called on first use; a test that wants only its
         * own kinds can register those instead.
         */
        static void registerBuiltinFactories();

        /**
         * Builds an object from a JSON spec and registers it under a kind and id.
         *
         * An identical spec under an existing id returns that handle - two maps share one source
         * that way. A different spec under that id is refused. Keys the factory does not consume
         * are applied as properties, and a key the SDK does not know is dropped with a warning,
         * so a spec written against another version still applies what it can.
         */
        static Result create(Context& context, const std::string& kind, const std::string& id,
                             const std::string& json, Handle& handle);

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
