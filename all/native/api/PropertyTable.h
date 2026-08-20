/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PROPERTYTABLE_H_
#define _MASSIF_PROPERTYTABLE_H_

#include <cstdint>
#include <cstddef>
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
        PT_STRUCT   // MapPos, MapRange, Variant, a vector - carried as JSON
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
    };

    struct PropertyEntry {
        const char* path;
        PropertyType type;
        std::uint8_t flags;
        // Null for a type the accessors cannot carry yet (OBJECT, STRUCT), for a static, and -
        // for setter - for a read-only property.
        void (*getter)(void* obj, PropertyValue& value);
        void (*setter)(void* obj, const PropertyValue& value);
    };

    struct ClassEntry {
        const char* cppClass;
        const PropertyEntry* props;
        std::uint16_t count;
    };

    /**
     * Looks up a class' property table by its fully qualified C++ name.
     * @param cppClass The class name, e.g. "massif::FogOptions".
     * @return The class entry, or null when the class declares no properties.
     */
    const ClassEntry* findClass(const char* cppClass);

    /**
     * Looks up one property of a class.
     * @param classEntry The class, from findClass.
     * @param path The property path, e.g. "rangeStart".
     * @return The property, or null when the class has no such property.
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
