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
     * Builds SDK objects from JSON specs. A factory handles only constructor arguments; every other
     * key is applied through the property table afterwards.
     */
    class Spec {
    public:
        /**
         * Builds one object from a spec. Plugins register these to add a type; tests to avoid linking every constructor.
         * @param context For resolving nested references by id.
         * @param spec The parsed spec, whose "type" chose this factory.
         * @param object Set to the new object and its class on success.
         * @param consumed The spec keys the factory used; the rest are applied as properties.
         */
        typedef Result (*Factory)(Context& context, const Variant& spec, ObjectRef& object,
                                  std::set<std::string>& consumed);

        /** Registers a factory for one "type" of a kind - how a plugin adds a type. */
        static void registerFactory(const std::string& kind, const std::string& type, Factory factory);

        /** Registers the fallback factory for a kind, used when no type-level one matches. */
        static void registerFactory(const std::string& kind, Factory factory);

        /** Registers the factories the SDK ships. Called on first use. */
        static void registerBuiltinFactories();

        /**
         * Builds an object from a JSON spec and registers it under a kind and id. An identical spec
         * under an existing id returns that handle; a different one is refused. Unknown keys only warn.
         */
        static Result create(Context& context, const std::string& kind, const std::string& id,
                             const std::string& json, Handle& handle);

        /** Whether a factory is registered for a kind. */
        static bool hasFactory(const std::string& kind);

        /**
         * Builds an object of the given kind.
         * @param context The context, for resolving nested references by id.
         * @param kind The object kind, e.g. "source", "layer", "options".
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
