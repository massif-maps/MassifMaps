/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PROPERTYTABLE_H_
#define _MASSIF_PROPERTYTABLE_H_

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace massif { namespace api {

    /**
     * How a property value is carried across the API boundary.
     */
    enum PropertyType {
        PT_BOOL,
        PT_INT,
        PT_FLOAT,
        PT_COLOR,
        PT_ENUM,    // an int constant; the name table gives the string spellings
        PT_STRING,
        PT_OBJECT,  // another registry object, addressed by id
        PT_STRUCT,  // MapPos, MapRange, a vector - carried as JSON
        PT_VARIANT  // free-form JSON, and a path can keep walking INTO it
    };

    enum PropertyFlags {
        PF_READONLY = 1,
        PF_STATIC = 2
    };

    /**
     * A property value in transit. Deliberately not a union: the string makes one impossible and
     * these are configuration calls, not a per-frame path.
     */
    struct PropertyValue {
        bool boolValue = false;
        long long intValue = 0;   // also carries COLOR as ARGB and ENUM as its constant
        double floatValue = 0;
        std::string stringValue;
        // Which field a getter filled. Without it a caller reading a bool as a float gets 0 and
        // cannot tell that from a real 0.
        PropertyType type = PT_STRING;

        /** The value as a number, whatever field carries it. */
        double asDouble() const;
        /** The value as an integer, whatever field carries it. */
        long long asLong() const;
        /** The value as a boolean, whatever field carries it. */
        bool asBool() const;

        // Use these rather than assigning a field: an unstamped type reads as the wrong thing.
        static PropertyValue ofBool(bool v);
        static PropertyValue ofLong(long long v);
        static PropertyValue ofDouble(double v);
        static PropertyValue ofString(const std::string& v);
    };

    /**
     * Another object reached through an OBJECT property, kept alive for as long as the reference
     * lives. The class name is what lets a dotted path keep resolving into it.
     */
    struct ObjectRef {
        std::shared_ptr<void> obj;
        const char* cppClass = nullptr;
    };

    struct PropertyEntry {
        const char* path;
        PropertyType type;
        std::uint8_t flags;
        // Null for a type the accessors cannot carry yet (STRUCT), for a static, and - for
        // setter - for a read-only property.
        void (*getter)(void* obj, PropertyValue& value);
        void (*setter)(void* obj, const PropertyValue& value);
        // Set only for OBJECT. Reading is enough to traverse a dotted path; writing one needs
        // the registry and a checked downcast, which land with the spec factories.
        void (*objectGetter)(void* obj, ObjectRef& out);
    };

    struct ClassEntry {
        const char* cppClass;
        const PropertyEntry* props;   // null for a class that declares none of its own
        std::uint16_t count;
        // A property declared on a base is reachable from every class below it, so lookups walk
        // this chain rather than the table being flattened.
        const char* base;
    };

    /**
     * Looks up a class' property table by its fully qualified C++ name.
     * @param cppClass The class name, e.g. "massif::FogOptions".
     * @return The class entry, or null when the class declares no properties.
     */
    const ClassEntry* findClass(const char* cppClass);

    /**
     * Looks up one property of a class, walking up its base chain.
     * @param classEntry The class, from findClass.
     * @param path The property path, e.g. "rangeStart".
     * @return The property, or null when neither the class nor any base declares it.
     */
    const PropertyEntry* findProperty(const ClassEntry* classEntry, const char* path);

    /**
     * Returns the number of classes in the table. Used by tests and tooling.
     */
    std::size_t getClassCount();

    /**
     * Returns a class by index, in the table's sorted order.
     */
    const ClassEntry* getClass(std::size_t index);

} }

#endif
