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
#include <typeinfo>

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
        PF_STATIC = 2,
        // A MapPos or MapBounds, so a read can convert it to another projection. Nothing else is
        // a coordinate, and converting a MapRange or a ScreenPos would be nonsense.
        PF_POSITION = 4,
        // An OBJECT property pointing at a Projection - whatever the class calls it. This is how
        // a read learns the coordinate system of the PF_POSITION properties beside it.
        PF_PROJECTION = 8
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
        /** The value as text, whatever field carries it. */
        std::string asString() const;

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
        // Set only for OBJECT. Reading is enough to traverse a dotted path.
        void (*objectGetter)(void* obj, ObjectRef& out);
        // Set for a writable OBJECT. The thunk casts from shared_ptr<void>, so the caller MUST
        // have checked the value's registered class against objectClass first.
        void (*objectSetter)(void* obj, const ObjectRef& value);
        // The class an OBJECT property points at, e.g. "massif::Projection". Null otherwise.
        const char* objectClass;
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
     * One class' runtime type, so a traversal can name what it actually found.
     */
    struct ClassTypeEntry {
        const std::type_info* type;
        const char* cppClass;
    };

    /**
     * The class of an object as it really is, rather than as the property that reached it declares.
     *
     * A `tileDecoder` declared as a `VectorTileDecoder` is nearly always an `MBVectorTileDecoder`,
     * and everything the subclass adds is unreachable by name without this. Falls back to the
     * declared name for a class the profile does not build, so the walk never loses its footing.
     */
    const char* concreteClass(const std::type_info& type, const char* declared);

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
     * Finds the class' PF_PROJECTION property, walking up its base chain.
     *
     * Scanned rather than looked up by name: a class is free to call it "projection" or
     * "baseProjection", and the facade should not have to know which.
     * @return The property, or null when nothing in the chain declares one.
     */
    const PropertyEntry* findProjectionProperty(const ClassEntry* classEntry);

    /**
     * Whether one class is the other, or derives from it, per the table's base chain.
     *
     * This is what makes writing an object property safe: the thunk casts from a type-erased
     * pointer, so the value's registered class has to be checked first. An unknown class is not a
     * subclass of anything, so the check fails closed.
     */
    bool isSubclassOf(const char* cppClass, const char* base);

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
