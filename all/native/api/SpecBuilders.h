/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_SPECBUILDERS_H_
#define _MASSIF_API_SPECBUILDERS_H_

#include "api/Context.h"
#include "api/Spec.h"
#include "api/StructCodec.h"
#include "core/Variant.h"

#include <memory>
#include <set>
#include <string>

namespace massif { namespace api {

    /**
     * Reading one spec key, for the generated constructor builders and the hand-written factories.
     *
     * Absent is not an error at this level: whether a key is required is decided by the caller,
     * which for a generated builder is the overload guard that ran before it.
     */
    std::string stringAt(const Variant& spec, const char* key,
                         const std::string& fallback = std::string());
    int intAt(const Variant& spec, const char* key, int fallback);
    double floatAt(const Variant& spec, const char* key, double fallback);
    bool boolAt(const Variant& spec, const char* key, bool fallback);
    Variant variantAt(const Variant& spec, const char* key);

    /** A by-value struct, through the same codec a property uses. Absent decodes to a default. */
    template <typename T>
    T structAt(const Variant& spec, const char* key) {
        T value;
        StructCodec::decode(variantAt(spec, key).toString(), value);
        return value;
    }

    /**
     * Resolves a reference that is either a registry id or an inline spec of another kind.
     *
     * requiredClass is what the caller is about to cast to: a "style" id naming a source has to be
     * refused here, not cast and read as the wrong class.
     */
    Result childOf(Context& context, const Variant& spec, const char* key, const char* kind,
                   const char* requiredClass, std::shared_ptr<void>& out);

    /** One spec value as a property value: a scalar as itself, an array or object as JSON. */
    PropertyValue specValue(const Variant& value);

    /**
     * Applies every spec key the factory did not consume, as a property ON THIS OBJECT.
     *
     * Spec::create does the same to a registered object; this one works on an intermediate that has
     * no handle - a style builder, whose setters ARE the JSON schema.
     */
    void applySpecProperties(const ObjectRef& object, const Variant& spec,
                             std::set<std::string>& consumed);

    /**
     * Builds a class that declares a !spec in its .i, from its own constructor.
     *
     * Generated - see scripts/gen-api-tables.py. Its own translation unit so a reduced property
     * table can link it: the full one needs every source, layer and service.
     * @return RESULT_UNKNOWN_TYPE when no declared class of that kind has that type.
     */
    Result buildFromConstructor(Context& context, const std::string& kind, const Variant& spec,
                                ObjectRef& object, std::set<std::string>& consumed);

} }

#endif
