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
        // A MapPos or MapBounds, so a read can convert it to another projection.
        PF_POSITION = 4,
        // An OBJECT property pointing at a Projection: the coordinate system of the PF_POSITION properties beside it.
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
        // Which field a getter filled, so a mistyped read is distinguishable from a real 0.
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

    /**
     * A property whose value is a bag of named entries (style parameters, HTTP headers, metadata);
     * the rest of the path is the key: `params.water_color`. Both thunks return whether the key exists.
     */
    struct IndexedAccess {
        bool (*getter)(void* obj, const std::string& key, PropertyValue& value);
        bool (*setter)(void* obj, const std::string& key, const PropertyValue& value);
    };

    struct PropertyEntry {
        const char* path;
        PropertyType type;
        std::uint8_t flags;
        // Null for a type the accessors cannot carry yet (STRUCT), for a static, and (setter) when read-only.
        void (*getter)(void* obj, PropertyValue& value);
        void (*setter)(void* obj, const PropertyValue& value);
        // Set only for OBJECT. Reading is enough to traverse a dotted path.
        void (*objectGetter)(void* obj, ObjectRef& out);
        // Set for a writable OBJECT. Casts from shared_ptr<void>: the caller must check the class against objectClass.
        void (*objectSetter)(void* obj, const ObjectRef& value);
        // The class an OBJECT property points at, e.g. "massif::Projection". Null otherwise.
        const char* objectClass;
        // Null for all but the handful of bag properties - see IndexedAccess.
        const IndexedAccess* indexed;
    };

    /** A second spelling of one path segment - `fog` for `fogOptions`, so `fog.rangeStart` also resolves. */
    struct AliasEntry {
        const char* alias;
        const char* path;
    };

    struct ClassEntry {
        const char* cppClass;
        const PropertyEntry* props;   // null for a class that declares none of its own
        std::uint16_t count;
        // Lookups walk this chain rather than the table being flattened.
        const char* base;
        const AliasEntry* aliases;    // null for a class that declares none
        std::uint16_t aliasCount;
    };

    /** One enum constant, by the name a spec spells it with. */
    struct EnumConstantEntry {
        const char* name;
        long long value;
    };

    /**
     * An enum constant's value, by name. False when nothing goes by that name.
     * For raw-spec paths, where an enum arrives as a string that `strtoll` would silently turn into 0.
     */
    bool enumValueOf(const char* name, long long& value);

    /**
     * One class' runtime type, so a traversal can name what it actually found.
     */
    struct ClassTypeEntry {
        const std::type_info* type;
        const char* cppClass;
    };

    /**
     * The object's concrete class rather than the declared one (a `tileDecoder` is an `MBVectorTileDecoder`).
     * Falls back to the declared name for a class the profile does not build.
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
     * The property an alias stands for, walking up the class' base chain.
     * @return The real path, or null when nothing goes by that alias.
     */
    const char* findAlias(const ClassEntry* classEntry, const char* alias);

    /**
     * Finds the class' PF_PROJECTION property, walking up its base chain; scanned, since its name varies.
     * @return The property, or null when nothing in the chain declares one.
     */
    const PropertyEntry* findProjectionProperty(const ClassEntry* classEntry);

    /**
     * Whether one class is the other, or derives from it, per the table's base chain.
     * Guards object-property writes (type-erased cast); an unknown class fails closed.
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
